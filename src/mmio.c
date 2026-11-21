#include "mmio.h"

#include "ai.h"
#include "dsp.h"
#include "dvd.h"
#include "gx.h"
#include "interrupt.h"
#include "ios.h"
#include "log.h"
#include "mem.h"
#include "pi.h"

#include <stdio.h>

// Broadway PVR (the Wii's Gekko-derivative CPU).
#define BROADWAY_PVR 0x00087200u

// Match DolRecomp's decoder so our instruction fallback decodes SPRs the same
// way the emitter did.
#define PPC_SPR(raw) ((((raw) >> 16) & 0x1Fu) | (((raw) >> 6) & 0x3E0u))
#define PPC_RD(raw)  (((raw) >> 21) & 0x1Fu)

static u32 g_spr_shadow[1024];   // read-back for environment SPRs

void mmio_init(void) {
    g_spr_shadow[287] = BROADWAY_PVR;   // PVR
}

// ---------------------------------------------------------------------------
// External (non-MEM1) memory: MEM2, then the device blocks, then a log for the
// rest.
// ---------------------------------------------------------------------------
static u64 ext_read(CPUState* cpu, u32 addr, u8 size) {
    // Physical MEM1 (0x00000000..0x01800000) -> ram offset directly.
    if (addr < 0x01800000u)
        return ram_read(cpu, addr, size);
    // Physical MEM2 (0x10000000..0x14000000) -> mem2.
    if (addr >= 0x10000000u && addr < 0x14000000u)
        return mem2_read(addr - 0x10000000u, size);
    if (mem2_ready()) {
        u32 off = mem2_offset(addr);
        if (off != (u32)-1)
            return mem2_read(off, size);
    }
    if (gx_fifo_contains(addr))
        return gx_fifo_read(addr);
    if (dsp_mmio_contains(addr))
        return dsp_mmio_read(addr - DSP_BASE, size);
    if (ios_ipc_contains(addr))
        return ios_ipc_read(cpu, addr - IPC_BASE, size);
    if (pi_fifo_contains(addr))
        return pi_fifo_read(addr);
    if (interrupt_mmio_contains(addr))
        return interrupt_mmio_read(addr, size);
    if (dvd_mmio_contains(addr))
        return dvd_mmio_read(addr, size);
    if (ai_mmio_contains(addr))
        return ai_mmio_read(cpu, addr - AI_BASE, size);
    if (g_mmio_log)
        fprintf(stderr, "[mmio] read%u 0x%08X (pc=0x%08X lr=0x%08X)\n",
                size, addr, cpu->pc, cpu->lr);
    return 0;
}

static void ext_write(CPUState* cpu, u32 addr, u64 val, u8 size) {
    if (addr < 0x01800000u) {
        ram_write(cpu, addr, val, size);
        return;
    }
    if (addr >= 0x10000000u && addr < 0x14000000u) {
        mem2_write(addr - 0x10000000u, val, size);
        return;
    }
    if (mem2_ready()) {
        u32 off = mem2_offset(addr);
        if (off != (u32)-1) {
            mem2_write(off, val, size);
            return;
        }
    }
    if (gx_fifo_contains(addr)) {
        gx_fifo_write(addr, val, size);
        return;
    }
    if (dsp_mmio_contains(addr)) {
        dsp_mmio_write(addr - DSP_BASE, val, size);
        return;
    }
    if (ios_ipc_contains(addr)) {
        ios_ipc_write(cpu, addr - IPC_BASE, size, val);
        return;
    }
    if (pi_fifo_contains(addr)) {
        pi_fifo_write(addr, val);
        return;
    }
    if (interrupt_mmio_contains(addr)) {
        interrupt_mmio_write(addr, size, val);
        return;
    }
    if (dvd_mmio_contains(addr)) {
        dvd_mmio_write(cpu, addr, size, val);
        return;
    }
    if (ai_mmio_contains(addr)) {
        ai_mmio_write(cpu, addr - AI_BASE, size, val);
        return;
    }
    if (g_mmio_log)
        fprintf(stderr, "[mmio] write%u 0x%08X = 0x%llX (pc=0x%08X lr=0x%08X)\n",
                size, addr, (unsigned long long)val, cpu->pc, cpu->lr);
}

// ---------------------------------------------------------------------------
// Instruction fallback: cache maintenance + unmodeled SPRs.
// ---------------------------------------------------------------------------
static void instr_fallback(CPUState* cpu, u32 raw, u32 cia) {
    if ((raw >> 26) != 31u) {
        fprintf(stderr, "[fallback] unhandled instruction 0x%08X at 0x%08X\n", raw, cia);
        cpu->exception |= PPC_EXC_PROGRAM;
        return;
    }
    const u32 xo = (raw >> 1) & 0x3FFu;
    switch (xo) {
    case 982u:  // icbi
    case 86u:   // dcbf
    case 54u:   // dcbst
    case 470u:  // dcbi
        cpu->pc = cia + 4u;   // no cache to maintain
        return;
    case 339u: { // mfspr
        const u32 spr = PPC_SPR(raw);
        const u32 rd  = PPC_RD(raw);
        cpu->gpr[rd] = (spr == 22u) ? interrupt_get_decrementer()
                                    : g_spr_shadow[spr];
        cpu->pc = cia + 4u;
        return;
    }
    case 467u: { // mtspr
        const u32 spr = PPC_SPR(raw);
        const u32 rs  = PPC_RD(raw);
        g_spr_shadow[spr] = cpu->gpr[rs];
        if (spr == 22u)  // decrementer: arm the OS alarm/timer exception
            interrupt_set_decrementer(cpu->gpr[rs]);
        cpu->pc = cia + 4u;
        return;
    }
    default:
        fprintf(stderr, "[fallback] unhandled XO %u (0x%08X) at 0x%08X\n",
                xo, raw, cia);
        cpu->exception |= PPC_EXC_PROGRAM;
        return;
    }
}

void mmio_attach(CPUState* cpu) {
    cpu->external_read = ext_read;
    cpu->external_write = ext_write;
    cpu->instruction_fallback = instr_fallback;
}
