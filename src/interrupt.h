#ifndef COLORSRECOMP_INTERRUPT_H
#define COLORSRECOMP_INTERRUPT_H

#include "core/cpu.h"

// VI vertical-retrace external interrupt delivery, using GXRuntime's
// device models (PI cause/mask + VI registers + VI retrace clock). When a
// retrace is due and external interrupts are enabled/unmasked, the host saves
// the guest state into the OS current context and jumps to the recompiled
// __OSDispatchInterrupt.

void interrupt_init(void);

// Lifetime counters (retraces queued, external interrupts delivered,
// decrementer interrupts delivered). Cheap; used by the end-of-run report.
void interrupt_stats(u64* retraces, u64* external, u64* decrementer);

// MMIO delegation for the PI/VI/PE interrupt registers.
bool interrupt_mmio_contains(u32 ea);
u64  interrupt_mmio_read(u32 ea, u8 size);
void interrupt_mmio_write(u32 ea, u8 size, u64 value);
// Call once per dispatch (or batched). Advances the VI clock and delivers the
// external interrupt when a retrace is due.
void interrupt_poll(CPUState* cpu);

// ---------------------------------------------------------------------------
// Idle fast-forward.
//
// The Wii OS idle thread spins in a loop that polls an OSThreadQueue and calls
// the scheduler. When that queue is empty and no thread is ready to dispatch,
// the guest provably has nothing to do until the next interrupt, so the host can
// advance the timebase straight to it instead of executing ~10^6 idle blocks per
// video frame (which is >90% of all executed blocks in this title).
//
// `interrupt_idle_skip_ticks` returns the guest timebase ticks to skip, or 0
// when the guest is not idling. Callers advance the timebase/decrementer and
// then use `interrupt_force_poll` so the pending retrace is delivered at once.
// ---------------------------------------------------------------------------
u32 interrupt_idle_skip_ticks(CPUState* cpu);
void interrupt_force_poll(CPUState* cpu);
void interrupt_idle_stats(u64* skips, u64* ticks);

// COLORS_STOP_RETRACE=N: request a clean stop once N retraces have been
// delivered, so two configurations can be compared at the same guest frame.
bool interrupt_stop_requested(void);

// Assert the IOS IPC interrupt (PI cause bit 0x4000) so the guest's IPC handler
// runs and completes a pending request.
void interrupt_assert_ipc(void);

// Drop the IPC interrupt (the guest acked the reply).
void interrupt_clear_ipc(void);

// ---------------------------------------------------------------------------
// Audio DMA (AID) interrupt.
//
// The DSP's audio-DMA completion (and the AI sample interrupt) reaches the PPC
// as PI cause bit 5 (AI). The SDK's handler at 0x80358330 acks it by writing
// DSP DSCR bit 3 (0xCC00500A) and then runs the AX frame callback, which is what
// releases the game's audio/DMA wait. We assert it once per video frame while
// it is not already pending, and clear it when the guest writes DSCR bit 3.
// ---------------------------------------------------------------------------
void interrupt_assert_audio_dma(CPUState* cpu);
void interrupt_clear_audio_dma(void);

// Commit a "GX draw finished" (PE_FINISH) interrupt: the graphics command
// processor reaching the end of a display list. The guest's handler at
// 0x8036A290 sets the draw-done flag, wakes the waiting thread and acks through
// the PE interrupt status register (0xCC00100A). With no real GPU we complete
// one list per frame, which is the "GPU always catches up" model.
void interrupt_commit_draw_done(CPUState* cpu);

// ---------------------------------------------------------------------------
// Decrementer (SPR 22).
//
// The guest OS arms its alarm/timer queue by writing the decrementer
// (`__OSSetDecrementer`, 0x80338830) and installs `__OSAlarmHandler`
// (0x8033A3B0) as the decrementer exception handler. The decrementer counts
// down at the timebase rate; an exception is latched on the 0->1 transition of
// its MSB and is delivered while MSR[EE] is set.
// ---------------------------------------------------------------------------

// Guest wrote DEC (mtspr 22). Value is in timebase ticks.
void interrupt_set_decrementer(u32 value);

// Current DEC register value (for mfspr 22).
u32 interrupt_get_decrementer(void);

// Advance DEC by `ticks` timebase ticks and deliver the decrementer exception
// if it latched and interrupts are enabled. Call once per dispatch.
void interrupt_advance_decrementer(CPUState* cpu, u32 ticks);

#endif
