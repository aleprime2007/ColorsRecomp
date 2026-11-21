#ifndef COLORSRECOMP_DSP_H
#define COLORSRECOMP_DSP_H

#include "core/types.h"

#include <stdbool.h>

// DSP / ARAM MMIO (0xCC005000-0xCC00503F).
//
// Minimal model for the Wii audio init handshake: DSP reset -> mailbox
// handshake -> ARAM DMA, polling the DSP control/status register (DSCR,
// 0xCC00500A):
//   DSCR bit 0 (0x01): DSP reset    -- hardware clears it once reset completes
//   DSCR bit 5 (0x20): ARAM DMA done -- set when an ARAM DMA is triggered
//   mailbox out HI  (0xCC005004): bit 15 set => a message is available
#define DSP_BASE 0xCC005000u
#define DSP_SIZE 0x40u

// DSP DSCR bits we model (Dolphin DSP.h UDSPControl).
#define DSP_DSCR_RESET 0x0001u
#define DSP_DSCR_INT 0x0004u
#define DSP_DSCR_AID 0x0008u
#define DSP_DSCR_ARAM_INT 0x0010u

bool dsp_mmio_contains(u32 addr);
u64  dsp_mmio_read(u32 off, u8 size);
void dsp_mmio_write(u32 off, u64 value, u8 size);

// Raise the audio-DMA (AID) interrupt status bit in DSCR. The PPC interrupt is
// delivered as PI cause DSP; the guest handler acks by writing DSCR bit 3.
void dsp_raise_aid(void);

// Register snapshot for the host diagnostics (the loader/audio probe prints
// the ARAM DMA address and current-pointer halves).
typedef struct {
    u16 aram_dma_ar_lo, aram_dma_ar_hi;  // 0x30 / 0x32 (ARAM side)
    u16 dma_ptr_lo, dma_ptr_hi;          // 0x38 / 0x3A (current pointer)
} DspDmaSnapshot;

void dsp_dma_snapshot(DspDmaSnapshot* out);

#endif
