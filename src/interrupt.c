// VI vertical-retrace external interrupt delivery.
// Ported from StrikersRecomp's runtime/host/interrupt.c, adapted for the
// the GXRuntime headers and Sonic Colors' __OSDispatchInterrupt address.
#include "interrupt.h"

#include "gxruntime/interrupts.h"
#include "gxruntime/vi_clock.h"

#include "dsp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MSR_EE 0x00008000u  // MSR[EE], external interrupts enabled (bit 16)

// Guest globals / entry points.
#define OS_CURRENT_CONTEXT 0x800000D4u  // OSContext* __OSCurrentContext
#define DISPATCH_INTERRUPT 0x803402E0u  // __OSDispatchInterrupt (verified via Ghidra)
#define EXC_EXTERNAL 4u                 // __OS_EXCEPTION_EXTERNAL_INTERRUPT
#define EXC_DECREMENTER 8u              // __OS_EXCEPTION_DECREMENTER
// __OSAlarmHandler body: the decrementer vector stub (0x8033A5E0) saves the
// remaining registers and branches here. We save the whole context ourselves,
// so jump straight to the body (skipping the stub avoids clobbering the saved
// r3/r4/r5, which the stub expects the vector prologue to have saved).
#define ALARM_HANDLER 0x8033A3B0u

// OSContext field offsets (dolphin/os/OSContext.h).
#define CTX_CR 0x080u
#define CTX_LR 0x084u
#define CTX_CTR 0x088u
#define CTX_XER 0x08Cu
#define CTX_FPR 0x090u
#define CTX_FPSCR 0x194u
#define CTX_SRR0 0x198u
#define CTX_SRR1 0x19Cu
#define CTX_STATE 0x1A2u
#define CTX_GQR 0x1A4u
#define OS_CONTEXT_STATE_FPSAVED 0x0002u

// VI retrace cadence. The retrace clock is advanced by *guest timebase ticks*,
// so one frame is exactly 40.5 MHz / 60 = 675,000 ticks, as on hardware.
// (Previously it was driven by dispatched-block counts, which produced over
// a thousand "frames" per emulated second and broke timing-dependent code.)
#define GC_TIMEBASE_HZ 40500000ull
#define VI_REFRESH_HZ 60ull
#define DEFAULT_TICKS_PER_RETRACE (GC_TIMEBASE_HZ / VI_REFRESH_HZ)

// Host polls between batched retrace/interrupt-delivery evaluations.
#define INTERRUPT_POLL_INTERVAL 128u

static DolInterrupts interrupts;
static DolViClock vi_clock;
static u64 frame_work_units;
static u32 interrupt_poll_accum;
static u64 last_timebase;

// Decrementer (SPR 22). Counts down at the timebase rate. The exception is
// latched on the 0->1 transition of the MSB and delivered when MSR[EE] is set.
// Idle fast-forward state.
static bool g_idle_skip;
static u64 g_idle_skips;
static u64 g_idle_ticks;

static u64 g_stop_retrace;
static bool g_stop_requested;
static u64 g_stat_retraces;
static u64 g_stat_ext;
static u64 g_stat_dec;
static u32 g_decrementer;
static bool g_decrementer_latched;
static bool g_decrementer_armed;  // guest has programmed DEC at least once

// Exception-delivery tracing (COLORS_TRACE_EXC=1) for debugging nested
// interrupt/handler problems.
static int g_trace_exc;
static int g_trace_irq;
static void trace_exc(const char* kind, const CPUState* cpu, u32 ctx) {
    if (!g_trace_exc) return;
    fprintf(stderr, "[exc] %-8s pc=0x%08X r1=0x%08X ctx=0x%08X msr=0x%08X tb=0x%llX\n",
            kind, cpu->pc, cpu->gpr[1], ctx, cpu->msr,
            (unsigned long long)cpu->timebase);
}

