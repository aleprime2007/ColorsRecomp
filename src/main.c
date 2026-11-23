// ColorsRecomp host entry point.
//
// Loads the recompiled Sonic Colors (Wii) main.dol into guest RAM, sets up a
// Wii boot environment, and drives the generated block dispatcher from the
// DOL entry point. Reports where execution stops (exception, MMIO access, or a
// pc that left recompiled code).
//
// The host is split into small modules:
//   mem.c        MEM1/MEM2 accessors
//   mmio.c       bus dispatch + instruction fallback
//   dsp.c        DSP mailbox / ARAM DMA (0xCC005000)
//   ai.c         audio interface (0xCD006C00)
//   pi.c         PI/GX FIFO registers (0xCC003000)
//   ios.c        IOS IPC + file descriptors + device service
//   boot_env.c   apploader/OS state reconstruction
//   dbg.c        diagnostics, per-block watches, final state report
//   interrupt.c  VI retrace / IPC / decrementer
//   dvd.c        DI + EXI register translation
//   disc.c       virtual disc image from game/files
//
// The generated header provides the CPU ABI (core/cpu.h), every func_XXXXXXXX
// prototype, and the static-inline dispatcher helpers dolrecomp_call() /
// dolrecomp_run_blocks() / DOLRECOMP_ENTRY_POINT.
#include "generated.h"

#include "boot_env.h"
#include "chunk_table.h"
#include "core/cpu.h"
#include "dbg.h"
#include "disc.h"
#include "gxruntime/loader.h"
#include "dvd.h"
#include "gx.h"
#include "interrupt.h"
#include "ios.h"
#include "log.h"
#include "mem.h"
#include "mmio.h"

#include "window.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_DOL_PATH "game/sys/main.dol"
#define DEFAULT_MAX_BLOCKS 100000000ull

// Persistent guest-cycle -> timebase accumulator.
static u64 g_cycle_accum = 0;

