#include "dsp.h"

#include "interrupt.h"
#include "log.h"

#include <stdio.h>

#define DSP_DSCR_OFF 0x0Au
#define DSP_MAILBOX_OUT_HI_OFF 0x04u
#define DSP_MAILBOX_OUT_LO_OFF 0x06u
#define DSP_AR_DMA_CNT_HI_OFF 0x28u

static u16 g_dsp_control;
static bool g_mailbox_out_valid;   // DSP has a pending mailbox-out message
// DSP ARAM DMA registers (0xCC005020..0xCC005036). The AX driver sets up the
// transfer (address/length) and starts it by setting bit 15 of 0x36, then polls
// for completion. We complete instantly: clear the start bit on write.
static u16 g_aram_dma_mm_lo, g_aram_dma_mm_hi;   // 0x20 / 0x22 (main memory)
static u16 g_aram_dma_ar_lo, g_aram_dma_ar_hi;   // 0x30 / 0x32 (ARAM)
static u16 g_aram_dma_ctrl;                      // 0x36 (length>>5 + start bit)
static u16 g_aram_dma_cnt_lo, g_aram_dma_cnt_hi; // 0x24 / 0x26
static u16 g_dsp_dma_lo2, g_dsp_dma_hi2;         // 0x34 / 0x36 (aux pair)
static u16 g_dsp_dma_ptr_lo, g_dsp_dma_ptr_hi;   // 0x38 / 0x3A (current ptr)
static u32 g_mailbox_out_value;    // DSP->PPC message payload
static u32 g_mailbox_in_value;     // PPC->DSP command payload
static bool g_mailbox_in_hi_seen;

bool dsp_mmio_contains(u32 addr) {
    return addr >= DSP_BASE && addr < DSP_BASE + DSP_SIZE;
}

void dsp_raise_aid(void) {
    g_dsp_control |= DSP_DSCR_AID;
}

void dsp_dma_snapshot(DspDmaSnapshot* out) {    out->aram_dma_ar_lo = g_aram_dma_ar_lo;
    out->aram_dma_ar_hi = g_aram_dma_ar_hi;
    out->dma_ptr_lo = g_dsp_dma_ptr_lo;
    out->dma_ptr_hi = g_dsp_dma_ptr_hi;
}

u64 dsp_mmio_read(u32 off, u8 size) {
    (void)size;
    if (off == DSP_MAILBOX_OUT_HI_OFF) {
        u16 v = (u16)((g_mailbox_out_value >> 16) & 0x7FFFu);
        if (g_mailbox_out_valid)
            v |= 0x8000u;
        if (g_mmio_log)
            fprintf(stderr, "[dsp] read  0xCC0050%02X -> 0x%04X\n", off, v);
        return v;
    }
    if (off == DSP_MAILBOX_OUT_LO_OFF) {
        // Reading the low half consumes the DSP's mailbox message (the guest
        // reads HI to test the "available" bit, then LO to take the data).
        bool was_valid = g_mailbox_out_valid;
        u16 v = (u16)(g_mailbox_out_value & 0xFFFFu);
        g_mailbox_out_valid = false;
        if (g_mmio_log)
            fprintf(stderr, "[dsp] read  0xCC0050%02X -> 0x%04X%s\n", off, v,
                    was_valid ? " (consumed)" : "");
        return v;
    }
    if (off == DSP_DSCR_OFF) {
        if (g_mmio_log)
            fprintf(stderr, "[dsp] read  0xCC0050%02X -> 0x%04X\n", off, g_dsp_control);
        return g_dsp_control;
    }
    switch (off) {
    case 0x20: return g_aram_dma_mm_lo;
    case 0x22: return g_aram_dma_mm_hi;
    case 0x24: return g_aram_dma_cnt_lo;
    case 0x26: return g_aram_dma_cnt_hi;
    case 0x30: return g_aram_dma_ar_lo;
    case 0x32: return g_aram_dma_ar_hi;
    case 0x34: return g_dsp_dma_lo2;
    case 0x36: return g_aram_dma_ctrl;
    case 0x38: return g_dsp_dma_ptr_lo;
    case 0x3A: return g_dsp_dma_ptr_hi;
    default: break;
    }
    return 0;
}