void interrupt_init(void) {
    g_trace_exc = getenv("COLORS_TRACE_EXC") != NULL;
    g_trace_irq = getenv("COLORS_TRACE_IRQ") != NULL;
    dol_interrupts_init(&interrupts);
    dol_vi_clock_init(&vi_clock);
    interrupt_poll_accum = 0;
    frame_work_units = DEFAULT_TICKS_PER_RETRACE;
    const char* env = getenv("COLORS_FRAME_BLOCKS");
    if (env != NULL && env[0] != '\0') {
        char* end = NULL;
        unsigned long long value = strtoull(env, &end, 0);
        if (end != env && value != 0)
            frame_work_units = value;
    }
    dol_vi_clock_configure(&vi_clock, frame_work_units, (u32)VI_REFRESH_HZ,
                           GC_TIMEBASE_HZ);
    g_idle_skip = getenv("COLORS_NO_IDLE_SKIP") == NULL;
    {
        const char* stop = getenv("COLORS_STOP_RETRACE");
        if (stop != NULL)
            g_stop_retrace = strtoull(stop, NULL, 0);
    }
}

// Guest addresses the idle check needs (SDK globals, reached through r13).
#define IDLE_SPIN_PC 0x803EBF48u          // idle thread loop head
#define R13_IDLE_POLL_QUEUE 21336u        // OSThreadQueue the idle loop polls
#define R13_READY_MASK 23432u             // __OSReadyThreadQueue priority bits
#define MSR_EE_BIT 0x00008000u

u32 interrupt_idle_skip_ticks(CPUState* cpu) {
    if (!g_idle_skip || cpu == NULL || cpu->pc != IDLE_SPIN_PC)
        return 0u;
    // Only when the guest itself has decided it is idle: nothing is waiting on
    // the polled queue and no thread is in the ready queue.
    if (mem_read32(cpu, cpu->gpr[13] - R13_IDLE_POLL_QUEUE) != 0u)
        return 0u;
    if (mem_read32(cpu, cpu->gpr[13] - R13_READY_MASK) != 0u)
        return 0u;
    // With external interrupts masked the guest is not in an idle wait and a
    // skipped retrace would never be delivered.
    if ((cpu->msr & MSR_EE_BIT) == 0u)
        return 0u;

    const u64 per = dol_vi_clock_timebase_ticks_per_retrace(&vi_clock);
    if (per == 0ull)
        return 0u;
    const u64 now = dol_vi_clock_now(&vi_clock);
    u64 ticks = per - (now % per);
    // Stop at the decrementer (OSAlarm) deadline when it comes first.
    const u32 dec = interrupt_get_decrementer();
    if (dec != 0u && (u64)dec < ticks)
        ticks = (u64)dec;
    if (ticks == 0ull)
        ticks = 1ull;
    if (ticks > per)
        ticks = per;
    g_idle_skips++;
    g_idle_ticks += ticks;
    return (u32)ticks;
}

void interrupt_force_poll(CPUState* cpu) {
    // Make the very next interrupt_poll() perform a full delivery pass.
    interrupt_poll_accum = INTERRUPT_POLL_INTERVAL - 1u;
    interrupt_poll(cpu);
}

bool interrupt_stop_requested(void) {
    return g_stop_requested;
}

void interrupt_idle_stats(u64* skips, u64* ticks) {
    if (skips != NULL) *skips = g_idle_skips;
    if (ticks != NULL) *ticks = g_idle_ticks;
}

void interrupt_stats(u64* retraces, u64* external, u64* decrementer) {
    if (retraces != NULL) *retraces = g_stat_retraces;
    if (external != NULL) *external = g_stat_ext;
    if (decrementer != NULL) *decrementer = g_stat_dec;
}

bool interrupt_mmio_contains(u32 ea) {
    return dol_interrupts_mmio_contains(ea);
}

u64 interrupt_mmio_read(u32 ea, u8 size) {
    u64 v = dol_interrupts_mmio_read(&interrupts, ea, size);
    if (g_trace_irq && (ea == DOL_PI_INTERRUPT_CAUSE || ea == DOL_PI_INTERRUPT_MASK)) {
        static u32 last_cause, last_mask;
        if ((ea == DOL_PI_INTERRUPT_CAUSE && (u32)v != last_cause) ||
            (ea == DOL_PI_INTERRUPT_MASK && (u32)v != last_mask)) {
            if (ea == DOL_PI_INTERRUPT_CAUSE) last_cause = (u32)v; else last_mask = (u32)v;
            fprintf(stderr, "[irq] read  0x%08X -> 0x%08X\n", ea, (u32)v);
        }
    }
    return v;
}