int main(int argc, char** argv) {
    setvbuf(stderr, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    const char* dol_path = DEFAULT_DOL_PATH;
    unsigned long long max_blocks = DEFAULT_MAX_BLOCKS;
    bool headless = getenv("COLORS_HEADLESS") != NULL;
    bool max_blocks_set = false;
    int window_scale = 2;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--max-blocks") == 0 && i + 1 < argc) {
            max_blocks = strtoull(argv[++i], NULL, 0);
            max_blocks_set = true;
        }
        else if (strcmp(argv[i], "--mmio-log") == 0)
            g_mmio_log = 1;
        else if (strcmp(argv[i], "--headless") == 0)
            headless = true;
        else if (strcmp(argv[i], "--window") == 0)
            headless = false;
        else if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
            window_scale = (int)strtol(argv[++i], NULL, 0);
            headless = false;
        } else if (argv[i][0] != '-')
            dol_path = argv[i];
        else {
            fprintf(stderr,
                    "usage: %s [dol-path] [--max-blocks N] [--mmio-log] "
                    "[--window|--headless] [--scale N]\n",
                    argv[0]);
            return 2;
        }
    }

    if (!mem2_init()) {
        fprintf(stderr, "error: failed to allocate MEM2\n");
        return 1;
    }

    // Default environment SPR state.
    mmio_init();

    CPUState cpu;
    if (!cpu_init(&cpu)) {
        fprintf(stderr, "error: cpu_init failed\n");
        mem2_shutdown();
        return 1;
    }

    dbg_install_write_watch(&cpu);

    // Open the window before dbg_init/gx_init: window mode implies the two
    // HLE shortcuts that let the game run unattended (the system font is not
    // present on the host, and GX decoding is what feeds the rasteriser).
    if (!headless) {
        if (window_init(640, 480, "ColorsRecomp", window_scale)) {
            // Interactive: run until the window is closed unless the caller
            // asked for a specific block budget.
            if (!max_blocks_set)
                max_blocks = ~0ull;
        } else {
            fprintf(stderr, "[run] no window available; continuing headless\n");
        }
    }

    interrupt_init();
    dvd_init();
    disc_init();
    dbg_init();

    DolLayout layout;
    if (!dol_load_into_ram(&cpu, dol_path, &layout)) {
        fprintf(stderr, "error: failed to load '%s'\n", dol_path);
        cpu_free(&cpu);
        mem2_shutdown();
        return 1;
    }

    printf("[run] DOL entry point: 0x%08X\n", layout.entry_point);
    if (layout.entry_point != DOLRECOMP_ENTRY_POINT)
        fprintf(stderr, "warning: DOL entry 0x%08X != recompiled entry 0x%08X\n",
                layout.entry_point, (u32)DOLRECOMP_ENTRY_POINT);

    boot_env_setup(&cpu, &layout);
    gx_init(&cpu);
    mmio_attach(&cpu);

    cpu.pc = layout.entry_point;

    printf("[run] starting execution at 0x%08X (max %llu blocks)\n",
           cpu.pc, max_blocks);

    unsigned long long blocks = 0;
    const char* stop_reason = "max-blocks watchdog";
    u32 last_pc = cpu.pc;
    const bool check_sp = getenv("COLORS_CHECK_SP") != NULL;

    while (blocks < max_blocks) {
        u32 pc = cpu.pc;
        // Translate physical MEM1/MEM2 addresses to their cached virtual form.
        // The OS context-switch (__OSContextSwitch/OSLoadContext) runs in real
        // mode and rfis to physical addresses; the recompiled code lives at
        // virtual addresses.
        if (pc < 0x01800000u)
            pc += GC_RAM_BASE;                       // MEM1 physical
        else if (pc >= 0x10000000u && pc < 0x14000000u)
            pc += 0x80000000u;                       // MEM2 physical
        cpu.pc = pc;
        dbg_pc_push(pc);
        dbg_block_enter(&cpu, pc, blocks, last_pc);

        // Idle fast-forward: the OS idle thread spins on an empty poll queue
        // with nothing ready to dispatch. Advance straight to the next
        // interrupt instead of executing the (dominant) idle blocks; the guest
        // timeline and every interrupt stay identical.
        {
            const u32 idle_ticks = interrupt_idle_skip_ticks(&cpu);
            if (idle_ticks != 0u) {
                cpu.timebase += idle_ticks;
                interrupt_advance_decrementer(&cpu, idle_ticks);
                ios_poll_delayed(&cpu);
                interrupt_force_poll(&cpu);   // deliver the retrace now
                if ((blocks & 0x3FFull) == 0ull && !window_poll()) {
                    stop_reason = "window closed";
                    break;
                }
                blocks++;
                last_pc = pc;
                continue;
            }
        }

        // Deliver a due VI-retrace interrupt before dispatching the next block.
        interrupt_poll(&cpu);
        if (interrupt_stop_requested()) {
            stop_reason = "retrace limit";
            break;
        }

        if (!chunk_dispatch(&cpu, pc)) {
            stop_reason = "pc left recompiled code (no handler)";
            fprintf(stderr, "[run] pc=0x%08X has no recompiled block (from 0x%08X, lr=0x%08X)\n",
                    cpu.pc, last_pc, cpu.lr);
            break;
        }
        last_pc = pc;

        // The GX decoder needs a run of FIFO bytes to validate a draw's vertex
        // stride (and to reassemble commands that straddle writes). Flushing
        // after every block gives it a few bytes at a time, so flush on a coarse
        // cadence instead; staging still flushes when its buffer fills.
        if ((blocks & 0x3FFFu) == 0u)
            gx_flush();
        dbg_post_block(&cpu, pc);

        if ((blocks & 0xFFFFull) == 0ull && !window_poll()) {
            stop_reason = "window closed";
            break;
        }

        if (cpu.exception) {
            // GameCube/Wii use `sc` as a post-cache-op sync barrier; the OS's
            // default system-call vector just syncs and returns (rfi).
            if (cpu.exception == PPC_EXC_SYSTEM_CALL) {
                cpu.exception = 0;
                ppc_rfi(&cpu, cpu.pc);
                blocks++;
                continue;
            }
            stop_reason = "cpu exception";
            break;
        }

        // Generated code charges guest cycle cost into `downcount` (a negative
        // accumulator). Accumulate it and convert to timebase ticks so the
        // guest's OSGetTick / timing loops see time advance. Broadway: CPU
        // 486 MHz, timebase 40.5 MHz -> 12 cycles per timebase tick. Use a
        // persistent accumulator because individual blocks are often only a
        // few cycles (integer /12 would truncate to 0 forever).
        if (cpu.downcount < 0) {
            g_cycle_accum += (u64)(-cpu.downcount);
            cpu.downcount = 0;
            if (g_cycle_accum >= 12u) {
                u64 ticks = g_cycle_accum / 12u;
                cpu.timebase += ticks;
                g_cycle_accum %= 12u;
                // The guest decrementer counts down at the same rate; this is
                // what makes OSSetAlarm/timers fire (OSAlarmHandler).
                interrupt_advance_decrementer(&cpu, (u32)ticks);
                ios_poll_delayed(&cpu);
            }
        }

        // Stack sanity: the guest stack must live in MEM1/MEM2. Catch a bad r1
        // at the first block that produces it (helps localize context/restore
        // bugs).
        if (check_sp) {
            u32 sp = cpu.gpr[1];
            if (sp < GC_RAM_BASE || sp >= 0x91000000u) {
                fprintf(stderr,
                        "[sp] bad r1=0x%08X after pc=0x%08X lr=0x%08X (r3=0x%08X r4=0x%08X r5=0x%08X)\n",
                        sp, last_pc, cpu.lr, cpu.gpr[3], cpu.gpr[4], cpu.gpr[5]);
                fprintf(stderr, "[sp] recent PCs:");
                dbg_print_pc_history();
                fprintf(stderr, "\n");
                dbg_dump_backchain(&cpu);
                break;
            }
        }

        blocks++;
    }

    gx_finalize();
    dbg_report_final(&cpu, blocks, stop_reason);
    window_shutdown();

    cpu_free(&cpu);
    mem2_shutdown();
    return 0;
}
