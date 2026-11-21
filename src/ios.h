#ifndef COLORSRECOMP_IOS_H
#define COLORSRECOMP_IOS_H

#include "core/cpu.h"
#include "core/types.h"

#include <stdbool.h>

// IOS IPC registers (0xCD000000..0xCD0000FF) and the minimal IOS device
// service behind them.
//
// The Starlet (ARM) OS handles requests; we front it with a register file and a
// per-device dispatcher: /dev/stm, /dev/fs, /dev/es, /dev/di (disc reads) and a
// present-but-silent /dev/usb. Replies are delivered through the request
// context (see ios_dispatch in ios.c).
#define IPC_BASE 0xCD000000u
#define IPC_SIZE 0x100u

bool ios_ipc_contains(u32 addr);u64  ios_ipc_read(CPUState* cpu, u32 off, u8 size);
void ios_ipc_write(CPUState* cpu, u32 off, u8 size, u64 val);

// Deliver any deferred (one-USB-frame) completions whose frame has elapsed.
void ios_poll_delayed(CPUState* cpu);

// 32-byte disc boot header ("SNCE8P..."), returned by /dev/di ReadDiskID.
void ios_set_disc_id(const u8 id[32]);

// Read the COLORS_TRACE_IO switch (per-request trace).
void ios_init(void);

// Total DI payload read from the disc (bytes and command count).
void ios_di_stats(u64* bytes, u32* reads);

#endif