void interrupt_mmio_write(u32 ea, u8 size, u64 value) {
    if (g_trace_irq && (ea == DOL_PI_INTERRUPT_CAUSE || ea == DOL_PI_INTERRUPT_MASK))
        fprintf(stderr, "[irq] write 0x%08X = 0x%08X (cause before 0x%08X)\n",
                ea, (u32)value, dol_interrupts_pi_cause(&interrupts));
    dol_interrupts_mmio_write(&interrupts, ea, size, value);
}

// Save the interrupted CPU state into an OSContext image in guest memory.
static void save_context(CPUState* cpu, u32 ctx) {
    for (int i = 0; i < 32; i++)
        mem_write32(cpu, ctx + (u32)i * 4u, cpu->gpr[i]);
    mem_write32(cpu, ctx + CTX_CR, cpu->cr);
    mem_write32(cpu, ctx + CTX_LR, cpu->lr);
    mem_write32(cpu, ctx + CTX_CTR, cpu->ctr);
    mem_write32(cpu, ctx + CTX_XER, cpu->xer);
    for (int i = 0; i < 32; i++) {
        u64 bits;
        memcpy(&bits, &cpu->fpr[i], sizeof bits);
        mem_write64(cpu, ctx + CTX_FPR + (u32)i * 8u, bits);
    }
    mem_write32(cpu, ctx + CTX_FPSCR, cpu->fpscr);
    mem_write32(cpu, ctx + CTX_SRR0, cpu->pc);
    mem_write32(cpu, ctx + CTX_SRR1, cpu->msr);
    mem_write16(cpu, ctx + CTX_STATE, OS_CONTEXT_STATE_FPSAVED);
    for (int i = 0; i < 8; i++)
        mem_write32(cpu, ctx + CTX_GQR + (u32)i * 4u, cpu->gqr[i]);
}

// Save the interrupted CPU state into the current OSContext and vector to the
// recompiled __OSDispatchInterrupt. It dispatches to the VI handler (which acks
// the VI register and wakes the retrace waiter), reschedules, and ends in
// OSLoadContext -> rfi.
static void deliver_external(CPUState* cpu) {
    u32 ctx = mem_read32(cpu, OS_CURRENT_CONTEXT);
    if (ctx < GC_RAM_BASE || ctx >= GC_RAM_BASE + cpu->ram_size)
        return;  // no current context established yet

    save_context(cpu, ctx);
    g_stat_ext++;
    trace_exc("external", cpu, ctx);

    cpu->gpr[3] = EXC_EXTERNAL;
    cpu->gpr[4] = ctx;
    cpu->srr0 = cpu->pc;
    cpu->srr1 = cpu->msr;
    cpu->msr &= ~MSR_EE;  // run the handler with external interrupts masked
    cpu->pc = DISPATCH_INTERRUPT;
}

// Deliver the decrementer exception to the guest OS alarm handler.
static void deliver_decrementer(CPUState* cpu) {
    u32 ctx = mem_read32(cpu, OS_CURRENT_CONTEXT);
    if (ctx < GC_RAM_BASE || ctx >= GC_RAM_BASE + cpu->ram_size)
        return;  // no current context established yet

    save_context(cpu, ctx);
    g_stat_dec++;
    trace_exc("decrementer", cpu, ctx);

    cpu->gpr[3] = EXC_DECREMENTER;
    cpu->gpr[4] = ctx;
    cpu->srr0 = cpu->pc;
    cpu->srr1 = cpu->msr;
    cpu->msr &= ~MSR_EE;
    cpu->pc = ALARM_HANDLER;
}

void interrupt_poll(CPUState* cpu) {
    // Advance the VI retrace clock from guest timebase ticks (the host adds
    // them in main.c from cycle accounting). Each poll feeds the ticks elapsed
    // since the previous poll, so frames stay locked to emulated time.
    const u64 now = cpu->timebase;
    const u64 elapsed = (now > last_timebase) ? (now - last_timebase) : 0u;
    last_timebase = now;
    if (elapsed != 0u)
        dol_vi_clock_advance(&vi_clock, elapsed);

    if (++interrupt_poll_accum < INTERRUPT_POLL_INTERVAL)
        return;
    interrupt_poll_accum = 0;

    u64 ticks = 0;
    bool retraced = false;
    while (dol_vi_clock_pop_retrace(&vi_clock, &ticks)) {
        dol_interrupts_assert_vi_retrace(&interrupts);
        retraced = true;
        g_stat_retraces++;
        if (g_stop_retrace != 0ull && g_stat_retraces >= g_stop_retrace)
            g_stop_requested = true;
    }
    // The AID/audio-DMA interrupt shares the frame cadence with the retrace in
    // this model. Assert it once per frame while it is not already outstanding;
    // the guest's handler acks it via DSP DSCR bit 3 (see dsp.c).
    if (retraced) {
        interrupt_assert_audio_dma(cpu);
        interrupt_commit_draw_done(cpu);
    }

    if (!(cpu->msr & MSR_EE))
        return;  // interrupts disabled; stay pending
    if (!dol_interrupts_external_pending(&interrupts))
        return;  // no pending source is unmasked yet

    deliver_external(cpu);
}

