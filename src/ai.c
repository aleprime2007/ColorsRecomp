#include "ai.h"

#define AI_CTRL_OFF 0x00u
#define AI_VOLUME_OFF 0x04u
#define AI_SAMPLE_COUNT_OFF 0x08u
#define AI_INT_TIMING_OFF 0x0Cu

static u32 g_ai_control;
static u32 g_ai_volume;
static u32 g_ai_int_timing;
static u32 g_ai_sample_base;
static u64 g_ai_sample_accum;

bool ai_mmio_contains(u32 addr) {
    return addr >= AI_BASE && addr < AI_BASE + AI_SIZE;
}

static u32 ai_sample_counter(CPUState* cpu) {
    // AI_CONTROL bit 0 = stream playing. AIDFR (bit 6) selects 32 kHz.
    const u32 rate = (g_ai_control & 0x40u) != 0u ? 32000u : 48000u;
    const u64 samples = (cpu->timebase * (u64)rate) / 40500000ull;
    return (g_ai_sample_base + (u32)samples) & 0x7FFFFFFFu;
}

u64 ai_mmio_read(CPUState* cpu, u32 off, u8 size) {
    (void)size;
    switch (off) {
    case AI_CTRL_OFF: return g_ai_control;
    case AI_VOLUME_OFF: return g_ai_volume;
    case AI_SAMPLE_COUNT_OFF: return ai_sample_counter(cpu);
    case AI_INT_TIMING_OFF: return g_ai_int_timing;
    default: return 0;
    }
}

void ai_mmio_write(CPUState* cpu, u32 off, u8 size, u64 value) {
    (void)size;
    if (off == AI_CTRL_OFF) {
        // Re-basing on play start keeps the counter monotonic across toggles.
        if (((u32)value & 1u) != 0u && (g_ai_control & 1u) == 0u)
            g_ai_sample_base = ai_sample_counter(cpu);
        g_ai_control = (u32)value;
        return;
    }
    if (off == AI_VOLUME_OFF) {
        g_ai_volume = (u32)value;
        return;
    }
    if (off == AI_INT_TIMING_OFF) {
        g_ai_int_timing = (u32)value;
        return;
    }
    (void)g_ai_sample_accum;
}