void dsp_mmio_write(u32 off, u64 value, u8 size) {
    (void)size;
    if (g_mmio_log)
        fprintf(stderr, "[dsp] write 0xCC0050%02X = 0x%04llX\n", off,
                (unsigned long long)(value & 0xFFFFu));
    if (off == DSP_DSCR_OFF) {
        u16 v = (u16)value;
        // AID/AI/ARAM interrupt status bits are write-1-to-clear: the guest
        // handler acknowledges the audio-DMA interrupt by writing bit 3, which
        // deasserts the PI DSP source.
        if ((v & DSP_DSCR_AID) != 0u) {
            g_dsp_control &= (u16)~DSP_DSCR_AID;
            interrupt_clear_audio_dma();
        }
        v &= (u16)~(DSP_DSCR_INT | DSP_DSCR_AID | DSP_DSCR_ARAM_INT);
        const bool reset_released = (g_dsp_control & 0x0001u) != 0u &&
                                    (v & 0x0001u) == 0u;
        v &= (u16)~0x0001u;         // reset completes immediately
        g_dsp_control = v;
        if (reset_released || (v & 0x0800u) != 0u) {
            // DSP leaves reset: the ucode posts its boot/ready message.
            g_mailbox_out_valid = true;
            g_mailbox_out_value = 0x00000000u;
            if (g_mmio_log)
                fprintf(stderr, "[dsp] boot message posted (DSCR=0x%04X)\n", v);
        }
        return;
    }
    if (off == DSP_AR_DMA_CNT_HI_OFF) {
        g_dsp_control |= 0x0020u;   // ARAM DMA completes immediately
        g_mailbox_out_valid = true; // DSP boots and replies via the mailbox
        g_mailbox_out_value = 0x00000000u;
        return;
    }
    switch (off) {
    case 0x20: g_aram_dma_mm_lo = (u16)value; return;
    case 0x22: g_aram_dma_mm_hi = (u16)value; return;
    case 0x24: g_aram_dma_cnt_lo = (u16)value; return;
    case 0x26: g_aram_dma_cnt_hi = (u16)value; return;
    case 0x30: g_aram_dma_ar_lo = (u16)value; return;
    case 0x32: g_aram_dma_ar_hi = (u16)value; return;
    case 0x34: g_dsp_dma_lo2 = (u16)value; return;
    case 0x38: g_dsp_dma_ptr_lo = (u16)value; return;
    case 0x3A: g_dsp_dma_ptr_hi = (u16)value; return;
    case 0x36: {
        u16 v = (u16)value;
        g_aram_dma_ctrl = v;
        if ((v & 0x8000u) != 0u) {
            // Transfer completes immediately.
            g_aram_dma_ctrl = (u16)(v & 0x7FFFu);
            if (g_mmio_log)
                fprintf(stderr, "[dsp] aram dma mm=%04X%04X ar=%04X%04X len=%u\n",
                        g_aram_dma_mm_hi, g_aram_dma_mm_lo,
                        g_aram_dma_ar_hi, g_aram_dma_ar_lo,
                        (unsigned)(v & 0x7FFFu) << 5);
        }
        return;
    }
    default: break;
    }
    if (off == 0x00u || off == 0x02u) {
        // PPC->DSP mailbox. The DSP ucode answers every command; model an
        // immediate reply so the driver's command/response protocol advances.
        if (off == 0x00u) {
            g_mailbox_in_value = ((u32)(value & 0xFFFFu) << 16);
            g_mailbox_in_hi_seen = true;
        } else if (g_mailbox_in_hi_seen) {
            g_mailbox_in_value |= (u32)(value & 0xFFFFu);
            g_mailbox_in_hi_seen = false;
        } else {
            g_mailbox_in_value = (u32)(value & 0xFFFFu);
        }
        // Reply with the command echoed back (status/ack style). The driver
        // only requires a well-formed message; values are derived from this.
        g_mailbox_out_value = g_mailbox_in_value;
        g_mailbox_out_valid = true;
        if (g_mmio_log)
            fprintf(stderr, "[dsp] mailbox in 0x%08X -> reply 0x%08X\n",
                    g_mailbox_in_value, g_mailbox_out_value);
        return;
    }
}