// The IOS (Starlet) interrupt reaches the PPC as PI cause bit 0x4000 (INT_IPC).
// Assert it so the guest's IPC interrupt handler runs and completes a request.
void interrupt_assert_ipc(void) {
    dol_interrupts_set_source(&interrupts, 0x00004000u, true);
}

// Drop the IPC interrupt once the guest has acknowledged the reply.
void interrupt_clear_ipc(void) {
    dol_interrupts_set_source(&interrupts, 0x00004000u, false);
}

void interrupt_assert_audio_dma(CPUState* cpu) {
    // Only raise it once the AID handler (SDK interrupt id 5) is installed;
    // otherwise the level stays asserted with nobody to ack it and the CPU
    // spins in __OSDispatchInterrupt.
    if (mem_read32(cpu, 0x80003040u + 5u * 4u) == 0u) {
        interrupt_clear_audio_dma();
        return;
    }
    if ((dol_interrupts_pi_cause(&interrupts) & DOL_PI_CAUSE_DSP) != 0u)
        return;  // already outstanding; do not storm the CPU
    // The PPC dispatcher reads DSCR to decide which DSP source fired, so the
    // AID status bit must be visible when the cause bit arrives.
    dsp_raise_aid();
    dol_interrupts_set_source(&interrupts, DOL_PI_CAUSE_DSP, true);
}

void interrupt_clear_audio_dma(void) {
    if ((dol_interrupts_pi_cause(&interrupts) & DOL_PI_CAUSE_DSP) == 0u)
        return;
    dol_interrupts_set_source(&interrupts, DOL_PI_CAUSE_DSP, false);
}

// The SDK's interrupt handler table lives in the OS globals block; the PE
// finish handler (id 19) is installed by the GX/AX init path.
#define OS_INT_HANDLER_TABLE 0x80003040u
#define OS_INT_PE_FINISH 19u

void interrupt_commit_draw_done(CPUState* cpu) {
    if ((dol_interrupts_pi_cause(&interrupts) & DOL_PI_CAUSE_PE_FINISH) != 0u)
        return;  // still outstanding; the guest has not acked the last one
    if (mem_read32(cpu, OS_INT_HANDLER_TABLE + OS_INT_PE_FINISH * 4u) == 0u)
        return;  // no handler installed yet (would latch forever)
    dol_interrupts_commit_pe_finish(&interrupts);
}

// ---------------------------------------------------------------------------
// Decrementer (SPR 22)
// ---------------------------------------------------------------------------
void interrupt_set_decrementer(u32 value) {
    g_decrementer = value;
    g_decrementer_armed = true;
}

u32 interrupt_get_decrementer(void) {
    return g_decrementer;
}

void interrupt_advance_decrementer(CPUState* cpu, u32 ticks) {
    if (!g_decrementer_armed)
        return;  // guest hasn't installed its alarm handler yet

    u32 before = g_decrementer;
    u32 after = before - ticks;
    if (ticks != 0 && (before & 0x80000000u) == 0 && (after & 0x80000000u) != 0)
        g_decrementer_latched = true;  // DEC MSB 0->1 = decrementer interrupt
    g_decrementer = after;

    if (!g_decrementer_latched)
        return;
    if (!(cpu->msr & MSR_EE))
        return;  // masked; stay latched until interrupts are re-enabled

    u32 ctx = mem_read32(cpu, OS_CURRENT_CONTEXT);
    if (ctx < GC_RAM_BASE || ctx >= GC_RAM_BASE + cpu->ram_size)
        return;  // no OS thread context yet

    g_decrementer_latched = false;
    deliver_decrementer(cpu);
}
