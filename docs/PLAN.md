# Static Recompilation of `game/sys/main.dol` (Sonic Colors, Wii)

## Goal

Build a **fully runnable static recompilation** of the Nintendo Wii executable
`game/sys/main.dol` (Sonic Colors, game ID `SNCE8P`), using **rea** + **ghidra**
for analysis and a dedicated **PowerPC→C recompiler** for code generation.

## Input facts

| Property | Value |
|----------|-------|
| File | `game/sys/main.dol` (8,182,272 bytes) |
| Format | Nintendo Wii DOL |
| Game ID | `SNCE8P` (Sonic Colors, PAL) |
| Text sections | `0x80004000` (10 KB), `0x800113c0` (~7.34 MB) |
| Data sections | 8 sections (MEM1 + MEM2) |
| BSS | `0x807c6500` (1.27 MB) |
| Entry point | `0x80004050` |
| Code size | ~7.35 MB ≈ ~1.8M PowerPC instructions |

## Toolchain

| Role | Tool | Notes |
|------|------|-------|
| Analysis / decompilation | `rea` (`--target-format gamecube-dol`) + `ghidra` (12.2 headless) | Function map, symbols, strings, reference pseudocode. |
| Static recompilation (PPC→C) | `DolRecomp` (`aharonahdoot/DolRecomp`) | GC/Wii DOL→C recompiler; 236 Gekko/Broadway opcodes incl. paired-single. |
| Runtime (Wii hardware) | `GXRuntime` (`tools/GXRuntime`, unmodified) | CPU, guest memory, devices, retail GX frontend + gxcore. DolRecomp is CPU-only; a runtime is mandatory for "fully runnable." |

Alternatives: NWiiRecomp, GeckoRecomp, RecompCore.

## Phases

### Phase 0 — Toolchain bootstrap
- Clone + build DolRecomp (`cmake -S . -B build && cmake --build build --config Release -j`).
- Clone DolRuntime; inspect how it integrates DolRecomp-generated split-C.
- Handle rea/Ghidra version drift (12.2 vs rea's verified 12.1.x).

### Phase 1 — Whole-binary analysis (rea + ghidra)
- `rea analyze game/sys/main.dol --target-format gamecube-dol`
- `rea decompile-all game/sys/main.dol --target-format gamecube-dol --output <dir>`
- Direct `analyzeHeadless` pass for symbol recovery, xrefs, and Wii OS/HW call sites
  (HAL: GX/DSP/EXI/SI/IPC).

### Phase 2 — Static recompilation (DolRecomp)
- `dolrecomp -j14 game/sys/main.dol SNCE8P build` → `SNCE8P_generated/` split-C.
- Validate against the rea/ghidra reference.

### Phase 3 — Runtime integration (the big lift)
- Wire generated C into DolRuntime: Broadway CPU state, MEM1/MEM2 map, GX graphics,
  DSP audio, EXI/SI controllers, IOS HLE.
- Provide game data in `game/files/` (`sonic2010_0.cpk` ~1.18 GB, SFD movies, etc.).

### Phase 4 — Build → boot → iterate
- Compile + link a native x86_64 executable; reach `__start` → entry; progressively
  fix recompilation issues and HLE/hardware gaps until it plays.

## Risks / notes

- Large multi-phase effort; "fully playable" is the end state, not the first milestone.
- DolRecomp does **not** handle self-modifying code (SMC); affected functions need
  manual patches.
- The runtime (graphics/audio/IOS HLE) is most of the "actually plays the game" work.
- Near-term concrete win: full analysis + reference decompilation + a DolRecomp
  C tree that compiles and boots to entry.

## Decisions (defaults)

1. Recompiler = DolRecomp + DolRuntime.
2. Keep Ghidra 12.2 as-is; installed a side-by-side **Ghidra 12.1.4** for rea.

## Host source layout (`src/`)

The host was split out of the original monolithic `main.c` (1561 lines). Each
module owns one hardware block or one concern, and the split is behaviour
neutral: the default run still stops at the same block/pc with identical
diagnostics.

| File | Responsibility |
|------|----------------|
| `main.c` | CLI, boot sequence, run loop, timing advance |
| `log.c/h` | `g_mmio_log` switch (`--mmio-log`) shared by all devices |
| `mem.c/h` | MEM1 backed accessors + 64 MiB MEM2, physical mirrors |
| `mmio.c/h` | external bus dispatch order + instruction fallback (SPR/cache) |
| `dsp.c/h` | DSP mailbox, DSCR, ARAM DMA (`0xCC005000`) |
| `ai.c/h` | AI registers + sample counter (`0xCD006C00`) |
| `pi.c/h` | PI/GX FIFO window (`0xCC00300C..`) + GXInit seeding |
| `ios.c/h` | IPC registers, fd table, `/dev/*` dispatch, deferred USB replies |
| `boot_env.c/h` | OS globals, disc ID, arena bounds, FST placement |
| `dbg.c/h` | PC history, per-block watches, temp HLE, final state report |
| `interrupt.c/h` | VI retrace, IPC interrupt, decrementer model |
| `dvd.c/h` | DI/EXI register translation to DolRuntime models |
| `disc.c/h` | virtual disc image built from `game/files` |

Bus dispatch order in `mmio.c` is significant: MEM1 → MEM2 → DSP → IPC → **PI
FIFO → interrupt model** → DI/EXI → AI. The PI FIFO *must* precede
`interrupt_mmio_*`, because DolRuntime claims the whole `0xCC003000` block and
would otherwise answer `0xCC003014` with zero (that shadowing caused the
runaway `memcpy`).

## Progress log

- **DolRecomp** built (`tools/DolRecomp/build/dolrecomp`); full static recompilation
  generated with `--cpu broadway` → `recompiled/generated/` (450 chunk files,
  ~1.84M instructions, **0 unknown opcodes**).
- **Runtime pinned to `DolRuntime` @ tag `v0.1.0`** (`tools/DolRuntime`). The HEAD
  commit drifted from DolRecomp HEAD (`ppc_psq_load` bool→void); `v0.1.0` is the
  compatible pin (bool `ppc_psq_load`, `external_pointer`+`downcount` tail ABI).
  Core builds clean and `dolruntime_tests` passes.
- **rea** hard-gates on Ghidra 12.1.x (not 12.2); installed `ghidra_12.1.4_PUBLIC`
  under `tools/` and set `GHIDRA_INSTALL_DIR` to point rea at it (`rea doctor`
  healthy). rea's `analyze`/`decompile` on `--target-format gamecube-dol` fail at
  output serialization (schema `format` discriminator missing `gamecube-dol` in the
  bundled CLI runtime) — rea bug; Ghidra loads/analyzes the DOL correctly.
- **Game client** (`src/main.c` + `src/interrupt.c` + top-level `CMakeLists.txt`)
  builds the full recompilation + DolRuntime core. `build/colorsrecomp` **boots and
  runs**: entry `0x80004050` → CRT init → OS init → DSP/audio init → a real-mode
  context switch, 10s of millions of blocks, no CPU exceptions, after fixes:
  1. MEM2 (64 MiB @ 0x90000000) via `external_read/write`.
  2. Timebase advancement (persistent cycle→timebase accumulator).
  3. Eager FP (`ppc_lazy_fp_set_enabled(false)`).
  4. DSP/ARAM MMIO model (0xCC005000 block: reset/mailbox/ARAM-DMA-done).
  5. Physical→virtual address translation (real-mode context-switch `rfi`).
- **Ghidra analysis** (direct `analyzeHeadless` + rea's `ReaGhidraPrepareDol.java`
  pre-script + `tools/ghidra/DumpDol.java`) produced `docs/ghidra-out/`:
  **29,016 functions** + **10,516 strings** + per-function decompilation.
  `__OSDispatchInterrupt = 0x803402E0` (verified), `OSGetTick = 0x803442D0`.
- **VI retrace interrupt** delivered in `src/interrupt.c` (DolRuntime PI/VI + VI
  clock → save OSContext → jump to `__OSDispatchInterrupt`).
- **Where it stands now**: the alarm/timer layer is *fixed*, the IOS IPC
  handshake works end to end, and the disc is enumerated successfully. The game
  now boots through the OS, creates **16 threads**, runs its own code
  (`0x806Exxxx`) and blocks in its own message/font initialisation (a chain of
  async-init busy-waits). Earlier it died in the SDK disc-error splash; the
  breakthrough was the DI reply status contract (`result` bit 0 = success).

### Fixed this round (all verified)

1. **Decrementer / OS alarm timer.** `mtspr 22` (`__OSSetDecrementer`,
   `0x80338830`) is now intercepted (instruction fallback) and modelled in
   `src/interrupt.c` as a 40.5 MHz down-counter with the architectural
   edge-triggered MSB latch. When it latches and `MSR[EE]` is set, the host
   saves the interrupted `OSContext` and enters the guest's own
   `__OSAlarmHandler` body at `0x8033A3B0` (handler installed by
   `__OSInitAlarm` `0x80339ED0` for exception type 8). Alarm handler returns via
   `OSLoadContext`. This is what finally released the alarm queue spin.
2. **Alarm queue semantics (why it used to deadlock).** Queue at `0x808F3910`
   is `{head, tail}`; node = `+0 handler, +8/+12 fire (OSTime), +16 prev,
   +20 next, +24/+28 period, +32/+36 start`. `__OSSetAlarm` writes the node's
   fire time *before* walking the sorted list, so re-arming an alarm that is
   still queued walks onto itself and self-links (`next == self`) — the old
   infinite loop. With the DEC exception working the handler unlinks fired
   alarms, so re-arming is safe.
3. **Boot arena globals.** `OSGetArenaHi()` returned 0 because the apploader
   fields at `0x8000310C/0x80003110` were never populated (we only set
   `0x8000311C`). The DSP/AX init switches to a stack at `OSGetArenaHi()-8`,
   i.e. `0xFFFFFFF8`, and crashed. `src/main.c` now sets `0x8000310C` =
   ArenaLo and `0x80003110` = `0x817FEC60`.
4. **DSP mailbox consume.** `0xCC005004` bit 15 = "message available"; reading
   the low half (`0xCC005006`) consumes it (libogc/SDK `DSPReadMailFromDSP`
   order). Without the consume, `__DSPInit` (`0x8033AE00`) and
   `FUN_8033AFB0` spin waiting for an empty mailbox.
5. **IOS IPC status handshake.** The guest's handler (`0x80393900`) implements:
   - `(status & 0x14) == 0x14` -> a reply is ready; run the reply processor
     (`0x803936A0`, dispatches on `ctx+8`: 3 = read, 6 = ioctl, else generic,
     then calls the completion callback at `ctx+32`).
   - `(status & 0x22) == 0x22` -> ack + send the next queued request.
   The starlet-side bits are now modelled (`0x14` on request, `0x22` once the
   guest wrote `0x08`, cleared when it writes `0x02`), and the IPC interrupt is
   deasserted on the ack. Result: no more interrupt storm (was thousands of
   IPC status writes, now 12 for the whole boot).
6. **Disc ID low memory.** `0x80000000..0x8000001F` must contain the disc boot
   header (`game/disc/header.bin`, "SNCE8P..."); the DVD layer builds its
   `DVDDiskID` from it. Now populated at startup (necessary, not sufficient).
7. **IOS device service skeleton.** `src/main.c` now has real file descriptors
   (`0x100 + slot`, path tracked per fd) and a per-device dispatch
   (`ios_dispatch`), instead of replying `fd = 0x100` to every open (all
   devices used to share one fd). Reply fields: `+4` = result, `+8` = reply
   type (= request type; 3 = read, 6 = ioctl).

### IOS request contract (verified from requests)

`IPC_CMD` (`0xCD000000`) = physical address of the request context
(`0x81600020`, `0x81600080`, `0x816000E0` — three client slots).

```
+0  request type: 1 open, 2 close, 3 read, 6 ioctl, 7 write
+4  result (s32, filled by the Starlet)
+8  fd (requests) / reply type (replies)
+12 arg: open -> path ptr; ioctl -> command; read -> buffer ptr
+16 open: 0; ioctl: in buf; read: length
+20 ioctl: in len
+24 ioctl: out buf (physical; guest adds 0x80000000)
+28 ioctl: out len
+32 completion callback (guest runs it after the reply is processed)
+36 callback argument
```

### Devices the game opens during boot

`/dev/stm/immediate`, `/dev/stm/eventhook`, `/dev/fs`, `/dev/es`, `/dev/di`,
then NAND files `/shared2/sys/SYSCONF`,
`/title/00000001/00000002/data/setting.txt`, and (on the error path)
`/shared2/test2/dvderror.dat`.

`/dev/di` ioctls seen: `0x86` ReadDiskID, `0x12` (drive inquiry), `0x95`
OpenPartition, `0x96` ClosePartition, `0xE3`.
`/dev/stm/immediate` ioctl `0x1000`, `0x5001`.

### Disc data (ready to serve)

`game/sys/fst.bin` parses cleanly (verified against `game/files`):

- 45 entries at `0x0C` (41 files), name table at `0x228`.
- directory: `type=1, name_off[3], parent, next`.
- file: `type=0, name_off[3], offset = word*4, length = word`.
- every length matches the extracted file byte-for-byte
  (`movie/ev110.sfd` = 156,229,632 at `0x1C5F4D48`, ... `sonic2010_0.cpk` =
  1,176,776,029), max end `0xFF7BFFDD` (single-layer disc).
- `game/sys/boot.bin`: `+0x424` = FST offset>>2 (`0x203640` -> `0x80D900`),
  `+0x428` = FST size>>2 (`0x107` -> 1052 = fst.bin size).
- Synthesizable image map: `boot.bin`@0, `bi2.bin`@0x440,
  `apploader.img`@0x2440, `main.dol`@0x3FF000, `fst.bin`@0x80D900, then all
  FST entries at their own offsets.

### Disc service + apploader emulation (this round)

- **`src/disc.c`** reconstructs the disc: `fst.bin` parse (45 entries at
  `0x0C`, file `offset = word*4`, `length = word`) mapped to the extracted file
  with the same length (all 41 FST files match uniquely), plus
  `boot.bin`@0, `bi2.bin`@0x440, `apploader.img`@0x2440 and `fst.bin`@0x80D900
  (from `boot.bin+0x424`). `disc_read(offset,len,dst)` serves any DI read.
- **Apploader emulation**: the real apploader loads the FST into MEM1 and
  publishes its address at `0x80000038` / size at `0x8000003C`; OSInit then
  shrinks ArenaHi to the FST start. We now place `fst.bin` at `0x817FE840` and
  set those globals (plus `0x80003110`), so the game's DVD layer can resolve
  paths to offsets. Also `/dev/di` `0x86` (ReadDiskID) writes the 32-byte boot
  header into the caller's command block, and `0x70/0x71` reads are served from
  the disc map into the reply's out buffer.

### DVD layer (Nintendo SDK) — decoded, needs exact status contract

Traced with `COLORS_TRACE_HALT=1` (prints first hits of key PCs + every
state-machine op). Key addresses:

- `0x8034CC40` — `__DVDStateMachine`; dispatches on `[block+8]` (op code, 0..0x2A,
  jump table `0x803927C4`).
- `0x8034C920` — "advance to next command" helper; publishes the current op at
  `[r13-23132]` and the current command block at `[r13-23120]`.
- Command block layout: `+8` op/state, `+16/+20/+24` args, `+32`,
  `+40` callback. Command blocks come from a 4-entry pool at `[r13-23052]`
  (32-byte slots, first byte = ioctl opcode).
- IOS request slots: `0x807E0860 + i*32` — `+0` handler, `+4` busy,
  `+8` busy byte, `+12` magic `0xFEEBDAED`, `+16` partition-data offset,
  `+20` result2, `+24/+28` out buffer/len.
- Send helpers: `0x80351F20` = `0x86` ReadDiskID (no out buffer; ID is DMA'd
  into the 32-byte command block, `ctx[0] = arg` which is 0),
  `0x80350E00` = `0x12` inquiry (out = 32 bytes), `0x80351C10` = `0x95`
  OpenPartition (out = 32 bytes, `out[0]` becomes `ctx+16`),
  `0x80351DC4` = `0x96` ClosePartition, `0x80351740` = `0x71` read
  (`in = {0x71, length, offset}`, data DMA'd into the out buffer),
  `0x80350C60` = `0xE3` (ES partition open, takes a completion handler),
  `0x8034F490` = `__DVDLowOpenPartition(offset, cb)`.
- `0x8034F9F0` = DI completion dispatcher: calls `ctx[0]` handler with
  `(result, ctx)`; our IPC reply `+4` is that `result`.
- `0x8034A0C0` = **fatal disc-error handler**: if the fatal error handler
  (`[r13-23096]`, installed at DVD init `0x80349D78` → `0x8034F5F0`) is present,
  it arms a 1-tick alarm `0x8034A0B0` → `0x8034F740` → `0x8034F5F0` →
  localized error splash (`FUN_8033E2A0`/`FUN_8033E490`: 640x480 VI setup,
  0x96000 framebuffer, texture from `/shared2/test2/dvderror.dat`,
  `OSDisableInterrupts` + `PPCHalt`).
- Op `0x0E` = partition open (`0x8034DCC0` sets `block+8 = 14`); the sentinel
  offset `0x01234567` is a legitimate "open the current/ES partition" marker.
- Observed boot sequence: `0x86` → `0x12` → `0xE3`/`0x95` → fatal path.
  The state machine takes the fatal branch right after the `0x86` completion,
  so the **ReadDiskID result/status contract** (what the SDK expects back in
  the command block / reply result) is the immediate thing to match. The
  `0x0E/0x10/0x20/0x23/0x2A` op values and the `[r13-23132]` checks in
  `FUN_8034D1F0` / `0x8034DA74` are the switch points.

### Breakthrough: DI status bit 0 (this round)

The DVD state machine took its fatal branch because **every DI ioctl reply
returned result 0**. The state machine's check at `0x8034D4D8`
(`rlwinm. r0, r31, 0, 31, 31` -> `beq 0x8034DA74`) treats an **odd result (bit 0
set)** as "command completed"; 0 means "no disc / retry" and arms the fatal
error splash. Returning `1` for `/dev/di` ioctls unblocked the entire boot.

With that one change the game:

- passes the disc-ID / inquiry / partition-open state machine,
- creates **16 OS threads** (loader / renderer / audio / pads ...),
- runs its own game code (`0x806Exxxx`) instead of the error splash,
- initialises IOS devices: `/dev/stm`, `/dev/fs`, `/dev/es`, `/dev/di`,
  `/shared2/sys/SYSCONF`, `setting.txt`, `play_rec.dat`, `/dev/usb/oh1/57e/305`.

Other fixes validated in this round:

- `/dev/usb*` opens now fail with `-6` (ENOENT) — without that the game polls a
  USB keyboard forever (write `0x43` to fd 0x105 in a loop).
- IOS write requests carry buffer/len at `+24`/`+28` (reads at `+12`/`+16`);
  the write reply now returns the written length.

### DVD state machine tables (resolved from the DOL)

- Op -> handler map (`head` table) at `0x807927A4`, resume table at
  `0x807926F8` (absolute code addresses, DOL data section `0x80712bc0`).
- Op `0x0E` -> `0x8034CEE8`: send `0x86` ReadDiskID then `0x12` inquiry
  (this is the disc-identification step).
- Op `0x0E` normal completion -> `__DVDLowOpenPartition(0x01234567, cb)`; the
  sentinel offset is the legitimate "open current/ES partition" marker.
- Ops `0x10`/`0x20`/`0x21`/`0x23`/`0x2A` and `[r13-23132]` are the switch points
  in `FUN_8034D1F0` / `0x8034DA74`.
- Helper `0x80350C60` = "send DI ioctl `0xE3` with a completion handler" (ES
  partition open). `0x8034F9F0` = DI completion dispatcher
  (`handler(result, ctx)`), our IPC reply `+4` is `result`.

### Current spin: message/font init

The game now blocks in its own message/font initialisation:

- `0x806E6C54` spins while `[r13-25224] != 0` (busy-wait; the "wait" helper
  `0x806E5470` is an empty `blr` stub).
- `FUN_806E6290` (state block `0x808E5480`) memsets the block, sets state 5,
  then (on success) sets state 4 and `[r13-25224] = 1`; `FUN_806E64A0` is
  supposed to do the work and clear the flag.
- A **second** call to `FUN_806E6290` bail-outs before reaching the state-4 step
  (leaving the stale flag set). Its checks are, in order:
  1. `0x80393AC0` IPC send (returns -22 if the client queue/heap is full),
  2. `0x80711200` (needs `[r13-25064]`, a 16 KiB buffer from `0x803950D0`,
     not `-1`),
  3. `0x807114B0` (allocate/init a message window, `0x80395400(handle,128,32)`),
  4. `0x806E48B0(w,h,1,0)` twice (sprite allocator: 660x45 and 1800x30;
     returns `0xFF` on failure).
  This is the next thing to trace (likely our NAND/file-service replies leave
  the handle at `-1` or the IPC client queue wedged).

### Async-init wait chains (diagnosed)

`COLORS_CLEAR_FONTFLAG=1` (host hack: zeroes `[r13-25224]` at `0x806E6C54`)
moves the game one step further, straight into the *next* wait loop:

```
0x806E6CC0  li r3, 2000 ; bl 0x806E5470 (empty stub)
            bl 0x806E9D30  -> 0x806EF714: returns (byte@0x808E9866 == 5)
            if (r3 == 0) loop
```

So the game is walking a chain of "start async init, busy-wait for the state
byte to advance". The state bytes live in per-subsystem blocks
(`0x808E5480` message/font, `0x808E9866` the next stage) and are advanced by
completion callbacks. The three `blr` stubs at `0x806E5450/60/70` are the
idle-delay hooks (`0x806E5470` is called with the delay in r3).

That means the real fix is not the individual flags but the **async completion
path**: the game's init steps kick off IOS/DVD work and rely on the
completion callbacks (IPC `ctx+32`) to advance the state. Next session should
trace which request each init step issues and whether our reply's result /
callback invocation matches (the DI `result` bit-0 fix already showed how
sensitive this contract is).

### Resource pools behind the stalls (this round)

- `0x803950D0(ptr32aligned, size)` — allocates from a **static 8-slot DMA
  buffer pool** at `0x807FE730` (16-byte stride; slot word 0 = buffer pointer).
  Returns the slot index, or a negative error (-4 alignment/slot, -22 full).
- `0x80395400(slot, size)` — sub-allocates inside a pool slot.
- The first stall's state block (`0x808E5480`, message/font init) is observed as
  `[+0] = 0xFFFFFFFA` (the -6 ENOENT we returned for `/dev/usb/oh1/57e/305`!),
  `[+41] = 5` (state), `[+28] = [+29] = 0`. The second `FUN_806E6290` therefore
  bails before the state-4 step; the stale wait flag `[r13-25224]` stays set.
- `[r13-25064]` is the DMA pool handle for the 16 KiB buffer; `0x80711200` fails
  with -22 if it is -1 or the pool is full. At the stall the pool has 6/8 slots
  free and the handle reads 2, so the pool is not the immediate cause.
- The failed USB open leaving `-6` in the message block was the trigger: with
  `/dev/usb/*` opens **succeeding** (a silent device; every transfer still
  returns `-6`), the game skips the stalled font wait and enters the real
  message/font init driving it from IPC completions. Verified by the guest
  backchain at the new stop:

  ```
  #0 0x80394C7C  #1 0x80711CD4  #2 0x80711ED4  #3 0x806E608C
  #4 0x80711428  #5 0x8039388C (IPC xact callback)  #6 0x8039393C (IPC IRQ)
  #7 0x80340554 (__OSDispatchInterrupt)  #8 0x806E6CCC (init wait loop)
  ```
  i.e. the reply runs the message-window allocator callback while the init loop
  waits -- exactly the intended async handshake.
- The game then proceeds into the message/font asset allocator (`0x80711A90`
  calls `0x80395400(handle, 96/32, 32)` and smaller blocks from the DMA slot),
  plus `memset`/`DCStore`/`DCInvalidate` cycles on the allocated buffers.
- The game also polls `/dev/usb/oh1/57e/305` with 2-byte write commands
  (`arg 0x1`/`0x2`, callback `0x80711310`) as a background device probe.
  Modelling the USB node as **present but silent** (open succeeds, transfers
  complete with 0 bytes) works better than ENOENT: the init keeps making
  progress instead of bailing. It still re-polls forever, so a later fix should
  give the probe a definitive "no such endpoint" answer *after* the open, or
  gate the poll rate.

### USB completion timing + the audio/AX wait (this round)

- **USB transfers are now deferred by one USB frame** (1 ms = 40 500 timebase
  ticks, `ios_defer_reply`/`ios_poll_delayed` in `src/main.c`). The game's USB
  driver was polling `/dev/usb/oh1/57e/305` ~44 000 times per 30 M blocks; with
  frame-accurate completion that dropped to **150** (300x) and the scheduler
  gets the time instead. The remaining USB traffic is a genuine background
  device poll.
- The game now settles in the **sound/AX init state machine**:
  - block base `0x808E9218` (init `FUN_806EF480`, per-subsystem vtable at
    `0x807588F0`), state byte `+1614` = `0x808E9866`, companion `+1615`.
  - `FUN_806EF714` returns 1 only when `[0x808E9866] == 5`; the init loop at
    `0x806E6CC0/0x806E6CC8` busy-waits for it.
  - state writers: `0x806EF4FC/62C/6D4/87C/8A4/8B4/9CC/A30/B5C/B68/CF8/D08`
    (values 0/2/3/4 observed; 5 is the "ready" transition). The init writes
    voice/buffer parameters (`sth 18/2048/0` at block `+5792..+5798`), i.e.
    this is the **audio driver / AX initialisation** waiting for the DSP.
- OS interrupt-handler table (`0x80003040`) at the stop:

  ```
  [0..4] 0x80340950   [9..16] 0x803888F0/0x803889B0/0x80388BD0 (EXI)
  [17] 0x80367CA0 [18] 0x8036A1B0 [19] 0x8036A290 (SI)
  [20] 0x80389C40     [24] 0x80352240 (DI)     [27] 0x80393900 (IPC)
  ```

  We deliver VI (0x10000) and IPC (0x4000) only. The **AI/DSP interrupt**
  (handler `[20]`) is never raised, which is consistent with the audio init
  never reaching state 5.

### Audio bring-up: AI + DSP mailbox (this round)

- **AI (Audio Interface) registers modelled** at `0xCD006C00`
  (`src/main.c`: `ai_mmio_read/write`). `AI_SAMPLE_COUNT` (`+0x08`) is derived
  from the timebase at the streaming rate (48 kHz, or 32 kHz when
  `AI_CONTROL` bit 6 is set). This fixed the driver's streaming handshake at
  `0x803584B8` (`while ([0xCD006C08] & 0x7FFFFFFF) == r3`) which previously spun
  forever because the counter never changed.
- **DSP mailbox responder**: the PPC->DSP mailbox (`0xCC005000/02`) now produces
  a reply in the mailbox-out (`0xCC005004/06`, bit 15 = message available, data
  in the low 15 bits of HI + LO). Reading LO consumes the message. A boot
  message is posted when the DSP is released from reset (DSCR bit 0 goes from
  set to clear, or bit 0x800 is written). With that, the SDK's DSP driver
  uploads the AX ucode: mailbox commands `0x80F30000`, `0x80F3D001`,
  `0x00000000`, `0x00000010` (ARAM/DMA addresses) now flow.
- Remaining audio gap: after `__AXDSPTaskStart` (`0x803660B0`) the driver waits
  at `0x8035A3E8` on the AX busy flag `[r13-22568]`. It is set by
  `0x8035A290` (`li r0,1; stw r0,-22568(r13)`) and cleared only by the AX
  init task (`0x8035A320`, `li r12,0; stw r12,-22568(r13)`). A real DSP
  completion interrupt is what re-runs the init; without DSP ucode emulation
  that transition needs an HLE shortcut.
- `COLORS_FORCE_AUDIO_STATE=1` (diagnostic) forces the earlier audio state byte
  (`0x808E9866 = 5`) and lets the boot continue past audio.

### First real disc read served (milestone)

With audio forced, the game left the audio init and asked for disc data:

```
[ios] di read off=0x2E55F4A0 len=0x800 -> game/files/movie/ev150.sfd
```

The guest's offset is **exactly** the FST entry offset (no partition-base
adjustment needed), and `src/disc.c` resolved it to the correct host file with
zero unmapped reads. The disc service, FST placement and DI ioctl contract are
therefore validated end to end against the real game.

### Audio deep-dive: AI, DSP mailbox, sound thread (this round)

- **AI registers modelled** (`0xCD006C00`): `AI_SAMPLE_COUNT` (+8) is derived from
  the timebase at the streaming rate, which fixed the streaming handshake at
  `0x803584B8`.
- **DSP mailbox responder**: PPC->DSP writes (`0xCC005000/02`) produce a reply
  (`0xCC005004/06`, bit 15 = available); reading LO consumes it; a boot message
  is posted when the DSP leaves reset. The AX ucode upload commands now flow
  (`0x80F30000`, `0x80F3D001`, ...).
- **DSP task-completion HLE**: `__AXDSPTaskStart(PB)` builds an AXPB at
  `0x807E4D40` whose `+40` callback is `0x8035A290`
  (`li r0,1; stw r0,-22568(r13)`) and waits at `0x8035A3E8` for that flag. With
  no DSP ucode we perform the callback's effect (`COLORS_NO_DSP_TASK_HLE=1`
  disables it).
- **Sound driver thread**: `0x806E6BF0` spawns `0x806E6DA0` on a private stack
  (`0x808E8CC0`) via `__OSSwitchStack`. Its message loop dispatches on
  `[msg+20]`; type **1** enters the sound state machine `0x806EF820`, whose
  state byte is `0x808E9866` (+1614 of block `0x808E9218`). The audio-init wait
  (`0x806E6CC8`) needs that byte to reach **5**, but the module never receives a
  type-1 message in our run (state stays 0), so the state machine never runs.
  `COLORS_FORCE_AUDIO_STATE=1` sets it for diagnostics.
- **DSP interrupt handler found**: during AI streaming init the game registers
  `__OSSetInterruptHandler(5, 0x80358330)` at `0x803582F8` (PI cause DSP,
  `0x40` -> source index 5) and sets `AI_CONTROL |= 0x40`. Our handler-table
  dump shows no entry at 5 because that init is never reached in the default
  run.
- **NAND file opens now return ENOENT** (`-6`) instead of zero-filled data, so
  parsers don't build structures from zeroes.
- With `COLORS_FORCE_AUDIO_STATE=1` (+ DSP task HLE, default on) the game gets
  past the AX task wait and reaches `0x80004434` -- a `memcpy` loop whose
  arguments are wild (`src ~0x5810xxxx`, `len 0xFF6F2D80`), i.e. a corrupt
  structure is being copied. The first real disc read
  (`movie/ev150.sfd`, 0x800 bytes) is served correctly before that.

### Sound module protocol decoded (this round)

The "sound" block `0x808E9218` is a sequencer/voice driver with a command
dispatcher. Root of the audio stall:

- **Command dispatcher** `FUN_806F7E6C` (called from `0x806F79F8`) switches on
  the command id:
  `0x1001`->`0x806EFD44`, `0x1003`->`0x806EFC7C`, **`0x1005`->`0x806EFE00`**,
  `0x1009`->`0x806F03B4`, `0x1100`/`0x1200`/`0x1600`/`0x1900` -> other handlers.
- **`0x806EFE00`** is the *init* handler: it sets the state byte to **5**
  (`0x806EFE44: li r0,5; stb r0,1614(r31)`), copies 9 config bytes from the
  message payload into `+1600..+1607`, stores `+1620 = 24`, resets `+1420`, and
  branches to `0x806F02E4` when `payload[0] != 0`.
- The state machine `FUN_806EF820` (type-1 messages, `[msg+20] == 1`) handles the
  *sub-states* 0/2/3/4; state **5 only comes from the `0x1005` command**.
- The game-side sender would be the sound-object constructor at `0x800AF1A8`
  (sets vtable `0x8075F8E8`, calls `0x8053E5AC(parent, 4101)`), but in our runs
  **neither `0x800AF1A8` nor `0x806EFE00`/`0x806F7E6C` is ever reached** -- the
  init wait (`0x806E6CC8`) happens first and the module never receives a
  command. This is the precise deadlock to resolve next.

### Sound module queues (decoded)

Two queues are involved, which is why the self-post at init does not reach the
command dispatcher:

- **Internal state queue** at `0x808E8D30` (+64 = lock/semaphore). Posted by
  `0x806E7120(msg, type, arg)` which sets `[msg+20] = type`, `[msg+12] = arg`
  and signals. The module init (`FUN_806EF480`) posts exactly one message here:
  **type 1, arg 4** (observed at block 1 444 300, `msg = 0x808E978C`).
- **Per-type command lists** at `0x80765250 + type*4` (type 0..4), popped by
  `0x806E43D0(type)`; `FUN_806E6DA0` (the sound thread) pops **type 0** in its
  loop. Message layout: `+0` u16 protocol (`0x1000/0x1100/0x1200/0x1600/
  0x1900`), `+16` u16, `+20` command id (1/5/8/10/13/22/60/66).
- Command id **1** enters the state machine `0x806EF820`; the init command
  `0x1005` reaches `FUN_806F7E6C` -> `0x806EFE00` (state = 5).
- In our runs the thread *is* alive and popping type-0 messages (`0x806E43D0`
  hit repeatedly from `0x806E6F2C`), but no message ever carries the `0x1005`
  command: the game-side sender (`0x800AF1A8` -> `0x8053E5AC(parent, 4101)`) is
  never reached because the message/font init is already waiting at
  `0x806E6CC8` for state 5. This is the ordering deadlock to break next.

### Corrupt copy after audio bypass (diagnosed)

Forcing the sound state (with the config fields filled as `0x806EFE00` would)
still leads to the same fault, now caught precisely:

```
[badcopy] pc=0x80004434 src=0x94000000 len=0xFF6F2D80 dst=0x136F2980 lr=0x8011C7AC
```

- `src = 0x94000000` is exactly **one past the end of our 64 MiB MEM2**
  (`0x90000000 + 64 MiB`).
- `len = 0xFF6F2D80` is negative-as-unsigned (underflow).
- `dst = 0x136F2980` is a *physical* MEM2 address.

So a size/pointer computation based on memory bounds underflowed. Candidate
causes: the low-memory physical-memory globals (`0x80000028` currently holds the
GC-style `0x01800000`), the MEM2 arena the game derives, or the frame/audio
buffer descriptor coming from the (still zero-filled) NAND/config data. The
caller returns to `0x8011C7AC` (`0x8011C788` -> `0x803BDFF4` -> `0x800043C4`),
which reads a handle from `[r13-21572]` and a size from `0x8036EB90`.

### Sound start chain (verified by tracing)

The complete start sequence, confirmed with PC traces:

- `FUN_806E6D60` ("sound system start"):
  `[r13-25144]=1`, `[r13-25140]=1`, `[r13-20376]=0`, `[r13-20372]=0`
  then `0x806F871C` -> `0x806E6A20` -> `0x806E74CC`.
- `0x806F871C` -> `FUN_806F29D4` (memset 10180 bytes at the sound block, then
  `0x806F1E88`, `0x806ED070`, `0x806F5788(2)`, `0x806F36C4`, `FUN_806EF480`) and
  then `0x807026A8`, `0x8070E7D8`, `0x806F0E08`.
- `0x806F0E08` builds and posts a **`0x1002`** command through the game API
  `0x8070BA2C` (observed at block 1,445,478 with queue ctx `0x808E98EC`).
- `0x806E74CC` memsets the 140-byte timer block at `0x808E8D78`, arms a
  `1000`-tick timer via `0x806E7710`, stores the queue index from `0x806E5530`
  at `+125`, and copies the config byte `[r13-25168]+4` to `[r13-20368]`.
- **`0x1005` is never posted**: its only sender is `0x800AF1A8`
  (`0x8053E5AC(parent, 4101)`) and that function is never reached.
  The message/font init polls `[0x808E9866] == 5` at `0x806E6CC8` in the
  meantime, so the boot waits on a command the game only sends later.

Internal queue details (for a faithful injection later):

```
head: 0x808E8D30+64 = 0x808E8D70     (timer-ordered, [node+12] = countdown)
node: +0 next, +8 callback, +12 countdown, +20 u16 type, +22 flag
post: 0x806E7120(msg, type, arg) -> sets +20=type, +12=arg, links, signals
pop:  0x806E43D0(type) from per-type heads [0x808E5250 + idx*16 + type*4]
```

### Audio shortcut is now the default + the real data fault (this round)

- The sound-init shortcut is now **on by default** (disable with
  `COLORS_NO_AUDIO_HLE=1`): it satisfies the message/font wait so boot proceeds
  while the sequencer message flow is still unimplemented. Documented as an HLE
  shortcut, not emulation.
- With it, the game advances to a genuine data fault, now characterised:

```
[badcopy] src=0x94000000  len=0xFF6F2D80 (-9,490,048)  dst=0x136F2980
```

  - `len` is a **negative size** (`0x100000000 - 0xFF6F2D80 = 0x90D280 =
    9,490,048`), i.e. a size computation underflowed.
  - `src = 0x94000000` is one past MEM2: the memcpy ran ~10.4 M iterations
    (32 B each) from a normal stack source until the pointer wrapped past the
    end of memory. The reported `lr = 0x8011C7AC` is a *stale* LR (that call
    site passes a byte-sized length from `0x803685B0`), and the guest stack is
    zeroed at that point, so the true caller still needs to be identified.
  - It happens right after the game's movie/text loader consumed the
    `movie/ev150.sfd` sector read (0x800-byte read at 0x2E55F4A0, served
    correctly by `src/disc.c`).

### The post-audio fault, precisely located (this round)

Caught at the **memcpy entry** with a huge length:

```
[badcopy] call dst=0x00000000 src=0x8090D680 len=0xFF6F2D80 lr=0x8011C7AC
          backchain: 0x8011C7AC <- 0x8011C3A4 <- 0x80011504 (CRT)
```

- `src = 0x8090D680` is the caller's stack buffer (`r1+64`) and `len` is the
  bogus size, so this *is* the `0x8011C7A8` call site.
- `dst = 0` is the return of the read/alloc thunk `0x803BDFF4`, which loads a
  callback from `[r13-21568]` and was asked for `len = 0xFF6F2D80` bytes
  (~4.28 GB, i.e. `-9,490,048`) → the callback correctly fails and returns 0,
  and the caller then memcpys into NULL, which is what we see running away.
- The size comes from `0x8036EB90()`:
  `r31 = [obj+32]` via `0x803685B0`; if `r31 == 0` it returns
  `0x803685A0(obj)` (`obj = 0x807DB3D0`, the movie/text archive handle), else 0.
  So an **object size field is negative/garbage**, which is the root cause.

This happens right after the movie loader consumed its first sector
(`movie/ev150.sfd` at 0x2E55F4A0, served correctly by `src/disc.c`).

### Size source of the bad memcpy (traced)

`0x8036EB90` (returns the bogus size) works like this:

```
r30 = 0x807FB230                    ; "current file/stream" object
0x80369B40()
0x80368510(r30)                     ; copies a 36-byte descriptor from
                                    ;   0x807FAF84 into r30
r31 = 0x803685B0(r30)               ; byte  [r30+32]
0x80367F90(r30+1664)
... (OSDisableInterrupts, copies 1536 B from r30+128 to [r2-888]+0) ...
if (r31 == 0) return 0x803685A0(r30)  ; dword [r30+28]  <-- the size
return 0
```

At the fault the descriptor at `0x807FAF84` reads:

```
8091DC20 80965C1C 00048000 00044000 00024000 80000000 80000000 00000000 00010100
```

i.e. `+0` = the game's **ArenaLo we computed** (`0x8091DC20`), plus a pointer and
sizes `0x48000/0x44000/0x24000`. That is a *memory/heap descriptor*, not an FST
entry, so the bogus size comes from the loader's own bookkeeping (likely a
memory-region/stream size that our apploader-style globals drive). Next step:
identify the writer of `0x807FAF84` and the descriptor semantics.

### Descriptor source of the bogus size

`FUN_80368400` (called from the loader before the copy) fills the descriptor at
`0x807FAF84`:

```
[desc+24] = ((src[20] & 0x07FFFFF8) << 3) + 0x80000000
[desc+32] = bit (src[20] << 3) >> 31
```

with `src = [r13-22360]` (a stream/archive global). The size actually used
(`[obj+28]`, via `0x803685A0`) is written by other code in the same `0x80367xxx`
module (writers at `0x80367EE8`, `0x80367FC0`, `0x80367FE4`, `0x80368100`,
`0x80368424`). So the next step is to inspect `[r13-22360]` (the stream/archive
handle) at the fault and see which of its fields is uninitialised — most likely a
field our file/disc service never filled.

### Why the runaway memcpy matters + memory globals (this round)

- The `memcpy(NULL, stack, 0xFF6F2D80)` starts at address 0 and walks upward, so
  it **overwrites low memory** -- watchers show `__OSCurrentThread (0x800000E4)`
  going to 0 exactly when the copy reaches it, and the thread scan drops to 0.
  So this single bad call destroys the OS state; everything after is garbage.
- The size comes from the loader's stream descriptor (`[obj+28]` via
  `0x803685A0`, object `0x807FB230` copied from `0x807FAF84`). Its fields look
  heap/arena-like (`base = 0x8091DC20` = the ArenaLo we computed, plus
  `0x48000/0x44000/0x24000` sizes and two `0x80000000` flags), so a stream/arena
  size is underflowing.
- **Memory globals corrected**: `0x8000311C/0x80003120` are the apploader's
  **MEM2 arena** bounds, not a MEM1 arena. They are now
  `0x90000000` / `0x94000000` (previously `0x311C` held `0x817FEC60`, a MEM1
  address, which is exactly the sort of value that produces MEM2-end pointers
  like the `0x94000000` we saw). The IPC heap bounds are separate
  (`0x80003130/0x3134` -> `[r13-21984]/[r13-21988]`), so this change is safe.
- The fault still reproduces, so the stream descriptor has another uninitialised
  field; the next probe is the stream global `[r13-22360]` and its `+0x14`
  (`[src+20]`) input to `0x80368400`.

### The bogus size is an ARAM/DSP DMA length (this round)

Tracing the descriptor writer `0x8036813C`..`0x80368148`:

```
r4 = [desc+24]                    ; encoded transfer length
r3 = [r13-22360]                  ; ARAM/DSP stream manager global
[r3+20] = r4 & 0x1FFFFFFC         ; <-- DMA length encoding (>>2 / aligned)
```

and `FUN_80368400` sets `[desc+24] = (([src+20] & 0x07FFFFF8) << 3) + 0x80000000`
from that same field. So the "size" the loader gets back (`0xFF6F2D80`,
`-9,490,048`) is a **decoded ARAM/DSP DMA length**, i.e. the module at
`0x8036xxxx` is the ARAM manager and its bookkeeping is uninitialised because we
don't model ARAM at all (only DSCR/mailbox registers).

`DolRuntime` already ships an ARAM implementation
(`tools/DolRuntime/include/dolruntime/aram.h`: 16 MiB store, `aram_read/write`,
`aram_dma_to_aram`/`aram_dma_to_ram`, `ARAM_BASE 0x10000000`). Wiring it in is the
next concrete step — but note its synthetic window `0x10000000..0x11000000`
**collides with our MEM2 physical mapping** (`0x10000000..0x14000000`), so the
ARAM base needs relocating (or MEM2's physical window adjusted) first.

### Diagnostic guards added

- `COLORS_CLAMP_COPY=1` enables two diagnostic guards:
  - `[memcpy-clamp]`: a `memcpy` with length >= 0x10000000 becomes a no-op
    (prevents the runaway copy from zeroing low memory / destroying
    `__OSCurrentThread`).
  - `[cache-clamp]`: same for `DCStoreRange`/`DCInvalidateRange`/`DCZeroRange`,
    whose no-op loops otherwise burn hundreds of millions of dispatches.

Default build keeps the pristine behaviour (the runaway copy is diagnosed but
not altered). With the clamp, the OS survives (16 threads) and the loader loop
recomputes the same bogus ARAM length; skipping the copy then leads to a null
call at `0x800129A0` (`pc -> 0x80000000`), showing the copy is load-bearing: the
ARAM/DSP transfer really has to work. This confirms **ARAM as the root cause**,
not the movie data.

### Descriptor fill path (final link)

`FUN_80367F90(info)` copies a **36-byte info struct** into the descriptor at
`0x807FAF84` (fields 0..32). The loader calls it as `0x80367F90(obj+1664)`, and
`obj = 0x807FB230` is the ARAM/stream manager block. So:

```
game builds info @ obj+1664 == 0x807FB8B0
      -> 0x80367F90 copies it to desc @ 0x807FAF84
      -> 0x80368400 re-derives desc+24 from [ [r13-22360]+20 ]
      -> 0x80368510 copies desc into obj
      -> 0x8036EB90 returns obj[+28]  (the "size")
```

So the size is the **game-supplied stream info's size field**, expressed through
the ARAM/DSP DMA length encoding. Fixing it means either (a) emulating the
ARAM manager's DMA bookkeeping so its encodings round-trip, or (b) finding why
the game's own info struct (at `0x807FB8B0`) has a bogus size -- its writer is
the next probe (the caller chain around `0x8011C700` -> `0x80368xxx`).

### ARAM register notes (so the next session doesn't chase them)

- `0xCC005028/0x502A/0x502C` = DSP DMA address/length registers; the game's
  `ARStartDMA` path writes them and reads back `0xCC005036` as DMA **control**
  (not the ARAM size):
  `803580E0..803580F4: lhz 20530/20534(r7); rlwinm ...; sth` -- control bits.
- `0xCC005036` bit 15 is cleared by `__DSPInit` (`8033AFCC`), confirming it is a
  control/status register, not a size register. The GC SDK's ARAM allocator uses
  a compile-time 16 MB size, so the bogus length is *not* due to a missing
  hardware size read.
- Therefore the length underflow comes from the ARAM manager's own bookkeeping
  after an allocation/DMA call that we do not satisfy (the manager state lives
  at `[r13-22360]` and in the object at `0x807FB230` / info at `0x807FB8B0`).

### The `0x8036xxxx` module is the DSP/AX driver (not a file loader)

Decisive identification this round:

- `0x80368830(id, value)` and `0x80369120(a, id, ...)` are **DSP/AX parameter
  setters**: both dispatch through jump tables on `id` (`0x8078xxxx` tables,
  `id <= 0x19 / 0x10`) and bit-pack values into a per-channel struct array at
  `[r2-888] + id*4` (e.g. `80369150..80369160: rlwimi r0, r5, 0, 31, 31; ...
  stw r0, 28(r8)`).
- The long chains of `0x80368830(9/11/13/14/15, 1)` before the failing
  `memcpy` are therefore **voice/sample setup**, not file I/O.
- `0x800043C4` is confirmed to be `memcpy` (`cmplwi cr1,r5,0; bclr` /
  `cmplw cr1,r4,r3; bclr` / small-path for len < 128).
- So the failing `memcpy(NULL, stack, 0xFF6F2D80)` is an **audio sample copy**
  from a stack staging buffer into its destination, and the "size" is the
  remaining sample bytes tracked by the ARAM/DSP manager -- which underflows
  because ARAM/DSP sample bookkeeping is not emulated.

Conclusion: to get past this, the game's DSP/AX sample path needs the ARAM model
(`DolRuntime`'s `aram_*`) plus enough DSP parameter semantics that the manager's
remaining-size arithmetic stays consistent.

### ARAM DMA register file wired (this round)

`src/main.c` now models the DSP ARAM DMA register block (`0xCC005020..0xCC005036`)
with instant completion:

```
0x20/0x22  main-memory address lo/hi
0x24/0x26  count lo/hi (main-memory DMA)
0x30/0x32  ARAM-side address lo/hi
0x36       length>>5 + start bit (bit 15); writing it with bit 15 clears the
           bit immediately (transfer done) and logs
```

The game's DMA setup helpers were decoded from its own code:
`0x803580A0(addr, len)` writes `0x5030/0x5032 = addr` and
`0x5036 = (len >> 5)`; `0x80358120` starts (sets bit 15) and `0x80358140`
clears it. Observed first transfer:

```
[dsp] write 0xCC005030 = 0x807E
[dsp] write 0xCC005032 = 0x48C0
[dsp] write 0xCC005036 = 0x000C
[dsp] write 0xCC005036 = 0x800C   -> dma ar=0x48C0807E len=384
```

So the game does issue ARAM DMAs (384 bytes here), and they now complete
instantly. The runaway sample-size memcpy persists, confirming the underflow is
in the AX sample **bookkeeping** (CPU-side), not in DMA completion.

### Key insight: the length is probably clobbered, not computed

At the fault the descriptor/info structs read:

```
info  @0x807FB8B0: 8091DC20 80965C1C 00048000 00044000 00024000 80000000
                   80000000 00000000 00010000 00000000
                   (field 7 == 0  -> the "size" should be 0)
desc  @0x807FAF84: 8091DC20 80965C1C 00048000 00044000 00024000 80000000
                   80000000 00000000 00010100 00000000
```

Yet the caller used `len = 0xFF6F2D80`. Since `0x8036EB90` would return 0 for
that info struct, the length must have been **clobbered between the size
lookup and the memcpy** -- and the only call in between is
`0x803BDFF4(handle, r25, &buf, 255)`, the read/alloc thunk that invokes a
callback from `[r13-21568]` (which returned NULL). This strongly suggests the
callback violates the ABI / fails and destroys `r25`.

So the next probe should be `[r13-21568]` (the registered read/alloc callback)
and why it returns NULL, rather than the ARAM size arithmetic.

### Found the failing read callback chain (this round)

The read/alloc thunk `0x803BDFF4` calls the callback registered by
`FUN_803BDF94(r3, r4)` at `0x8002FAA0`, which installs:

```
read  cb = 0x8002F6E0     write cb = 0x8002F6FC
```

`0x8002F6E0` is a thin wrapper:

```
or  r0, r3, r3
lwz r3, -24720(r13)        ; archive/file handle
li  r6, 0
li  r7, -1
b   0x800300DC             ; archive read(handle, buf, len, 0, -1)
```

At the fault the handle is valid (`[r13-24720] = 0x807D57C0`) and the callback
is registered, yet the read returns 0, so the loader gets `dst = NULL` and then
runs with a clobbered length. **So the real failure is the archive read
`0x800300DC` returning 0** -- i.e. the game's CPK/archive layer cannot serve the
requested data. That is the next thing to fix (the archive is
`sonic2010_0.cpk`, which the disc service can serve once the game opens/reads it
at the right sector offsets).

### The failing reader is a C++ iostream (this round)

`0x800300DC` is the game's generic `read()` dispatcher: it calls a **virtual
method on the handle** (`vtable[24]` or `vtable[28]`, chosen by comparing the
buffer against `[r13-32672]`), i.e. a Metrowerks `filebuf`/`istream` read. At the
fault the stream object reads:

```
stream@0x807D57C0: 00000000 01000000 00000000 00010000
                   807D57B0 80030684 807D57DC 807C6780 0000000A 00000020
```

- **`+0` = 0** -- the vtable pointer is NULL, so the stream is *not constructed*
  (no vtable), and any virtual read on it cannot work. That is why `0x800300DC`
  yields 0 and the caller ends up with `dst = NULL` plus a clobbered length.
- `+20 = 0x80030684` and `+28 = 0x807C6780` are still plausible data pointers, so
  the object is partially initialised -- consistent with a failed open that left
  the stream half-built.

So the chain is: **file open fails -> stream has no vtable -> virtual read
returns 0 -> loader gets NULL -> runaway sample copy**. The next step is to find
the stream construction/open (a C++ `ifstream` on an archive) and why it fails,
i.e. which path/handle it uses (most likely the archive on disc, or a NAND file
we still return ENOENT for).

### Correction: the huge size is genuine, not clobbered

Tracing the block right after the lookup (`0x8011C780`) settles it:

```
[size] lookup returned 0xFF6F2D80      (r25 <- r3 = 0x8036EB90())
[badcopy] call dst=0x00000000 src=0x8090D680 len=0xFF6F2D80 lr=0x8011C7AC
```

So `0x8036EB90()` really returns `0xFF6F2D80`; the length is *not* destroyed by
the intermediate call. (The `info`/`desc` structs dumps read 0 only because they
are rewritten later in the loop.)

Therefore the underflow is inside the **AX/ARAM sample manager's remaining-size
arithmetic** (`[obj+28]`, object `0x807FB230`, info `0x807FB8B0`, fed through
`0x80368400` / `[r13-22360]+20`). That manager's state depends on:
- the ARAM DMA completions (now modelled, instant),
- the AX voice/parameter API (`0x80368830`, `0x80369120`) writes,
- its own allocations (ARAM addresses it hands out).

Next step is to watch `[r13-22360]+20` / `[obj+28]` across the loader loop and find
the first iteration where the "remaining" value goes negative, which pins the
allocation/DMA call that broke the invariant.

### The "manager" globals are hardware base pointers (important)

`0x80366F20..0x80366F3C` (DSP/AX init) sets:

```
[r13-22356] = 0xCC000000   (DSP block)
[r13-22360] = 0xCC003000   (PI / GX FIFO block)
[r13-22352] = 0xCC001000   (PE block)
[r13-22348] = 0xCC004000   (CP block)
```

So these are **hardware base addresses**, not struct pointers -- the module at
`0x8036xxxx` is a register abstraction layer (DSP / PI-FIFO / PE / CP). Therefore:

- `[ [r13-22360] + 20 ]` is a read of **`0xCC003014`** (PI FIFO end register).
- The size arithmetic depends on GX FIFO registers we did not model.

I added a minimal PI/GX FIFO model (`0xCC00300C..0xCC003023`: base/start/end/
write-pointer/wrap, with the read pointer tracking writes so the FIFO drains
instantly). The huge size still appears, so the value comes from another field
of this abstraction (candidates: `0xCC001000`-based PE registers or `0xCC004000`
CP registers, or the FIFO registers being written with zero by the guest before
the read).

Next probe (updated after instrumentation): the PI-FIFO registers are **not
touched at all** in this run (no `[pi]` accesses), so the size is *not* coming
from `0xCC003014`. It comes from **field 7 of the info struct at `0x807FB8B0`**
(`obj+1664`), which the AX/DSP module fills before the copy. So the next probe is
the writer of `obj+1664` (fields: `8091DC20 80965C1C 00048000 00044000 00024000
80000000 80000000 <size> ...`), i.e. the AX voice/sample allocator state, not the
GX FIFO.

### AX "obj" initialiser found (this round)

`FUN_8036EAD0(base, size)` initialises the AX object at `0x807FB230`:

```
[r29+0]  = base
[r29+4]  = base + size - 4
[r29+8]  = size
[r29+28] = 0          <-- the "size" later returned by 0x8036EB90
[r29+20] = base
[r29+24] = base
... then 0x80368510(r31+1664)  (desc -> info copy)
     and 0x80367F90(r31)       (obj  -> desc copy)
```

Call sites show the intended usage, e.g. `0x8011C474`:
`0x8036EAD0(r1+64, 1024)` -- a stack staging buffer of 1024 bytes.

So `obj[+28]` starts at 0 and is **updated later** by other code (the sample/voice
bookkeeping) to a "remaining bytes" value; that update is where it becomes
`0xFF6F2D80`. The next probe is therefore a watch on `[0x807FB230+28]` across the
loader loop to catch the first bad write and its PC.

### ROOT CAUSE of the huge size (found)

`FUN_80368400` computes the descriptor [28] (the "size") as:

```
r4 = [desc+20]                       ; (reg0xCC005038<<16 | reg0xCC00503A) + 0x80000000
r0 = [desc+24]                       ; (reg0xCC005030<<16 | reg0xCC005032)
r4 = r0 - r4                         ; subf
[desc+28] = r4
if (r4 > 0) [desc+28] += [desc+8]
```

and the write that made the object's size absurd was caught live:

```
[axsize] 0x00000000 -> 0xFF6F2D80 at pc=0x8036853C lr=0x8036EBB8
```

So the loader's "size" is literally:

```
size = (DSP reg 0xCC005030/32) - (DSP reg 0xCC005038/3A) - 0x80000000
```

Our run has `0xCC005030/32 = 0x7F6F2D80` and `0xCC005038/3A = 0`, giving
`0x7F6F2D80 - 0x80000000 = 0xFF6F2D80` exactly. The `0x38/3A` pair is a
**hardware-maintained DSP/AID DMA pointer** that the game never writes, so it
reads 0 and the subtraction underflows.

Next step: model `0xCC005038/0xCC00503A` (and check `0x34/0x36`) as the DSP's
current DMA pointer / counters, advancing them with the DMA so this
"remaining bytes" arithmetic stays positive. That is the single remaining
unknown for this failure.

### Refined formula + why it breaks

Re-reading `FUN_80368400` precisely:

```
desc[24] = ((reg 0xCC003014 & 0x07FFFFF8) << 3) + 0x80000000   ; PI-block reg
desc[20] = ( (reg 0xCC005038 << 16) | reg 0xCC00503A ) + 0x80000000
desc[28] = desc[24] - desc[20]
```

With the game having written `0xCC005038/3A = 0x0090D280` and `0xCC003014 = 0`
(never touched), that is:

```
desc[28] = 0x80000000 - (0x90D280 + 0x80000000) = 0xFF6F2D80
```

which matches the observed value exactly.

**Key finding: the game never writes the PI/GX registers at all** (no `[pi]`
accesses in the whole run) -- its GX/FIFO initialisation has not run, so
`0xCC003014` reads 0. The audio/loader code reuses that PI-block register as a
size base, so the underflow is a *symptom* of missing GX init.

Next steps:
1. Find whether/when the game's GX init should run (it is likely blocked earlier
   or waits on a display/VI condition), and initialise `0xCC00300C..0020` as the
   real GXInit does (FIFO base/end, write pointer).
2. Then this loader arithmetic turns positive and the sample copy proceeds.

### Correction: that register path was gated off

`0x80368440: lbz r0, -22319(r13); cmpwi r0,0; beq 0x8036847C` -- the
PI-register reads are skipped when the flag is 0, and it *is* 0 in our run
(consistent with no `[pi]` accesses at all). So `desc[24]` keeps whatever value
the earlier part of `FUN_80368400` computed:

```
desc[24] = (( [ [r13-22360] + 20 ] & 0x07FFFFF8 ) << 3) + 0x80000000
```

i.e. the read of `0xCC003014` happens unconditionally in that earlier block, but
our `pi_fifo_read` log showed no access... meaning the value used came from the
**cached/stale `desc[24]`** path (the function's early-out at `0x80368418`
`if ([r13-22320] == 0) goto 0x80368440` may also skip it).

So the exact provenance of `desc[24]` in this run still needs one more probe:
log `desc[20]`, `desc[24]`, `desc[28]` and the `-22319/-22320` flags at the
`0x80368538` call site. The rest of the chain is fully mapped.

### Final provenance of the bad subtraction (verified)

Probe at the descriptor update (`0x8036853C`), two consecutive passes:

```
#1 flags=1/1 d20=80000000 d24=80000000 d28=00000000
#2 flags=1/1 d20=8090D680 d24=80000000 d28=FF6F2D80   <- the break
```

`d20` changes from `0x80000000` to `0x8090D680` via the swap block at
`0x80368498..0x803684B8`, which moves fields between the two descriptors
(`0x807FAF84` and `0x807FAF60`). `0x8090D680` is exactly **the stack staging
buffer** passed in by `0x8036EAD0(r1+64, 1024)`.

`d24` stays `0x80000000`, which is the `+0x80000000` bias applied to a **zero**
register/field value. So:

```
size = d24 - d20 = 0x80000000 - 0x8090D680 = 0xFF6F2D80
```

i.e. the code subtracts a **stack pointer** from a **bias-only constant**. On
real hardware `d24` must be a real end/limit address above the buffer (the GX
FIFO end or the audio buffer end), so the subtraction is positive.

Conclusion: the loader/audio path expects the two descriptors to carry the real
buffer **start/end** (built by `0x8036EAD0` + the FIFO/ARAM registers). Our build
has `end = 0`, so it underflows. Seeding the PI-FIFO registers (base/start/end/
wptr now `0x80000000 .. 0x80200000`) did not change `d24`, so `d24` is filled by a
*third* writer -- the remaining probe is the writer of `desc[24]` between the
`0x8036EAD0` init and the `0x80368400` update.

### FIXED: the long-running runaway copy (PI FIFO shadowing)

Root cause of the `0xFF6F2D80` size and the memcpy that destroyed low memory:

- `FUN_80368400` computes `desc[24] = (([0xCC003014] & 0x07FFFFF8) << 3) + 0x80000000`.
- That read was being answered by DolRuntime's interrupt model (which owns
  `0xCC003000..` and returns 0 for offset `0x14`), so the PI-FIFO register model
  added in `src/main.c` **never saw the access**. `desc[24]` collapsed to
  `0x80000000`, and the size became `0x80000000 - <stack ptr> = 0xFF6F2D80`.
- Fix: route `0xCC00300C..0xCC003027` **before** `interrupt_mmio_*`, and seed the
  FIFO registers as GXInit would (base/start `0x80000000`, end/wptr
  `0x80200000`).

Verified result:

```
[desc] flags=1/1 d20=80000000 d24=8091DC20 d28=00000000
[desc] flags=1/1 d20=8090D680 d24=8090D680 d28=00000000   (size now 0)
```

`[axsize]` and `[badcopy]` are gone; the runaway memcpy and the OS-memory
corruption no longer occur. The game now reaches block ~7.25M and proceeds into
its C++ layer.

### New blocker: null virtual call at `0x800129A8`

```
pc left recompiled code: 0x80000000 (from 0x800129A0, lr=0x800129BC)
8001298C: lwz r31, 0(r3)          ; object
800129A8: lwz r12, 8(r31)         ; <- field +8 is NULL
800129B0: lwz r12, 12(r12)
800129B4: mtctr r12 ; bctrl       ; -> jump to 0
```

`0x80012A08` is a **reference-count decrement** (`lwarx/stwcx` on `[r3]`,
returning the new count). So the pattern is:

```
if (--obj->refcount != 0) skip;
else  obj->vtable->destroy(obj);     <- [obj+8] is NULL here
```

i.e. this is a C++ **destructor** being invoked on an object whose interface
pointer at `+8` is NULL -- either a double-destroy (vtable already cleared), an
object that was never constructed, or a refcount that reached 0 too early.
The PC history at the stop (`0x80136F2C`, `0x80136F38`, `0x8005D8BC`,
`0x80012948`) shows the game's C++ container/refcount layer.

Next step: identify the object (`r31 = [[r3]]`, `r3` at the stop was
`0x80AE6A60`) and check whether its constructor ran (it would set `+8`), or
whether our emulation destroyed it twice.

### The crashing object is an all-zero heap block (this round)

Instrumenting the vcall site (`0x800129A0`) gives:

```
[vcall] NULL iface: obj=0x80AE6A60 refcount=0 (hit 112)
    +00 00000000
    +04 00000000
    +08 00000000   <- interface pointer never set
    ... (whole 0x40 bytes zero)
```

- The address is inside the game's **arena heap** (`0x8091DC20..`), so this is a
  heap-allocated object.
- Its reference count was 1 (the decrement took it to 0) *before* the destructor
  call, yet the rest of the object (including the `+8` interface/vtable) is
  entirely zero.
- The preceding 111 vcalls used valid interfaces (`0x807A06D8`, `0x8075A790`),
  with objects at `0x80AE6000`, `0x80AE60A0`, `0x80AE6100`, `0x80AE6180`, ...
  i.e. the same object type allocated 0x80-0x100 bytes apart.

So one instance of a C++ container element is allocated and refcounted but **never
constructed** (or was cleared). Next probe: find the allocation/factory for these
elements (likely `0x80012A28`-`0x80012A44` helpers) and the constructor that sets
`+8`, then determine why instance #112 skipped it (allocation failure, a load
that returned NULL, or a zeroing pass).

### Object lifetime captured: constructed, then zeroed by a fill

Watching `0x80AE6A60` through its whole lifetime gives the full story:

```
refc 00000000->80AE6A80  iface 00000000->00000000  pc=0x803DD5A0  (freed: free-list link)
refc 80AE6A80->00000001  iface 00000000->8079FD80  pc=0x803CC354  (allocated + constructed)
refc 00000009->00000000  iface 8079FD80->00000000  pc=0x800046B8  (ZEROED by a fill)
refc 00000000->00000001                              pc=0x80012948  (container refcount set)
refc 00000001->00000000  iface 00000000             pc=0x80012A08  (destructor -> NULL vtable)
```

So the object **is** constructed correctly (`iface = 0x8079FD80`), then a
**memory fill zeroes it** (refcount 9 -> 0, interface -> 0). The container later
refcounts and destroys it, jumping through the now-NULL vtable.

The fill is the `0x80004660..0x80004710` word-fill core (stores of `r7`, 32 bytes
per iteration). Its loop executes inside a single recompiled block (backward
`bc` becomes a `goto`), so per-iteration hooks at the loop head only see the
first iteration; the caller of the fill (whoever passed a range covering
`0x80AE6A60`) still needs to be captured -- best done by hooking the fill's
public wrapper entry `0x80004714` and validating `(dst, len)` against the object.

Conclusion: this is a **heap overrun / stray zero-fill** (an emulation-induced
size error elsewhere), not a bad constructor.

### The object is a BSS pool element; failure is a double-destroy

The fill instrumentation clarifies the object's home and the failure mode:

- `0x80AE6A60` lies inside **BSS** (`0x807C6500 + 0x12B014 = 0x808F1514`; the boot
  BSS clear `fill(dst=0x807C6500, len=0x12B014, val=0)` covers it). So it is a
  **static pool element**, not a heap allocation.
- The first lifetime transition stores a free-list link at `+0`
  (`0x803DD5A0`), then the element is allocated/reused and constructed
  (`iface=0x8079FD80`, `0x803CC354`).
- The "zeroing" transition (`refc 9 -> 0`, `iface -> 0`) at `0x800046B8` is a
  **memset from the object's own destructor** (the memory routine's `lr`
  `0x8000472C` is its caller) -- i.e. the object destroyed itself and cleared its
  vtable.
- Then the container sets `refc = 1` (`0x80012948`, `iface` still 0) and destroys
  it again -> NULL vtable -> jump to 0.

So this is a **double-destroy / reuse-after-destroy** of a pool element: the
object is torn down, re-refcounted without re-initialisation, and torn down a
second time. The next step is to find the pool's alloc/free pair
(`0x803DD590`-ish free, `0x803CC354` construct) and see why the second use did not
re-run the constructor (most likely a failed allocation/return-code check that
our emulation perturbs).

### ROOT CAUSE FIXED: the boot memory map (0x80003118..0x80003134)

The multi-round heap corruption / NULL-vtable crash is fixed. Root cause: the
host boot environment wrote the **wrong values into the IOS/boot memory map**
that the PPC loader leaves in low memory. We had guessed that
`0x8000311C/0x80003120` were MEM2 arena bounds (`0x90000000/0x94000000`) and
that `0x80003130/34` were an IPC heap in MEM1. The actual map (WiiBrew "Memory
map") is:

| Address | Meaning | Value we now write |
|---|---|---|
| `0x80003100` | physical MEM1 size | `0x01800000` |
| `0x80003104` | simulated MEM1 size | `0x01800000` |
| `0x8000310C/10` | MEM1 arena start/end | OSInit-derived / FST start |
| `0x80003118` | physical MEM2 size | `0x04000000` |
| `0x8000311C` | simulated MEM2 size | `0x04000000` |
| `0x80003120` | end of MEM2 addressable to PPC | `0x93400000` |
| `0x80003124` | usable MEM2 start | `0x90000800` |
| `0x80003128` | usable MEM2 end | `0x933E0000` |
| `0x80003130/34` | IOS IPC heap start/end | `0x933E0000` / `0x93400000` |
| `0x80003138` | Hollywood version | `0x00000011` |

Consequence of the old values, established with live traces:

- The renderer carves its texture pools out of `[r13-27764]` (= `[0x3124]`,
  the usable MEM2 start) up to `[r13-23540]` (= `[0x3128]`, the usable MEM2
  end). With `0x3124/0x3128` zero it produced a pool of **negative size**
  (`base=0x00A8E140`, `end=0xFFFD0044`, i.e. `size=0xFF541F04`), and that
  pool then handed out blocks overlapping live allocations.
- A subsequent 640x480 texture clear (`memset(0x00AD7B20, 0, 0x4B000)`)
  overwrote a live 16-byte smart-pointer control block at `0x80AE6A60`; its
  strong/weak counts were already at 0 after the destructor, so the next
  release took `[block+8]` (the interface/vtable) as NULL and jumped to 0.

With the map fixed, texture buffers land in MEM2 (`0x90ABE660`, `0x90AD8320`,
...) and the crash is gone.

### Audio: AID interrupt + draw-done modelling

The game's main thread was found blocked in the AX DMA wait
(`0x80369F40`, flag `[r13-22280]`, queue `[r13-22288]`). The release path is
the SDK AID interrupt:

1. hardware raises **PI cause DSP (0x40)** with DSCR `AID` status bit 3;
2. `__OSDispatchInterrupt` reads `0xCC00500A`, maps DSCR bit 3 to internal
   source `0x04000000`, and dispatches handler id 5 = `0x80358330`;
3. that handler acks DSCR bit 3 (write-1-to-clear semantics), then calls the
   registered AI callback (`[r13-22744]`);
4. the AX callback sets `[r13-22280]` and `OSWakeupThread([r13-22288])`,
   releasing the waiter.

Host changes: `interrupt_assert_audio_dma()` (one per video frame, single
flight) + `interrupt_clear_audio_dma()` on a DSCR bit-3 write. The existing
audio-init shortcut now also performs `__AIInit`'s guest-visible effects
(register handler 5, unmask `0x04000000`, install the AX callback) so the
first AID completion can be delivered; the game later installs its own AX
callback (`0x8035A1E0`), which is the expected takeover.

For the renderer's "display list finished" wait, `interrupt_commit_draw_done()`
commits a **PE_FINISH** interrupt once per frame (only when handler id 19 is
installed). Handler 19 = `0x8036A290` sets the draw-done flag, wakes the queue
and acks through the PE interrupt status register (`0xCC00100A`).

Result: the main thread now cycles *wait → wake → work* and the game executes
its frame loop.

### Current state after this round

```
build   : clean, -Wall -Wextra clean
default : 200,000,000 blocks, no exception
boot log: MEM1/MEM2 arenas, heaps as on hardware (OSReport HLE)
disc    : CPK parsed; asset stream reads from 0xB957D280 onward
audio   : AID handler + AX DMA callback every frame (no storm)
stop    : scheduler/task idle loop after the asset batch
```

The remaining stop is a **content** problem: at block ~10.86M the game parses
a header at `0x9012C860` (MEM2 load buffer) and reads
`count = 0x6D056AF9` from `+8`, so its table walk never ends. The call chain is
`0x803BDA8C -> 0x80016878 -> 0x800A6A3C -> 0x8009B750 -> 0x8009B330` (game
file/loader code). That buffer should hold a real file (the game archives are
not yet served correctly by `disc.c`/IOS FS), so the next step is disc content
mapping rather than CPU/hardware work.

### FIXED: DI read offsets are FST word offsets (not bytes)

This was the blocker behind the RSO/module garbage. The `/dev/di` read command
block is `{u8 cmd; u32 offset; u32 length}` where **offset stays in the FST's
native unit (disc byte offset >> 2)**, exactly as the SDK's file layer keeps it.
`ios.c` was passing it through as bytes.

Decoded live (with `COLORS_TRACE_IO=1`), both reads become exact file starts:

| raw `in+8` | `<< 2` | object | length |
|---|---|---|---|
| `0x2E55F4A0` | `0xB957D280` | start of `sonic2010_0.cpk` (CPK header) | 2048 |
| `0x2E55ED68` | `0xB957B5A0` | `sonic2010.sel` | 7392 (its exact size) |

Before the fix the CPK header read landed inside `movie/ev150.sfd` (the FST word
offsets happen to fall in that file's disc range), so CriWare parsed movie data,
the HBM module pointer became bogus, and the Metrowerks relocator walked a
1.8-billion-entry table at `0x803BD094`.

After `offset << 2` the CPK is parsed correctly and the game streams assets:

```
[di] cmd=0x71 len=0x800   off=0xB957D280 -> CPK header
[di] cmd=0x71 len=0x1E440 off=0xB957DA80
[di] cmd=0x71 len=0xAEC0  off=0xB959C280
[di] cmd=0x71 len=0x79320 off=0xB95FAD40
...
```

### GX FIFO wired to DolRuntime's headless decoder (foundation for rendering)

The game writes a real GX command stream (e.g. `0x61 0F 00 00 FF` BP loads) to
`0xCC008000`; previously every word was swallowed. The host now routes FIFO
writes into DolRuntime's Aurora-independent recomp frontend
(`DolRuntime::retail_gx_frontend`, C API in
`dolruntime/aurora_recomp/retail_gx_frontend_c.h`):

- `src/gx.c` stages the big-endian FIFO bytes and hands them to
  `dol_aurora_recomp_frontend_write_fifo` + `_flush`, with a guest-address
  resolver that maps MEM1 (cached/uncached/physical) and MEM2 to host pointers
  for CP arrays, textures and TLUTs.
- The CMake target enables `DOLRUNTIME_ENABLE_AURORA_RECOMP=ON` (that build is
  Aurora-free; the full WebGPU Aurora renderer needs Dawn/SDL3 downloads and
  stays off) and now declares `CXX` for the project.
- One portability fix in the vendored `graphics/gxcore/src/gxcore.cpp`: a C++20
  designated initializer was out of declaration order (newer GCC rejects it).

Current decode result on a 100M-block run with the font bypass:

```
fifo_writes=193849  state_packets=633  resource_packets=10  draws=0
flush_failures=32667  pending=410247
```

i.e. CP/XF/BP state decodes correctly (633 state packets: array bases, VCD/VAT,
XF loads, BP writes), but draws are not produced and the frontend's pending
buffer grows past its 256 KB FIFO limit, after which bytes are dropped. Decoding
is therefore **opt-in** (`COLORS_GX_DECODE=1`) until the draw path is fixed;
the default run only counts FIFO writes.

To find the decode failure, `tools/gxfifo_probe.cpp` replays a raw FIFO capture
(`COLORS_GX_DUMP=<path>`) through the same frontend with a flat fake address
space and reports the decoder's own error (`last_error()`, opcode, offset,
context bytes). It pinpointed the first stall:

```
stall at offset 24802: error=unsupported FIFO opcode opcode=0xFF
context @24778: FF FF FF FF 3F 60 00 00 00 00 00 00 44 20 00 00
                43 F0 00 00 00 00 00 00 FF FF FF FF 3F 60 00 00
```

The probe now reports the last trace events before the stall, which localises
it to a **draw**:

```
ev[7042] kind=12 a=0x98 b=0x0 c=0x4 d=0x14   <- draw, opcode 0x98, 4 verts,
                                                derived vertex size 0x14 (20)
ev[7043..7046] kind=1 (1-byte NOPs at 0x60DF..0x60E2)
failing bytes: 00 00 FF FF FF FF 3F 60 00 00 ...
```

So the draw consumed `3 + 4 * 20 = 83` bytes, then the parser walked four
zero bytes and hit 0xFF — i.e. the vertex data the game wrote is **longer than
the derived layout** (the tail is float data). The most likely cause is the
VCD/VAT-derived layout missing an attribute the game enables (e.g. texture
coordinates or a normal), so `vertex_size` is under-estimated.

The failing draw is at FIFO offset `0x608B`: opcode `0x98` (prim 3, vtx_fmt 0),
vertex count 4. Decoding the bytes as 4 x 24 gives a clean **fullscreen quad**:

```
v0 pos=(0,0,0)     color=0xFFFFFFFF uv=(0.125,0)
v1 pos=(0,480,0)   color=0xFFFFFFFF uv=(0.125,1)
v2 pos=(640,0,0)   color=0xFFFFFFFF uv=(0.875,0)
v3 pos=(640,480,0) color=0xFFFFFFFF uv=(0.875,1)
```

So the true stride is **24 = position f32x3 (12) + color RGBA8 (4) + uv f32x2
(8)**, and the next real command (`48` INVL_VC + `61 43 ...`) starts exactly at
`0x608B + 3 + 4*24 = 0x60EE`. The decoder derived **20** (position + uv) because
its VCD/VAT reading does not see color0: the captured state is
`vcd_lo=0x00000200`, `vcd_hi=0x00000001`, `vat0=0x41377009`, and
`derive_layout_from_cp()` reads color0 presence from VCD_LO bits 13-14 while
`0x200` only sets bit 9 (position). Forcing the layout to 24 right before the
draw (`gxfifo_probe <fifo> at 0x608B 0 24`) makes the stream decode a further
~25 KB, which confirms the diagnosis.

**First real bug fixed (host side):** the decode stall was not the layout at
all -- the decoder rejected a BP `0x52` (EFB copy trigger) because the copy
destination is a **MEM2 physical** address (`0x01328420`, computed from
`EFB_ADDR`), and `gx_resolve()` only mapped MEM1 physical and MEM2
virtual/uncached. Adding the `0x10000000..0x14000000` physical window made the
copy resolve, and draws started flowing:

```
[gx] draw #1 primitive=128 vtx_fmt=7 verts=4 size=6 pn_matrix=0 xform=0x3
...
[dbg] gx: draw packets=280 state=3044 resources=211
```

The remaining desync is the fullscreen quad described above: the game writes
`pos(12) + RGBA8(4) + uv(8)` (24 B) while its captured GX state
(`vcd_lo=0x200`, `vcd_hi=0x1`, `vat0=0x41377009`) declares only position + one
texcoord, so `derive_layout_from_cp()` yields 20 and leaves 12 bytes. Also note
the vendored decoder reads tex0 from `VCD_HI` bits 0-1 and color0 from
`VCD_LO` 13-14, which does not match this title's bit usage -- worth reconciling
against Dolphin's `CPMemory`/`VertexLoader` tables.

**Strong lead on the quad:** decoding the CP stream around the draw shows the
game writing VAT-like values to **CP registers `0x77`/`0x87`/`0x97`**:

```
08 50 00 00 02 00           CP 0x50 (VCD_LO) = 0x00000200
08 60 00 00 00 01           CP 0x60 (VCD_HI) = 0x00000001
10 00 00 10 08 00 00 00 10  XF load base 0x1008 count 1
08 77 40 37 70 06           CP 0x77 = 0x40377006   <- ignored by the decoder
08 87 c8 24 12 09           CP 0x87 = 0xC8241209   <- ignored
08 97 04 82 41 20           CP 0x97 = 0x04824120   <- ignored
```

`dol_gx_recomp_load_cp_reg()` only accepts the canonical `0x70`/`0x80`/`0x90`
VAT registers, so the last VAT for format 0 stays at the earlier init value
`0x41377009` (pos f32x3 + tex f32x2 = 20 bytes), while the vertices written for
the draw are pos + RGBA8 + uv = 24 bytes. Handling the `0x7n`/`0x8n`/`0x9n`
register aliases (or feeding the layout from the game's `GXSetVtxDesc` calls) is
the next concrete fix; the probe (`at <offset> <fmt> <size>`) already confirms
that 24 makes the stream continue.

Note: the vendored decoder already treats `0x70+n`/`0x80+n`/`0x90+n` as
per-format VAT words (`vat_group{0,1,2}[n]`), so the `0x77` write lands on
format 7 and format 0 keeps the early `0x41377009`. The mismatch therefore
comes from the **VCD**, which declares position + one texcoord while the
vertices carry a colour as well; display-list-supplied state is the remaining
place it could come from.

Tracing the trace events at the stall (`COLORS_GX_DECODE=1` now prints the last
`CP_VCD`/`VERTEX_LAYOUT` events) shows **no CP_VCD event near the draw at all** —
the VCD was set once early (`0x200`/`0x1`) and never updated, while the draws in
between use per-format VAT words (`0x70+n`). So the FIFO-derived state simply
does not describe this quad; the authoritative state lives in the SDK's own
`__GXData` block in guest memory (written by `GXSetVtxDesc`/`GXSetVtxAttrFmt`).

`__GXData` **located**: the FIFO helpers load it from `[r2-888]`
(e.g. `0x8036D400: lwz r7,-888(r2)`) and the live value is `0x807FA960` with
`r2=0x808FC5C0`. Its layout around the vertex state:

| offset | field | live value |
|---|---|---|
| `+0x014` | VCD_LO | `0x00000200` |
| `+0x018` | VCD_HI | `0x00000001` |
| `+0x01C + 4*fmt` | VAT group A | `[0]=0x41377009`, `[7]=0x40377006` |
| `+0x03C + 4*fmt` | VAT group B | `0xC8241209` |
| `+0x05C + 4*fmt` | VAT group C | `0x04824120` |

So the SDK's own state matches the FIFO-derived state exactly: the game really
does run that quad with `VCD=position(+tex7 bit)` and `VAT[0]` whose *color0
format is RGBA8* but whose VCD color0 mode is 0. Reconstructing the layout from
`__GXData` (planned) therefore gives the same 20 bytes; the remaining question
is how the title's vertices end up 24 bytes wide, i.e. whether the VCD's
attribute bit positions differ from the decoder's model (the vendored decoder
reads tex0 from VCD_HI bits 0-1, which is where the game's `0x1` lands).

The bit layout was then checked against Dolphin's `CPMemory.h` (`TVtxDesc`):
Position at VCD_LO bits 9-10, Normal 11-12, **Color0 13-14**, Color1 15-16;
Tex0..7 at VCD_HI bits 0..15. That matches the vendored decoder exactly, so the
game's recorded state really does declare only position + tex0 for that draw.
Dolphin also notes that CP `0x50-0x57`/`0x60-0x67` are VCD aliases; the decoder
now accepts all of them (fidelity fix), but the captured stream only uses
`0x50`/`0x60`, so this did not change the outcome.

The probe can now inject a VCD mid-stream (`gxfifo_probe <fifo> vcd <offset>
<lo> <hi>`). Injecting `VCD_LO=0x2200` (i.e. adding the Color0 = Direct bits)
right before that draw gives **`layout0=24`**, and the stream then decodes past
the quad (the next failure is much later). This confirms both the decoder and
the diagnosis: the only missing piece is the color0 mode bit in the VCD that is
in effect for that draw (`0x200` observed, `0x2200` needed).

Scanning the whole capture shows the game *does* use `VCD_LO=0x2200` (four
times, early: `0x9A0`, `0xA70`, `0xAD8`, `0xB40`) and switches back to `0x200`
at `0x5E3F`, immediately before the quad. So the title toggles Colour0 and the
quad is emitted while it is disabled, i.e. the FIFO stream and the vertex
payload disagree for this one draw.

Remaining work on it: hook the SDK entry that writes `__GXData+0x14`
(`GXSetVtxDesc`) and log every VCD update with its guest caller, then correlate
with the quad's draw. The practical alternative is a decoder tolerance: when a
draw's declared layout leaves the parser at an invalid opcode, retry with the
same VAT plus the attributes whose formats are already programmed (here
Colour0/RGBA8) — the capture then decodes end-to-end, which is what a
rasteriser needs.

The host now also **recovers** from a desync: if the decoder's pending buffer
exceeds 64 KB it is reset and decoding resumes (logged as `[gx] resync`), so a
long run keeps producing packets instead of buffering forever. Measured on a
100M-block run with the font bypass:

```
fifo_writes=193849  draws=412  state=3817  resources=285  flush_fail=30330
pending=53033  resync=5
```

Then the capture decodes end-to-end and draw packets become the input for a
rasteriser.

`gxfifo_probe` gained two modes used above: whole-stream replay (absolute error
offset + last trace events + VCD state) and `at <offset> <fmt> <size>` (feed a
prefix, override one layout, continue).

Next for GX: fix that desync (probe until the whole 100 KB capture replays
cleanly), then draws flow and the packet stream becomes the input for a
rasteriser.

### Frame loop reached (this round)

After the speedup the boot sequence can be followed for seconds of emulated
time. The output of that is a **working 60 fps frame loop**:

- 300M blocks ≈ 6.5 s wall clock, ~2.5 s emulated, retrace counter at
  `0xEC` (236 frames).
- The main thread alternates between two waits, as a frame loop should:
  - `VIWaitForRetrace` (`0x80353360`, counter `[r13-22868]`, queue
    `[r13-22904]` → `0x808F3BA8`), and
  - the draw-done wait (`FUN_80369F40`, flag `[r13-22280]`, queue
    `[r13-22288]` → `0x808F3E10`).
- No further disc I/O after the initial asset batch, i.e. the game is rendering
  / waiting on presentation rather than loading.

`COLORS_NO_GX_WAIT_HLE=1` restores the interrupt-driven draw-done wait; by
default the host satisfies the flag before the loop ("the GPU always
finishes"), which is the same assumption as the per-frame PE_FINISH model.

### Boot-path findings after the speedup (this round)

With the game now running at ~0.4x real time, its boot sequence could be
followed for seconds of emulated time. Four issues were found and three fixed:

1. **Phantom USB device (fixed).** `ios.c` allowed any `/dev/usb/...` path to
   open, so the game's USB driver enumerated `/dev/usb/oh1/57e/305` and polled a
   nonexistent device forever (one write every ~0.6 ms). Device nodes now fail
   with `-6 (ENOENT)`; only the controller node `/dev/usb` opens.
2. **AID interrupt storm (fixed).** `interrupt_assert_audio_dma()` now requires
   the SDK handler for interrupt id 5 to be installed (same guard as the
   PE_FINISH path) and drops the source otherwise. Without this the DSP cause
   latched with nobody to ack it and the CPU spun in `__OSDispatchInterrupt`.
3. **Font wait semantics (diagnosed).** `0x806E6C54` is
   `while (flag != 0) sleep(100)`, i.e. `[r13-25224]` is "still loading"
   (the emitter's `bc 4,2` branches when EQ is clear). The default run parks
   here because the **system font cannot be loaded**: the font thread's file
   open returns `-6` (we serve no NAND). `COLORS_CLEAR_FONTFLAG=1` now sets the
   flag to 0 (ready) and lets boot continue.
4. **HBM module is dynamic code (limitation, handled).** With the font wait
   bypassed the game loads the Home Button Menu RSO into MEM2 and calls into
   it (`pc=0x9012E82C`, `lr=0x80016B04`) — code that a DOL-only static
   recompilation cannot execute. The host now reports the module as unavailable
   (`GetHbmModule` result forced to 0 at `0x80016868`/`0x800169A4`), which is
   the game's own graceful path; `COLORS_HBM_MODULE=1` keeps the real module
   for future interpreter/JIT work.

Captured boot log (OSReport HLE) with `COLORS_CLEAR_FONTFLAG=1`: asset loads
continue deep into the CPK (`0xB99C1300`, `0xB99D4AC0`, `0xB99E6AC0`, ...),
i.e. the content path is healthy.

After those fixes the game runs its frame loop (236 retraces) and then parks in
the GX draw-done wait described below — the remaining scheduling question.

### FIXED: build optimisation (the real throughput bottleneck)

The CMake default was `Debug` (no `-O`), and at `-O0` GCC compiles each chunk's
~4096-case `switch (ctx->pc)` as a **linear compare chain**:

```
; -O0
cmp $0x800153bc,%eax ; je ... ; cmp $0x800153bc,%eax ; ja ...   (thousands)
; -O2
mov 0x280(%rdi),%eax ; add $0x7ffeec40,%eax ; cmp $0x3ffc,%eax
ja ... ; lea ...(%rip),%rdx ; movslq (%rdx,%rax,4),%rax          (jump table)
```

`add_compile_options(-O2 -fno-strict-aliasing)` is now applied to the whole
target (the emitter reinterprets guest memory through several pointer types, so
strict aliasing is disabled deliberately). Results, verified identical final
state (`pc=0x80343F9C`, same audio counters) on the same workload:

| run | before | after |
|---|---|---|
| 20M blocks | 11.2 s | **0.76 s** (~15x) |
| 1B blocks | — | **32.7 s** |

Clean build cost rises to ~8 minutes (450 TUs at -O2, 16 cores).

### Current state after this round

```
build   : clean, -Wall -Wextra clean; -O2, 300 MB binary
speed   : ~26M blocks/s; 1B blocks = 776 frames (~13 s emulated) in 33 s,
          i.e. ~2.5x slower than real time
game    : boots, CPK parsed, first asset batch loaded (4 reads), 60 fps
          retrace counter advancing (VI DI0=0x1107)
stop    : boot sequence blocks in a GX draw-done wait
```

The remaining block is the GX draw-done wait in `FUN_80369F40` (Wii EFB FIFO
writes to `0xCC008000`, then `0x80338840`, clear `[r13-22280]`, sleep on queue
`[r13-22288]`). The PE_FINISH handler (id 19 = `0x8036A290`) runs hundreds of
times and does set `[r13-22280] = 1` and call `OSWakeupThread([r13-22288])`,
yet the main thread stays in state 4 at `0x80343240`. Observations to start
from next time:

- The wait is only *entered* as a dispatch entry after a sleep completes
  (`0x80369FAC`); the first check and the sleep happen inside one recompiled
  block, so per-block probes cannot see the flag at the decision point.
- The thread's backchain is unreliable here: stale frames from earlier calls
  can appear, so treat `[0x80369FAC]` in the chain as a hint, not proof.
- Worth checking next: whether `OSWaitThread` (`0x80343E90`) actually enqueues
  the thread (queue head/tail read back as 0 while the thread is state 4), and
  whether `OSWakeupThread` sees it; that would explain a lost wakeup.

### O(1) chunk dispatch (throughput)

`dolrecomp_call()` is emitted as a chain of 450 range comparisons, so a block
near the end of the chain paid ~450 tests per dispatch. Since the chunks are
contiguous 0x4000-byte windows (one small exception at `0x80004000`), the host
now dispatches through a generated direct-mapped table:

- `tools/gen_chunk_table.py` parses `recompiled/generated/generated.h` and emits
  `src/chunk_table.c` (`chunk_dispatch()` / `chunk_contains()`), regenerated by
  a CMake custom command whenever `generated.h` changes.
- `main.c` calls `chunk_dispatch()` instead of `dolrecomp_call()`; the table
  keeps the `ppc_host_call()` hook first and the same word-alignment rule.

Verified behaviour-identical: 20M blocks give the exact same final state
(`pc=0x80343F9C`, `lr`, `r1`, AID counts 13/18) before and after, and the same
workload drops from 13.6 s to 11.2 s (~18%; more in code that lives far down
the old chain).

### VI retrace now locked to guest time

`interrupt_poll` used to advance the retrace clock by *dispatched block counts*
(`COLORS_FRAME_BLOCKS`, 350k per frame), which produced ~1100 "frames" per
emulated second. It now advances by **elapsed timebase ticks**
(40.5 MHz / 60 = 675,000 per frame), so frames match hardware timing. The debug
`COLORS_FRAME_BLOCKS` override now means ticks per retrace.

Side effect: the game advances 20x fewer frames per block budget, so a run of
80M dispatches covers only ~0.2 s of emulated time (≈11 frames). Throughput is
therefore the next practical concern: the host loop re-enters the dispatcher for
every basic block. Batching with `dolrecomp_run_blocks()` (e.g. 32 blocks per
host iteration, falling back to the translating loop when a chunk is missed)
should give a large speedup without changing guest behaviour.

### OSReport HLE (new debugging surface)

`0x8033BFB0` is the SDK's formatted report (`crxor 6,6,6` + `bl` at every call
site). It was previously invisible, which is why the CriWare/filesystem
failures were silent. `COLORS_OSREPORT=1` now routes it to stderr (with the
guest caller in parentheses) using a small `%d/%u/%x/%s/%c/%p/%f` formatter.

The boot log this exposed (first 14 lines) confirms the fixed memory map:

```
MEM1 Arena : 0x8091dc20 - 0x817fe840
MEM2 Arena : 0x90000800 - 0x933e0000
MEM1 Heap  : 0x80965c20 - 0x817fe840  14.6 MB
MEM2 Heap  : 0x9012c800 - 0x933e0000  50.7 MB
NintendoWare Heap : 0x80965c40 - 0x80ae5c40  1.5 MB
Failed to set home directory.        (0x803986A4, non-fatal)
```

The last stop is the Metrowerks **RSO/module linker** (`FUN_803bcfb0`, the
"LOSLink" relocation engine) walking the module at `0x9012C860` with a bogus
section count `0x6D056AF9`. The module pointer comes from
`GetHbmModule` (`0x800168BC`) → `RSOObjectInit` (`0x803BDA60`).

Evidence gathered with the new `COLORS_TRACE_IO` trace:

- Only **two** DI reads happen in the whole run, both served from
  `movie/ev150.sfd`'s disc range:
  `len=0x800 off=0x2E55F4A0` and `len=0x1CE0 off=0x2E55ED68`.
- The second read's destination is `0x1012C860` (physical) = `0x9012C860`,
  i.e. MEM2 heap base + 0x60, and the buffer content is byte-identical to
  `ev150.sfd` at `0x2E55ED68` (verified against the host file).
- `0x2E55ED68` is exactly a **CPK-relative** offset, not a disc offset: the CPK
  is at FST offset `0xB957D280`, so a real read of CPK+`0x2E55ED68` would be at
  disc `0xE7AD...`.
- The game opens `/dev/fs` but issues no ioctls on it, and only one unhandled
  DI ioctl (`0x12`, 32-byte out buffer, left untouched) precedes the disc ID
  query.

Conclusion: the game's file layer believes the CPK starts at **disc offset 0**,
so CriWare reads return unrelated disc data; the module loader then relocates
that data and walks a 1.8-billion-entry table. The next probe is how the CPK's
base offset is obtained (the DI ioctl `0x12` response and/or the CriFs binder
setup), followed by implementing whatever NAND/ISFS plumbing "set home
directory" needs.

### Host split into modules (behaviour-neutral)

`src/main.c` (1561 lines: MMIO models + IOS service + boot setup + watches + run
loop + report) was split into the modules listed under **Host source layout**.
Details worth knowing:

- `dbg.c` now owns the PC ring buffer, every per-block watch, and the final
  `[run]`/`[dbg]` state dump. `main.c` calls `dbg_pc_push()` +
  `dbg_block_enter()` at exactly the same point in the loop, so all watch
  output (order and content) is unchanged.
- The temporary HLE shortcuts (`COLORS_NO_AUDIO_HLE`,
  `COLORS_NO_DSP_TASK_HLE`, `COLORS_CLEAR_FONTFLAG`) live in `dbg.c` next to the
  watches that trigger them, clearly marked.
- `mmio.c` kept the dispatch order but dropped a dead duplicate PI-FIFO check
  (the first one always matched, so behaviour is identical).
- `boot_env.c` performs the apploader/OS reconstruction in the original order:
  OS globals -> disc ID at `0x80000000` -> arena bounds -> FST at
  `0x817FE840` -> PI FIFO seeding.
- `mem2_read/write` only truncate at the extreme end of MEM2 instead of
  guarding every access, so valid accesses are byte-identical.

Verification after the split (default args, no env flags):

```
[disc] FST: 1052 bytes, 44 regions mapped
[boot] FST image at 0x817FE840 (1052 bytes)
... identical [obj6A60]/[desc]/[vcall] trace ...
[run] stopped: pc left recompiled code (no handler)
[run] blocks executed : 7250343
[run] final pc         : 0x80000000
[run] link register    : 0x800129BC
[run] exception        : none
```

i.e. the same stop point as before the split, with no new warnings.

### Remaining (toward fully runnable)

0. **GX draw packets, then a rasteriser**: the FIFO decoder is wired and
   decodes state; draws need the vertex-layout calls
   (`set_vertex_layout`/`set_indexed_attr`/`derive_vertex_layout`) issued when
   VCD/VAT change, plus finding the command that stalls the parser (pending
   buffer growth). After that, either build Aurora (needs Dawn/SDL3 downloads
   and a GPU) or add a software rasteriser behind the packet callback. Audio
   output (`DolAudioDma` -> host) is the other half of "playable".
1. **System font + HBM**: the default run parks on the NAND system font
   (`[r13-25224]` stays "loading"; the font open returns `-6`). Options: serve a
   real NAND font image, or HLE the font-open path. The HBM module additionally
   needs dynamic-code execution (interpreter/JIT) if the Home Menu is wanted.
1. **GX draw-done wakeup** (current blocker once the font wait is bypassed): the boot sequence sleeps on
   `[r13-22288]` waiting for `[r13-22280]`, the flag the PE_FINISH handler
   (`0x8036A290`) sets. The handler runs, so check whether `OSWaitThread`
   (`0x80343E90`) actually enqueues the thread (queue head/tail read 0 while
   state is 4) and whether `OSWakeupThread` then finds it. If the enqueue is
   the problem, the wait is inside a block with interrupts disabled and the
   pending PE_FINISH is delivered after the thread parks, i.e. a lost wakeup
   that a level-triggered flag alone cannot fix.
2. **Memory accessors** (further speed): `mem_read32`/`mem_write32`/... are
   out-of-line calls through `translate_addr`; an inline MEM1 fast path in the
   runtime header would remove the dominant per-access cost.
2. **Sound init command** (`0x1005`): replace the remaining audio-init shortcut
   (`COLORS_NO_AUDIO_HLE`) with the real message flow through `0x806EFE00`.
3. **`DolAudioDma`**: swap the hand-written AI/DSP register model for the
   DolRuntime `DolAudioDma` device so AID timing/masks come from the shared
   model instead of the frame-cadence approximation.
4. **GX graphics**: `PE_FINISH` is currently a per-frame draw-done stub; real
   rendering needs a GX command-processor backend.


### Debug flags (host)

- `--mmio-log` — MMIO/IPC/DSP/AI/device traffic (`[ios]` request decoding, the
  Starlet status-bit handshake, `[dsp]` mailbox exchanges, `[ios] di read`
  results). **Do not redirect long runs to a file** (a 120M-block run produced a
  7.4 GB log and filled `/tmp`).
- `COLORS_TRACE_EXC=1` — every external/decrementer exception delivery.
- `COLORS_CHECK_SP=1` — stop at the first block that leaves a non-RAM `r1`.
- `COLORS_TRACE_HALT=1` — `PPCHalt` first entry + the one-shot PC traces
  (DVD state machine, sound module, DI/ReadDiskID sends).
- `COLORS_TRACE_AX=1` — watch the sound state byte (`0x808E9866`) and the AX busy
  flag (`[r13-22568]`) transitions with PCs.
- `COLORS_NO_AUDIO_HLE=1` — disable the audio-init shortcut (state byte,
  `__AIInit` effects, AX callback registration).
- `COLORS_CLEAR_FONTFLAG=1` — clear the message/font wait flag (diagnostic).
- `COLORS_TRACE_REF=1` — smart-pointer/reference-block lifetime, MEM pool
  creation, texture-buffer allocation, and the legacy heap/object watches.
- `COLORS_TRACE_IDLE=1` — first idle-thread entry with thread states, the
  interrupt-dispatch PI-cause trace, AI/AID handler hits, and the current
  block-counted spin (`0x803BD094`).
- `COLORS_TRACE_IRQ=1` — PI cause/mask register reads and writes.
- `COLORS_CLAMP_COPY=1` — clamp absurd memcpy/cache-op lengths (diagnostic).
- `COLORS_OSREPORT=1` — route the SDK's `OSReport` (`0x8033BFB0`) to stderr with
  the guest caller (`[os] (lr) message`); capped at 4000 lines.
- `COLORS_TRACE_IO=1` — every IOS request (type, fd, arg, in/out buffers,
  opened path) plus decoded DI read command blocks (`[di] cmd=...`).
- `COLORS_TRACE_FILES=1` — game-side `open` wrapper / HBM module request sites
  (`[file]`).
- `COLORS_CLEAR_FONTFLAG=1` — treat the system-font load as complete (the font
  lives on NAND, which we do not serve); lets boot proceed past the font wait.
- `COLORS_HBM_MODULE=1` — keep the real Home Button Menu RSO result instead of
  reporting the module unavailable (its code is MEM2 dynamic code and cannot be
  executed without an interpreter/JIT).
- `COLORS_GX_DECODE=1` — route the GX FIFO (`0xCC008000`) through DolRuntime's
  headless frontend and report decoded state/draw packets. Off by default.
- `COLORS_NO_DSP_TASK_HLE=1` — disable the DSP task-completion HLE.
- `COLORS_DBG_FONT=1` — dump the message/font + DMA-pool state at the stop.
- `COLORS_FRAME_BLOCKS=N` — override the VI retrace period in timebase ticks
  (default 675,000 = 40.5 MHz / 60 Hz).

### Reproduce

```
cmake --build build -j16
./build/ColorsRecomp game/sys/main.dol              # default: full run
./build/ColorsRecomp game/sys/main.dol --max-blocks 200000000
COLORS_TRACE_IDLE=1 ./build/ColorsRecomp game/sys/main.dol   # thread/IRQ view
./build/ColorsRecomp game/sys/main.dol --mmio-log | grep '\[ios\]'
```

### FIFO staging cadence and a latent truncation bug

The decoder needs a run of FIFO bytes to reassemble commands and to validate a
draw's vertex stride, so `gx_flush()` moved from *every block* to every 0x4000
blocks (staging still flushes whenever its buffer fills). That exposed a latent
bug in `gx_fifo_write`: when the stage filled in the middle of a 32/64-bit FIFO
write it flushed and `break`-ed out of the byte loop, **dropping the rest of
the write**. With per-block flushes the stage never reached its limit, so the
loss never happened; with a coarse cadence it corrupted the stream immediately
(draws fell to zero). The loop now flushes *before* storing a byte, so straddling
writes are preserved.

The stride recovery is also incremental-data aware: `stream_check` reports
`kTruncated` when there are not yet enough following bytes to judge a layout,
and a FIFO draw is then left pending (retried on the next flush) instead of
being committed with a stride that cannot be validated.

## Software rasteriser (first light)

`src/raster.c` + `src/raster.h` now consume the decoded draw packets and
rasterise them on the host, with `src/texdecode.cpp` wrapping gxcore's
headless, Dolphin-conformant texture decoder (`decode_texture`, `decode_ci`,
`decode_tlut`).

What it implements:

- **Vertex decode** from the packet's `vtx_fmt` using the CP VCD/VAT state
  packets (position/normal/colour/tex0 attributes, direct and indexed via the
  CP array base/stride packets). If a draw's real stride is wider than the
  declared VCD layout — the same retail quirk the frontend recovers from — the
  layout is re-derived with colour0 forced on, which reproduces the observed
  `pos f32x3 + RGBA8 + uv f32x2` vertices exactly.
- **Transform** with the guest's own position matrix, projection and viewport
  (perspective and orthographic), reversed-Z depth, top-left-origin EFB.
- **Rasterisation**: quads, triangles, strips, fans; edge functions, depth
  test, perspective-correct UVs.
- **Textures**: gxcore's tiled GX formats (I4/I8/IA4/IA8/RGB565/RGB5A3/RGBA8 and
  C4/C8/C14X2/CMPR through a TLUT), wrap/clamp/mirror from BP 0xA0.
- **EFB copies** (`COPY_DESTINATION` packets): the copied EFB rectangle is kept
  as an RGBA8 image keyed by destination address and served when the game binds
  that address as a texture. That covers render-to-texture and mip generation
  without re-encoding GX's tiled memory layout.
- **Present**: `COLORS_FB_DUMP=<path>` writes the EFB as a binary PPM just
  before a display copy clears it (`copy_clear`), and at exit if no display copy
  was seen. `COLORS_FB_PRE=<path>` snapshots the EFB just before the full-screen
  blit, and `COLORS_TEX_DUMP=<dir>` writes decoded textures (great for telling
  a wrong texture apart from a wrong transform).

### The 342-pixel bug

The first frames came out empty. The cause was the **GX window-coordinate
bias**: the viewport origin registers (XF 0x101A/B) carry the +342 bias and
Dolphin compensates with `xOrig - bpmem.scissorOffset * 2`. BP 0x59 here holds
`171`, i.e. half the bias, so the correction is `2 * 171 = 342`:

```
x = ndc.x * wd + (xorig - 2*scissorOffset)
y = EFB_H - (yorig - 2*scissorOffset) + ht * ndc.y
```

Getting this wrong shifted every quad 171 px right and up (the first version
used the BP field as-is). With it right, the full-screen quad maps exactly to
`(0,0)..(640,480)` and the mip quads to `(0,0)`, `(160,0)`, `(240,0)`,
`(280,0)`. `COLORS_RASTER_WHITE_TEX=1` makes the sampler return white, which
turns the pre-blit EFB into a coverage map: it shows the mip rectangles and the
±1 px blur passes drawn over them, which is exactly what the guest submits.

### The 256 KB FIFO recording cap (the real reason there was no scene)

The frontend keeps a raw copy of the decoded FIFO bytes for `.dolt` output in
`DolGxRecompState.fifo` (256 KB). A live emulator never drains it, so
`dol_gx_recomp_push_fifo` starts failing once 256 KB have been parsed — and
because every `parse_stream` call pushes the bytes again, *every subsequent
flush fails*, silently and forever. That is why no scene geometry ever
appeared: the decoder simply stopped after ~17M blocks.

`dol_aurora_recomp_frontend_set_record_fifo(frontend, false)` turns the
recording off for the live path. Note the parameter order of `parse_stream`:
`(bytes, allow_partial, record_fifo_bytes, depth, consumed)` — the first patch
disabled `allow_partial` instead, which also disabled the incremental hold /
stride recovery. With both correct:

```
before: draws=345  state=3543  resources=249  flush_fail=296  skip=296
after : draws=707  state=6960  resources=525  flush_fail=0    skip=0
```

### Stride recovery must be per-draw

The recovered vertex stride is applied by temporarily rewriting the shared
`vertex_layouts[vtx_fmt].vertex_size` around the `handle_draw` call and
restoring it afterwards. Persisting it corrupted every later draw of that
format (the game mixes 20-byte `pos+uv` and 24-byte `pos+colour+uv` vertices in
the same format).

### Reversed-Z depth compare is LEQUAL

Both triangles of the game's flat 2D quads sit at `z = 1.0`. A strict
`z > depth` test drops the second triangle, which showed up as exactly half of
the safety screen. GX's default ZMode is LEQUAL, so equal depths must pass.

### First real game frame

With those fixed the renderer draws the console's **Wii Remote wrist-strap
health-and-safety screen** (a CMPR 640x480 texture decoded by gxcore, drawn as
a two-triangle strip over a bloom pass). `COLORS_FB_DUMP` captures it. The
remaining visual gap is TEV blending: the bloom composite is currently a plain
`vertex * texel` modulate, so its dark texture paints over the white background
instead of being added.

### After the saves: the movie is decoding

With the save store in place the next frames bind a **new texture at
`0x011EDEA0`, 640x480 RGBA8** — the CRI Sofdec decoder writes decoded frames
into MEM1 and the game samples them. Frame work also spreads out across the
decoder (`0x803875B0/0x803875EC`, `0x803BE770`, `0x803C1924`) instead of a
single spin, so the movie path is genuinely running.

The presented frame is still white: the final composite is the same bloom chain
over a white base, so the movie quad either is not reaching the last composite
or the fade never retracts. That is now a rendering/compositing question rather
than a game-logic stall.

### Confirming the converter bug (not stale state)

The Y plane is already flat (`Y=30`, `U=V=128`) at **2.7B** blocks, while the
destination buffer is still all zeros there (first content write lands ~5.1B).
At 5.2B the planes are *still* flat and the destination holds the magenta/grey
8-pixel stripes — so a flat YUV frame was converted into a striped RGBA image.
That rules out "stale destination" and pins the bug on the conversion.

Inspecting the emitted code for that routine, `dcbz`, `lfsx`, `stfsx` and the
`ps_merge00/01/10/11` lane moves all look correct (`ps1[]` is the second
single, matching IBM's merge encoding). The remaining suspects are the
paired-single arithmetic (`ps_add`) or the LUT base/index setup, which is where
the next probe should go. DolRecomp ships PS cross-check tests
(`tools/DolRecomp/tests/dolphin`) that can validate the PS path directly.

### The decoder output is flat — the stripes come from the converter

Hooking the converter entry (`0x8063AFF8`/`0x8063B00C`) shows its input structs:

```
src a: +0x00 format=3   +0x04 Y  = 0x92D31C20 (640x480)
       +0x10            +0x14 U  = 0x92D7CC20 (320x240)
       +0x20            +0x24 V  = 0x92D8F820 (320x240)
dst b: +0x04 = 0x811EDEA0 (640x480 RGBA)
```

so it is planar **YUV420**. Dumping the planes at 5.2B blocks
(`COLORS_GRAYDUMP`) gives:

```
Y = 30 everywhere   (640x480)
U = 128 everywhere  (320x240)
V = 128 everywhere  (320x240)
```

A flat neutral-grey frame. Yet the destination buffer is the alternating
magenta/grey 8-pixel stripe pattern. **Flat input cannot produce a striped
output**, so the conversion itself is being emulated incorrectly — the
candidates are the paired-single math (`ps_merge11`, `ps_add`), the LUT index
computation, or the write stride (`stfsx`). The 8-pixel period is exactly one
32-byte `dcbz` block, which is a strong hint that the `dcbz`/store interleave
is off.

That also reframes the earlier "decoder failure": the decoder output is a
blank grey frame (consistent with the stream stalling early), and the stripes
are a separate converter-emulation bug that will still need fixing once video
decoding works.

### The movie buffer is filled by a YUV->RGB converter

Filtering the write journal to non-`memset` writers points at `0x8063B118`
(caller `0x8063B020`), inside a routine at `0x8063B00C` that:

- sets up GQRs (`mfgqr4`/`mtgqr4`) and paired-single constants,
- `dcbz`-allocates two destination lines,
- reads 16-bit source planes and looks up four tables at `base+0/1024/2048/3072/4096`,
- combines with `ps_merge11` / `ps_add` and writes floats with `stfsx`.

That is a YUV->RGBA conversion with bilinear upscaling. So the magenta/dark
stripe pattern is the converter faithfully rendering its **YUV input**, and the
question moves upstream: either the MPEG decoder emits a test pattern (its
error fallback) or its YUV output is wrong.

### Who writes the movie buffer

`COLORS_WATCH_WRITE=<lo>:<hi>` installs DolRuntime's memory-write journal and
reports the first content writes into a guest range. The movie buffer is first
zeroed by the SDK's `dcbz` block-clear (`0x8033B190`), then filled through
`memset` (`0x80004714`, loop at `0x800046B8`, caller `0x8000472C`). The result,
read back as RGBA8, is an **8-pixel-wide magenta/dark stripe pattern** across
the whole 640x480 area — the shape of a test/placeholder fill (or of a planar
YUV buffer read as RGBA), not a decoded frame. The player is therefore failing
its decode and substituting that pattern.

### The movie buffer content

`COLORS_MEMDUMP` writes the 640x480 RGBA buffer the movie quad samples. Running
past 5B blocks shows it is not empty any more, but it holds a **magenta/black
vertical stripe pattern** (two colours, 8-pixel stripes) — a failure/test fill,
not a decoded frame. Combined with the three reads and the constant work in the
MPEG bitstream code, the player is failing to produce video and paints a
placeholder instead.

### What the SFD stream looks like on the wire

The three movie reads contain valid **MPEG-PS** data, not a CRI container:

```
[di] #22 off=0xB8FF5548 len=0x80000 first=00 00 01 BA 21 00 01 00
[di] #23 off=0xB9075548 len=0x36800 first=00 00 01 BA 21 00 01 1B
[di] #24 off=0xB90ABD48 len=0x64000 first=00 00 01 BA 21 00 01 27
```

(`00 00 01 BA` is the MPEG pack header start code.) So the file named `.sfd`
is an MPEG program stream, the data is fetched correctly, and the player reads
three chunks (1.2 MB) at retrace 1282-1284 before going quiet.

No other DI command is issued (`di_mmio` writes stay 0; the only unknown ioctl
is `0x12`, a drive inquiry), and the frame buffer at `0x011EDEA0` stays **all
zeros**, so the stall is inside the player's decode/sync path rather than in the
disc layer. The main thread keeps busy in the MPEG bitstream code
(`0x803875EC`) and the frame loop continues.

### Movie quad layout and the SFD stall

The Sofdec logo frame is drawn by a 24-byte-vertex quad
(`fmt=0, prim=0x98, vs=24`) sampling `0x011EDEA0`. Its VCD/VAT is stale: it
declares an s16 position, while the payload is
`pos f32x3 + RGBA8 + uv f32x2`. With the declared layout the transform produced
`bbox=(-15136,240)-(17632,240)` — nothing on screen. The rasteriser now has a
size-based fallback (`layout_from_size`) used whenever the declared layout's
size disagrees with the payload stride; it covers 12/16/20/24-byte shapes
(`pos f32x3`, +RGBA8, +uv, and both). The quad then lands correctly
(`v0=(0,60)`, the 640x480 image letterboxed between y=60 and y=420).

MEM1-backed textures are also re-decoded on every bind, because the Sofdec
decoder rewrites the same buffer each frame (caching by address alone kept the
first decode forever).

What remains is the decoder itself: `COLORS_MEMCHECK=0x011EDEA0` shows the frame
buffer is **all zeros** from 2.5B to 3B blocks, and the player issues only three
disc reads (`0x80000`, `0x36800`, `0x64000` at retrace 1282-1284). So playback
stalls before the first decoded frame — the next thing to chase.

### Rasteriser fast paths

Two TEV configurations cover almost every full-screen pass (bloom composites,
the fade quad). They are now detected once per draw and skip the generic
combiner:

- **texture pass-through** (`c = {ZERO,TEXC,-,-}`, `a = {ZERO,TEXA,-,-}`) →
  `out = texel`,
- **raster pass-through** (`c = {ZERO,ZERO,ZERO,RASC}`, `a` likewise) →
  `out = vertex colour`.

Measured on the SDL2 window: 61 frames / 8 s before, 74 frames / 8 s after
(~+20%); the 200M-block frame renders identically (2784 colours).

### Window output (SDL2)

`src/window.c` presents the software rasteriser's EFB in a host window:

- `--window` (default when a display is available), `--headless` to disable,
  `--scale N` for integer scaling (default 2x), or `COLORS_HEADLESS=1`.
- Window mode implies `COLORS_GX_DECODE` (feeds the rasteriser) and
  `COLORS_CLEAR_FONTFLAG` (the NAND system font is absent), so the game runs
  unattended.
- Every completed frame is presented at the display copy, before the EFB
  clear — the same image `COLORS_FB_DUMP` writes.
- The title bar shows the frame counter and host frame rate; Esc / Ctrl+Q /
  closing the window stops the run (`[run] stopped: window closed`).
- Without an explicit `--max-blocks`, window mode runs until the window closes.

Build integration is optional: CMake looks for SDL2 with pkg-config and
compiles a no-op stub when it is missing, so headless builds are unchanged.

Measured: ~20-30 guest frames/s headless, ~5 frames/s with the window at 2x
(software rasterising ~1.3M pixels per frame including the bloom chain).

### The save-system stall (fixed) — this is what gated the next scene

The game retries `/tmp/system.sav`, `/tmp/game_0..5.sav` and `/tmp/banner.bin`
**every frame** (246 opens each by 3B blocks) because the NAND path returns
ENOENT. `src/ios.c` now backs `/tmp/*` with host memory:

- `ios_store_open()` creates/finds a per-path buffer (opt out with
  `COLORS_NO_SAVE_STORE`),
- the save I/O uses IPC type 4 with the buffer at `+12` and the length at
  `+16` (an earlier write path used `+20`, which silently stored nothing),
- close keeps the backing data, so a re-open sees the saved image.

With the store in place the game finishes the save init and, for the first
time, loads the next scene:

```
[di] #19 retrace=1277 off=0xB9A39FA0 len=0x266A0 -> sonic2010_0.cpk
[di] #20 retrace=1277 off=0xB9B68FA0 len=0x9B80  -> sonic2010_0.cpk
[di] #21 retrace=1277 off=0xB9B72B20 len=0x62620 -> sonic2010_0.cpk
[di] #22 retrace=1282 off=0xB8FF5548 len=0x80000 -> movie/segalogo_jp.sfd
[di] #23 retrace=1282 off=0xB9075548 len=0x36800 -> movie/segalogo_jp.sfd
[di] #24 retrace=1284 off=0xB90ABD48 len=0x64000 -> movie/segalogo_jp.sfd
```

That is the SEGA logo movie streaming from the disc — the first game-side
progress past the white fade. Playback then holds after three reads (the SFD
player stops requesting data), which is the next thing to chase.

### The fade timeline (COLORS_BRIGHTNESS)

Sampling the EFB at every present (mean, max, dark-pixel count) gives the
game's actual progression:

```
frame 1      mean 0     dark 307200   (cleared)
frame 41     mean 242   dark 7339     safety screen page 1
frame 361    mean 240   dark 6350     safety screen page 2
frame 1241   mean 246   dark 4107     fading out
frame 1271   mean 255   dark 0        pure white — holds forever
```

So the safety screens really do play through (each page holds ~300 frames) and
end with a deliberate **fade to white**, after which the next scene never
draws. At that point:

- main thread: `VIWaitForRetrace` → normal 60 Hz loop, still executing game and
  CriWare code each frame (104 KB MEM2→MEM1 message copy),
- four worker threads: idle in the task pool,
- one more thread: idle in the same pool,
- **no thread is blocked in the CriWare module**, so the white screen is the
  game's own state, not a deadlock in our scheduler.

### Frame composition and the text pass

`COLORS_FRAME_LOG=1` keeps a 48-entry ring of recent draws and dumps it at each
present. In the post-safety-screen state a frame is: the bloom chain (mip
quads at 160x120/80x60/40x30/20x15 with ±1 px blur offsets), two full-screen
composites (EFB copy, then the 160x120 bloom), and finally a **text pass**:
draws with `vertex_size` 3-5 whose vertices are indexed (CP arrays) glyph
quads sampling a font atlas at `0x10B9ABC0`.

Those text draws currently render nothing because their transform collapses:

- `matrix_index_a = 0x3CF3CF00` → PN row 0 → matrix 0,
- but all ten captured position matrices are **all zero**
  (`position_matrix_word_mask = 0xFFF`, values 0.0),
- with a perspective projection `w = -z = 0`, so every text vertex is clipped.

Earlier frames (the safety screens) transform correctly, so the matrices are
zeroed *later* in the run. Follow-up instrumentation (`COLORS_XF_DEBUG`) shows
the game loads matrices **only through direct XF loads** (`0x10`, base 0,
count 12, first row `3F800000 ...` = identity) — the earlier suspicion of an
uncaptured *indexed* XF path was an artifact of a damaged parser, not the
guest. Combined with `colour_update = 0` on the text draws, the text pass is
most likely a measurement/no-op pass: nothing is meant to be written, so the
zero matrix is not the missing "press A" prompt.

(The parser damage mentioned above came from an over-broad edit and was
repaired; the restored tree reproduces the exact previous counts,
`draws=10600 state=94776 resources=7856` at 200M and
`draws=275341 state=1904269 resources=459974` at 3.2B with `flush_fail=0`.)

### The second 256 KB cap: GXCallDisplayList

Disabling FIFO recording covered the streaming path, but the `CALL_DL` branch
pushed the display list into the same 256 KB recording buffer
*unconditionally*. After enough `GXCallDisplayList` calls it filled and **every**
subsequent display list failed (`FIFO recording buffer full`, opcode 0x40),
which desynced the stream and ended rendering. That is what stalled the run at
~2.5B blocks. The push is now gated on `record_fifo_bytes` like the rest:

```
before: draws=95538   (frozen after ~2.5B blocks)
after : draws=275341  state=2251385  resources=459974  (still drawing at 3.2B)
```

### The 8192-event trace cap (why the game "stopped rendering")

The frontend records every decoded event into a fixed `trace[8192]` array and
`trace_event()` silently drops anything past the end. The host emits packets by
walking that array from `first_event`, so once it filled (state 6960 + draws 707
+ resources 525 = **8192 exactly**) the game kept writing the FIFO and kept
being decoded, but **no further packets reached the renderer**. That was the
whole "the game stops drawing after 12 frames" mystery — not the guest.

`RetailGxFrontend::flush` now rewinds the trace after a live sink consumed it
(`trace_count = 0`). Result over 200M blocks:

```
before: draws=707     state=6960     resources=525
after : draws=10600   state=94776    resources=7856   clears=153
```

### TEV + ROP

The rasteriser now runs the real pixel pipeline instead of a flat modulate:

- **TEV stages** from BP 0xC0/0xC1 (+2 per stage), stage count from genMode
  bits 10-13, texture selection/enable from TREF (0x28-0x2F), K colours from
  0xE0-0xE7 and K selectors from 0xF6-0xFD.
- Combiner `(d op ((a op2 b) * scale + bias))` with the GX ADD/SUB, bias and
  scale encodings; colour args are **per channel** (the first version used
  `tex[0]`/`ras[0]` for all three, which rendered every frame in greyscale),
  alpha args are scalar.
- **ROP**: BP 0x41 blend enable/subtract, src/dst factors (ZERO/ONE/SRC*),
  colour/alpha update enables, and BP 0x40 depth test/compare/update.
- **Scissor**: BP 0x20/0x21 with Dolphin's field order (`y` bits 0-10,
  `x` bits 12-22, both +342) — the first version had x/y swapped, which
  produced a bogus `(0,342)-(479,1620)` rectangle and clipped the frame.

With these the console's wrist-strap safety screens render **exactly** as
authored: full colour, correct composite, and they animate through their pages
as the game advances.

### Timeline instrumentation

Three counters make the game's progress measurable:

- `[di] #n retrace=.. off=.. len=.. -> region` (COLORS_TRACE_FILES) for every
  IOS disc read,
- `[dbg] disc: ios_reads=.. bytes=.. di_mmio=..R/..W` at exit — the low-level
  DI counters prove the game never issues a register-level DVD command
  (0 writes),
- `[dbg] timeline: last_draw_blk=.. last_ax_dma_blk=..` sampled every 1M blocks,
  which dates the last draw and the last AX DMA callback.

Findings:

- All 18 IOS disc reads happen at retrace 4-8 during boot and total 1.6 MiB
  (`sonic2010_0.cpk` + `sonic2010.sel`).
- The game then renders the safety screens for ~35 s of video with **no I/O at
  all**: no IOS calls, no DI reads, no DI MMIO writes.
- Draws and AX DMA callbacks continue together until the render stops, so the
  audio path is alive right up to that point.

### Where the game stops now

With the display-list cap fixed the renderer keeps running: at 3.2B blocks it
has produced **275,341 draws / 2,389 frames** and is still going. The safety
screens render and animate correctly; after them the game holds on a full-screen
fade quad (uniform 0.25 grey when blending is disabled, saturating to white
through the bloom chain when it is on) and draws only a UI/text pass
(`vertex_size=5`, glyph quads sampling a font atlas at `0x10B9ABC0`) plus the
bloom chain.

No new disc I/O ever happens: **18 IOS reads / 1.6 MiB total, 0 DI MMIO writes**.
So graphics and audio are alive (`last_ax_dma_blk` tracks `last_draw_blk`) while
the game waits on the asset that would start the next scene. The remaining work
is finding why that load request is never issued (a CriWare streamer path — the
main thread spends most of its time in a 104 KB MEM2→MEM1 `memcpy` posted every
frame).

### Byte-wise FIFO resync

The old desync handling buffered until 64 KB and then hard-reset the frontend,
discarding CP/XF state. `dol_aurora_recomp_frontend_skip_pending()` now lets the
host drop the offending byte and re-parse, so a desynced stream resynchronises on
the next real command boundary and keeps its state. Measured on the same 100M
blocks:

```
before: flush_fail=607  pending=20240  resync=2
after : flush_fail=296  pending=0      resync=0  skip=296
```

(pending now returns to zero every flush; the remaining failures are
`handle_draw`/FIFO-payload limits rather than opcode errors, and each one costs
one byte.)

### Texture bindings at this boot stage

Logging every texture packet (`COLORS_TEX_DEBUG=1`) shows the game only ever
binds the five EFB-copy destinations (`0x13284020` 640x480, `0x10AA9B80`
160x120, `0x10ABC7C0` 80x60, `0x10AC12E0` 40x30, `0x10AC2720` 20x15), all RGBA8.
No authored texture (CPK asset) is bound, and the EFB snapshot before the blit
contains only the mip/blur quads — so the frame being composited has no scene in
it yet.

### What the game draws right now

Decoded per frame at this boot stage: one 640x480 blit quad plus a mip chain
rendered into the EFB at 160x120 / 80x60 / 40x30 / 20x15, then copied out with
`EFB -> texture` copies (`0x13284020` for the 640x480 image, `0x10AA9B80` for
160x120, ...). The EFB snapshot before the blit is **black**, and every sampled
texture is one of those (black) copies, so the presented frame is empty. The
geometry path, transform, texturing, EFB-copy cache and present path are all
exercised and correct; the missing piece is game-side content (the scene is not
being drawn yet at this point in boot).

Measured on 100M blocks with the font bypass:

```
gx   : fifo_writes=193849 draws=345 state=3543 resources=249
       flush_fail=296 pending=0 resync=0 skip=296
raster: tris=690 pixels=1978942 textured=345 clears=6
```

`COLORS_RASTER_WHITE_TEX=1` + `COLORS_FB_PRE=<path>` is the validation recipe:
it turns the pre-blit EFB into a coverage map showing the mip rectangles and
their ±1 px blur passes exactly where the guest submits them.



## Runtime migration: DolRuntime → GXRuntime

The host runtime dependency is now **GXRuntime** (`tools/GXRuntime`), consumed
**unmodified**. `DolRuntime` is no longer linked. Everything GXRuntime does not
provide lives in this repository:

| Gap | Where it is closed |
|-----|--------------------|
| DolRecomp's generated code calls instruction-shaped FP / PS / FP load-store helpers (`ppc_fadds`, `ppc_ps_madd_op`, `ppc_lfs_op`, `ppc_lwarx_op`, `ppc_stsw`, `ppc_fcmp`, ...) that GXRuntime's `core/cpu.h` does not declare | `src/compat/core/cpu.h` (declarations, found before GXRuntime's include dir and chained with `#include_next`) + `src/compat/ppc_abi.c` (Dolphin-bit-exact implementations, ported from the project's own DolRuntime copy). GXRuntime's primitive helpers (`ppc_fma`, `ppc_fctiw`, `ppc_fres`, `ppc_fpscr_updated`, ...) are reused, never redefined. |
| `ppc_psq_load` / `ppc_psq_store` return `void` in GXRuntime but `bool` in the DolRecomp ABI, so emitted `if (!ppc_psq_load(...)) return;` does not compile | `tools/fix_generated_gxruntime.py` rewrites those call sites in `recompiled/generated/chunks`. **Re-run it after any recompilation.** |
| Lazy FP: GXRuntime raises an FP-unavailable exception whenever `MSR[FP]` is clear, but the guest's FP-unavailable vector at `0x800` is *not* in the recompiled image, so the trap can never be serviced | `src/compat/core/cpu.h` defines `ppc_fp_available(cpu, cia)` to `1`, restoring the eager-FP behaviour the host previously selected via `ppc_lazy_fp_set_enabled(false)` (and removing one call per generated FP instruction). |
| Guest memory-write journal used by `COLORS_WATCH_WRITE` (`ppc_set_mem_write_journal`) | `src/compat/ppc_abi.c` owns the hook; `src/compat/core/cpu.h` wraps `mem_write8/16/32/64` with a single predictable "journal installed?" test so the diagnostics still fire. |
| GXRuntime's **C** GX frontend API (a) leaves packet-drain mode off, which is only valid for offline replay — a live sink re-walks the retained trace ring and the decoder starts rejecting the stream; (b) exposes no last-error accessors; (c) cannot drop a prefix of the pending stream | `src/gx_frontend.{h,cpp}` — a host bridge over the **C++** `gxruntime::aurora_recomp::RetailGxFrontend`: enables `set_packet_drain_enabled(true)`, forwards the C++ packet into the same plain-data structs the C API uses, re-exports `last_error/offset/opcode`, and owns the pending byte stream so a decode failure can be recovered. |
| Retail streams write vertices wider than the declared VCD/VAT, so the decoder can consume a draw with too small a stride and land inside the vertex payload | The bridge's recovery: on failure it snapshots `DolGxRecompState` (which already reflects every command parsed before the rejection), then retries from successive later byte offsets (up to 1 KiB) until the remainder parses, then continues. This replaces the byte-wise resync the previous frontend performed internally. |
| C render resource packet omits texture dimensions / TLUT and the EFB-copy source rect (the C++ sink struct has them) | `raster_packet_resource()` derives them from the packet's trace event: `TEXTURE` → `event.e/f` = width/height, `event.tlut_*`; `COPY_DESTINATION` → `event.g` = `width<<16|height`, `event.e/f` = source x/y, `event.d` = post-copy clear. |
| gxcore namespace / include path | `src/texdecode.cpp` uses `gxruntime::gxcore` and `gxruntime/gxcore/texture_decode.hpp`. |
| `WII_MEM2_*` duplicated between `src/mem.h` and `core/cpu.h` | `src/mem.h` relies on the runtime's definitions. |

CMake wires it together with `add_subdirectory(tools/GXRuntime GXRuntime)`,
`GXRUNTIME_ENABLE_AURORA=OFF`, `GXRUNTIME_ENABLE_AURORA_RECOMP=ON`, and links
`GXRuntime::runtime`, `GXRuntime::retail_gx_frontend`, `GXRuntime::gxcore`.
SDL2 (not SDL3) is detected for the window backend, matching `src/window.c`.

### Equivalence check after the migration

Both checkpoints below reproduce the pre-migration numbers exactly.

200M blocks (font bypass, `COLORS_GX_DECODE=1`):

```
[dbg] gx: fifo_writes=396883 draws=10600 state=94776 resources=7856
           trace=0 flush_fail=0 pending=0 resync=0 skip=0
```

1700M blocks — past save init, the next-scene stream and the SFD load:

```
[dbg] irq: retraces=1335 external=4132 decrementer=39624
[dbg] gx:  fifo_writes=3453677 draws=93623 state=828627 resources=71768
            trace=0 flush_fail=0 pending=0 resync=0 skip=0
[dbg] disc: ios_reads=24 bytes=3440768 (3.3 MiB) di_mmio=100R/0W
[gx] raster: draws=92326 tris=182712 pixels=1124410140 textured=92326
             clears=1325 dumps=1325
[dbg] cri: post=8 enqueue=10
```

The disc totals (`ios_reads=24`, `3.3 MiB`) and the retrace at which the SFD
finishes loading are identical to the pre-migration run, so the CPU, FPU, MMIO,
MEM2, timebase, interrupt and audio-HLE behaviour is unchanged; and without the
font bypass the run still parks at the same font wait (`0x806E6C5C`).

`skip` reports successful desync recoveries performed by the host bridge
(`colors_gx_frontend_resync_count`): 148 on the 200M checkpoint and 1304 by
1700M, with the draw count still exactly 10600 / 93623 — the pre-migration
values.

That is the one known fidelity gap of this migration. Sonic Colors writes some
vertices wider than the VCD/VAT declares (an attribute the title never enables
but does emit), so GXRuntime's decoder can consume a draw with too small a
stride and land inside the vertex payload; it then rejects the following
"command". The bridge recovers by resuming at the next byte offset that parses
(the rejected bytes are payload, not whole commands, so no draw is lost), which
means those ~1.4% of draws are rasterised with the declared stride and can have
distorted geometry. Upstream DolRuntime's decoder solved this by validating the
declared stride against the following commands (`stream_check`) and adopting the
smallest wider stride that parses cleanly. Reproducing that here would mean
feeding the decoder one command at a time so the corrected stride can be applied
to the draw before it is parsed — possible through the same bridge, but not yet
done.


## Idle fast-forward (throughput)

Measurement first: the Wii OS idle thread (`0x803EBF48`, priority 31) spins in a
loop that polls an `OSThreadQueue` and calls `__OSReschedule`. Because a console
game is vsync-locked, that spin accounted for **>90 % of all executed blocks**:
1.27 M guest blocks per emulated 60 Hz frame, i.e. ~3.1 M blocks/s of host time
produced only ~2.4 emulated frames per wall-clock second.

`interrupt_idle_skip_ticks()` (`src/interrupt.c`) detects the skip-safe state
using the guest's own bookkeeping rather than a heuristic:

* `pc == 0x803EBF48` (the idle loop head),
* the polled queue (`r13-21336`) has no waiter,
* `__OSReadyThreadQueue` (`r13-23432`, the priority bitmap `__OSReschedule`
  maintains) is empty,
* `MSR[EE]` is set (otherwise a skipped retrace could never be delivered).

When all four hold, the host advances the timebase straight to the next event
(VI retrace or the decrementer/`OSAlarm` deadline, whichever is sooner) and
forces one interrupt delivery pass (`interrupt_force_poll`), so retrace, AID and
PE-finish cadence are unchanged. `COLORS_NO_IDLE_SKIP=1` disables it.

Effect at 1.35 B blocks: 1059 retraces (~17.6 s emulated). With the skip the
same 1059 retraces arrive after **22.2 M blocks**. Reaching 43 s emulated (2600
retraces) costs 47 M blocks instead of ~3.4 B.

Faithfulness check — identical binary, `COLORS_STOP_RETRACE=1059`, same guest
frame:

| | no skip | skip |
|---|---|---|
| blocks | 1,349,488,127 | 22,213,928 |
| draws | 73390 | 73180 |
| state packets | 650916 | 649046 |
| resource packets | 54500 | 54344 |
| disc reads / bytes | 18 / 1.68 MiB | 18 / 1.68 MiB |
| GX desync recoveries | 1045 | 1045 |

The resync count is identical, so the fast-forward does not perturb the command
stream; the 0.3 % draw difference is batch-boundary sensitivity of the decoder
recovery, not of the skip. (Both runs are post-migration; the pre-skip column is
the same build with `COLORS_NO_IDLE_SKIP=1`.)

New diagnostics: `COLORS_STOP_RETRACE=N` (clean stop at a guest frame, so two
configurations can be compared), `[dbg] idle: skips=… guest_ticks=…`,
`COLORS_GX_FAIL_LOG=1` (decoder's first failures), `COLORS_THREADDUMP=a,b,c`
(multiple OS thread-table dumps with the ready bitmap), and the draw ring grew
to 160 entries so a whole frame's draw list is visible.

### Where the game actually is now

At 43 s emulated the screen is a white loading screen with a **spinner** (two
small quads orbiting (280,7), redrawn every frame) underneath a full-screen
`EFB -> texture` blit. The guest is alive and busy: 11 of 15 threads ready,
`__OSReadyThreadQueue = 0x008AC001`, ~18 k blocks/frame of real work, and the
per-frame game logic (object timer list at `0x80123CAC`) runs.

The loading never completes. Confirmed at **133 s emulated** (retrace 8000,
123.7 M blocks, `COLORS_STOP_RETRACE=8000`):

```
[dbg] idle: skips=18653 guest_ticks=5293552016 (130.70 s emulated)
[dbg] gx: fifo_writes=20778938 draws=559893 state=4958436 resources=418140
[dbg] cri: post=8 enqueue=10
[dbg] disc: ios_reads=24 bytes=3440768 (3.3 MiB)
```

So 130.7 of 133 emulated seconds are idle-skip: the guest is blocked, not slow.
The CRI/Sofdec pipeline froze at `post=8 enqueue=10` long before (the movie is
110 frames / 3.65 s), no further DI reads are issued, and the frame pump
(`0x806759E0`) keeps re-converting the same single decoded frame
(`Y=30`, `U=V=128` → RGB 16,16,16, written once and never updated).

The DI trace shows exactly where the pipeline stops feeding:

```
[di] len=0x80000 off=0xB8FF5548 dst=0x12C7AC00 outlen=524288
[di] len=0x36800 off=0xB9075548 dst=0x12CFAC00 outlen=223232
[di] len=0x64000 off=0xB90ABD48 dst=0x12C7AC00 outlen=409600
```

Only 1.68 MiB of the 2.21 MiB stream is ever requested: the reader stops because
the 512 KiB ring at `0x12C7AC00` never drains, i.e. the guest MPEG decoder stops
consuming after ~10 frames. Both audio-HLE switches are load-bearing for boot
(`COLORS_NO_DSP_TASK_HLE=1` / `COLORS_NO_AUDIO_HLE=1` stop after one DI read), so
they cannot be used to bisect the stall.

A full frame's draw list (now that the ring keeps all 69 draws) shows the frame
is *only* the bloom pyramid, with no scene and no movie quad:

```
  0 160x120 @(0,0)     tex=13284020 blend=0   <- downsample 640x480 -> L0
  1  80x60  @(160,0)   tex=10AA9A40 blend=0   <- L0 -> L1
  2  40x30  @(240,0)   tex=10ABC660 blend=0   <- L1 -> L2
  3  20x15  @(280,0)   tex=10AC11A0 blend=0   <- L2 -> L3
  4..42  4x/8x blur passes around L0/L1/L2/L3 (blend=1, col=white, +-1..+-8 px)
 43..45  re-sample L0/L1/L2 at 160x120
 46..47  640x480 composite (tex=13284020 blend=0, then tex=10AA9A40 blend=1)
```

So the guest submits a post-processing chain over an EFB that has no scene in
it: the final composite is an EFB copy of the pyramid itself. That is why the
screen is uniformly white — and it also means the additive blur passes must be
checked against the TEV/blend rules once real scene content exists.

Diagnosis: the blocker is the guest Sofdec decode thread, not the scheduler
(thread table is healthy and the ready bitmap is populated), not the rasteriser
(the pre-blit EFB and every EFB copy contain exactly what was drawn), and not
disc I/O. Two ways forward: (a) find why the decode thread stops consuming the
ring, or (b) HLE the movie video path — the converter at `0x8063AFF8` is called
per frame with `{src Y/U/V, dst RGBA8 640x480}` descriptors, so a host-side
decoded frame could be blitted there instead of relying on the guest decoder.


## The white screen: a TEV bug, not missing content

Symptom: the window is uniformly white from the first safety screen onwards,
even though the game keeps running.

Instrumentation path (all env-gated, still in the tree):

* `COLORS_TEX_MAGENTA=1` forces every sampled texel to magenta — the whole frame
  went magenta, so the screen *is* covered by textured geometry and the colour
  comes from the texture/TEV path, not from a clear or a missing draw.
* `COLORS_COPY_LOG=1` shows the EFB-copy chain climbing
  `0x000000 → 0x202020 → 0x404040 → 0x808080 → 0xFFFFFF` across the four bloom
  mip levels, i.e. **every pass doubles** and saturates.
* `COLORS_TEV_DUMP=1` printed the stage state, which exposed the bug in
  `tev_combine()`:

```c
/* before */
f32 t = (op & 1) ? (a - b) : (a + b);   /* a, b only ... */
return (op & 1) ? (d - t) : (d + t);    /* ... then d + a + b */
```

The GX combiner is `(a * b) + (c * d)` (with `(a * b) - (c * d)` for the
subtract op); the old code ignored `c` entirely and added `a + b + d`, which
turns a `TEXC * ONE` pass into `2 * TEXC` once the stage scale is applied — hence
the doubling and the blown-out white.

Fixed in `src/raster.c`:

* `tev_combine()` now evaluates `(a*b) ± (c*d)`, then bias, then scale.
* The combiner fields are extracted exactly as Dolphin's
  `BPMemory.h` `TevStageCombiner` declares them — colour
  `d[3:0] c[7:4] b[11:8] a[15:12]`, alpha `rswap[1:0] tswap[3:2] d[6:4] c[9:7]
  b[12:10] a[15:13]`, then `bias[17:16] op[18] clamp[19] scale[21:20]
  dest[23:22]`. (An intermediate edit had the colour/alpha args in the wrong
  order; it was reverted against the upstream layout.) `TevColorArg` and
  `TevScale` values were also checked against the same header.
* The two pass-through fast paths are re-keyed to the real pass-through forms
  (`(TEXC * ONE) + (ZERO * ZERO)` and the `RASC` equivalent).

Result: the uniform white is gone. The frame now renders `RGB(0,0,0)`.

**That black is correct for this point in boot.** With textures forced to
pass-through (`COLORS_FORCE_TEX_PASS=1`) the frame is still flat black, so the
EFB genuinely contains nothing: the EFB copies the game samples are the cleared
(black) framebuffer, and the only things it submits on top are the bloom pyramid
and two cover quads. There is no scene because (a) the movie never decodes and
(b) all the text is drawn with the NAND system font, which is not present. The
white was the TEV bug amplifying that near-black chain to saturation.

So the remaining blankness is a *content* problem (movie decode + system font),
not a colour problem.


## Follow-up: what the black frame actually contains

After the TEV fix the frame is black rather than white, and this is now
explainable end to end.

**The TEV fix is validated.** The EFB-copy chain that used to climb
`0x20 -> 0x40 -> 0x80 -> 0xFF` now plateaus instead:

```
[copyhit] addr=0x13284020 640x480 first_px=00000000
[copyhit] addr=0x10AA9B80 160x120 first_px=00000000
[copyhit] addr=0x10ABC7C0  80x60  first_px=00000080
[copyhit] addr=0x10AC12E0  40x30  first_px=00000080
[copyhit] addr=0x10AC2720  20x15  first_px=00000080
```

i.e. the passes are no longer amplifying; what remains is transparent black,
which composites to black over black.

**The frame really is empty.** Two independent checks:

* `COLORS_COPY_LOG` shows the EFB copies the game samples are the cleared
  framebuffer.
* A whole frame's draw list is only the bloom pyramid plus two cover quads —
  there is no scene geometry, no UI geometry and no text geometry.

**The loading artwork exists in guest memory but is never drawn.**
`0x10B0F4C0` holds a real 640x480 image (32 439 distinct RGBA values: blue
`(0,0,255)`, greys, white, transparent black), and it *is* bound as a texture,
but the frame never samples it — the two cover quads that would use it resolve
to transparent black.

**Those cover quads also expose a missing feature.** Their texcoords come out of
the vertex payload in *pixel* units (`u=0..640`, `v=0..480`) rather than
normalised, which is the GX idiom for a full-screen pass driven by the texture
matrix. The rasteriser has no texture-matrix/texgen step (positions get the
guest matrices, texcoords do not), so the UVs are used raw. `COLORS_UV_PIXELS=1`
applies a per-texture divide as an experiment; it does not change the result
here because the sampled content is transparent, but the missing texgen path is
a real gap for later screens.

**Nothing is pending in the guest.** All worker threads park in the same
`sleep -> 0x803414E0 -> 0x80555E4C` chain (the 0x80555xxx worker pool, idle),
the main thread is in its normal per-frame loop blocked in the frame wait
(`0x80353360`), and no DI read is outstanding. So the game is not deadlocked; it
sits in a load state whose completion condition is never satisfied, with an
empty draw list.

Next step for visible content: find that load state (the state machine driven
around `0x800124C4`/`0x800127D0` and the media pump `0x806759E0`) and the
condition it waits on, now that runs reach 2+ emulated minutes in ~40 M blocks.


## Vertex layout, not stride: what the empty draws really carry

Chasing the same two full-screen cover quads (`fmt=0`, `prim=0x98`, `vs=20`)
turned up the actual defect.

The 20-byte vertices handed to the rasteriser are **garbage**:

```
v0 00000000 00000000 00000000 00000000 43F00000
v1 00000000 44200000 00000000 00000000 44200000
v2 43F00000 00000000 48614500 00020000 00000000
v3 00000000 00000000 00000000 00000000 00000000
```

Read with the VCD/VAT-derived layout (`pos` f32x3 @0, `tex0` f32x2 @12) that is
`pos=(0,0,0)` / `(0,640,0)` / `(480,0,230944)` / `(0,0,0)` with texcoords
`(0,480)`, `(0,640)`, denormal, `(0,0)` — not a quad. `v2.z = 230944` and an
all-zero `v3` cannot come from a real vertex.

The obvious hypothesis was the "retail stream writes wider vertices than
VCD/VAT declares" quirk, so the bridge grew `recover_vertex_strides()`: it walks
the buffered commands with the same length table GXRuntime uses, and for any
draw whose declared stride does not continue cleanly it adopts the smallest
wider stride that does (the heuristic the previous runtime used). On this
content it correctly finds **nothing to fix** — the stream parses cleanly at the
declared 20-byte stride all the way to the tail of every buffer.

So the desync theory is dead here: the bytes are self-consistent and the payload
is what it is. That points at the **attribute layout** rather than the stride —
either the VAT/VCD state used for that draw is stale (the title switches vertex
formats quickly and the layout is only re-derived when a VCD write arrives), or
the vertex really carries an attribute the VCD never enables, which is the case
the raster's `force_color0` retry exists for. Next lead: log the VCD/VAT words
alongside the draw and check them against the payload's shape.

## The game's own diagnostics

`COLORS_OSREPORT=1` surfaces the guest's `OSReport` output, which was previously
invisible. It prints the RVL kernel banner, heap layout, and then:

```
[os] (803986A4) Failed to set home directory.
```

That is the SDK's NAND/ISFS home-directory setup failing (no NAND emulation), a
direct explanation for the missing system font — every piece of text the game
would draw is unavailable. Everything else it reports is healthy (heaps, arena
bounds, corrupt-context flags).

`recover_vertex_strides()` is enabled by default and can be turned off with
`COLORS_NO_STRIDE_RECOVERY=1`; `COLORS_STRIDE_DEBUG=1` logs any correction.


## Vertex layout verified against Dolphin; the payload itself is the odd one

The VCD/VAT decode the raster uses was checked against Dolphin's
`VideoCommon/CPMemory.h` `TVtxDesc`, which is the authoritative register layout:

```
Low:   bits 9-10 Position, 11-12 Normal, 13-14 Color0, 15-16 Color1,
       bits 0-8 = PosMatIdx / Tex0..7MatIdx flags
High:  bits 0-1 Tex0Coord, 2-3 Tex1Coord, ... 14-15 Tex7Coord
```

`derive_layout()` matches that exactly (position from VCD_LO bit 9, normals 11,
colours 13/15, texcoords from VCD_HI `2*i`), and the observed values for the
cover quad agree with it:

```
vcd lo=00000200 hi=00000001 vat0=41377009 C8241209 04824120
  -> position direct, 3 components, VAT fmt 4 (f32)   = 12 bytes
  -> tex0    direct, 2 components, VAT fmt 4 (f32)    =  8 bytes
  -> vertex size 20, matching the frontend's vs=20
```

So layout, stride, and stream slicing are all consistent — and the bytes really
are:

```
v0 00000000 00000000 00000000 00000000 43F00000   pos (0,0,0)      uv (0,480)
v1 00000000 44200000 00000000 00000000 44200000   pos (0,640,0)    uv (0,640)
v2 43F00000 00000000 48614500 00020000 00000000   pos (480,0,230944)
v3 00000000 ... (all zero)                        pos (0,0,0)
```

Two facts follow and they are both actionable:

1. **`v2.z = 230944` and an all-zero `v3`** mean the guest's source buffer holds
   stale or partially written data at that point (`x`/`y` are the sane 0/480/640
   while `z` is garbage) - i.e. the game reuses a buffer and does not rewrite
   every component. Worth checking with the write journal on the buffer the CP
   array points at.
2. **The texcoords are in pixel units** (`0..640`, `0..480`), which is the GX
   idiom for a texture-matrix-driven pass. The rasteriser applies the guest
   matrices to positions only; it has **no texture-matrix/texgen step**, so
   those UVs are used raw. That is a confirmed missing feature, and the socket
   for it already exists: the bridge's `ColorsSink` receives the C++ packet's
   `tex_matrices`/`xf_regs`, which the trimmed C struct drops, so exposing them
   through `gx_frontend.h` is all the plumbing needed.

## Diagnostics added this round

`COLORS_OSREPORT=1` (guest OSReport), `COLORS_STRIDE_DEBUG=1` (stride recovery,
which correctly finds nothing here), `COLORS_TEX_DUMP_ONE=<addr>` (decode one
texture to `/tmp/opencode/texone_N.ppm`), `COLORS_MEDIA_DEBUG=1` (media player
pump/predicate calls), plus per-blocked-thread stack backchains in
`COLORS_THREADDUMP` and an `UNMAPPED` DI-read warning.


## TEV verified against Dolphin; the black frame is the safety-screen background

With the combiner formula and field layout now matching Dolphin's `BPMemory.h`
exactly (`d[3:0] c[7:4] b[11:8] a[15:12]`, alpha `rswap/tswap` then
`d[6:4] c[9:7] b[12:10] a[15:13]`, and `(a*b) +- (c*d)`), the frame renders grey
at retrace 12 and black from retrace 20 onwards, and it stays black.

Instrumenting the two full-screen cover quads at retrace 80 shows why:

```
[tev] raw C0=08FFFA C1=08FFD0 gen=000010
[tev]  s0 map=0 en=0 c=(15,15,15,10) a=(7,7,7,5) op=0 bias=0 sc=0
```

`d = RasColor`, `a = b = c = Zero`, texture disabled — a deliberate black
cover with vertex-driven alpha. So the guest is drawing a black background, not
failing to draw: what is missing on top of it is the *text*, which comes from the
NAND system font (see `Failed to set home directory` above). A health-and-safety
screen is exactly a black field with white text.

Two consequences:

* The "white screen" is fully resolved: it was the TEV double-count saturating a
  black background to 255. The correct output for this content is black.
* `COLORS_FORCE_VS_SIZE=<n>` was added to test vertex-stride hypotheses from the
  raster side (it overrides the stride and drops attributes that no longer fit).
  At 12, 16 and the declared 20 the frame stays black, which rules the stride out
  conclusively: the draw is defined to output black whatever the vertices are.


## NAND file service (`nand/`)

A NAND dump is now wired into the IOS layer (`src/ios.c`). The SDK opens NAND
paths *directly* through `IOS_Open` (`/shared2/sys/SYSCONF`,
`/title/00000001/00000002/data/setting.txt`, ...), so the dispatcher maps any
absolute guest path that is not `/dev/...` onto `nand/<path>` (override the root
with `COLORS_NAND=<dir>`), and read/write/seek/close operate on the host file.

`IOS_Seek` (IPC type 5) was previously **not implemented at all** — added for
both NAND and save-store descriptors, with the same argument layout the SDK uses
for file reads (`buf` at ctx+12, `len` at ctx+16).

Verified reads (sizes match the files on disk exactly):

```
/shared2/sys/SYSCONF                          16384 bytes
/title/00000001/00000002/data/setting.txt       256 bytes
/title/00000001/00000002/data/play_rec.dat      128 bytes
```

### Why the full tree is opt-in

Reading the real `SYSCONF` makes the SDK run its bignum signature verification
(hot PCs `0x80374060`, `0x80374150`, `0x803741C4`, `0x803742B8` — the
multiply-accumulate/divide loops). In this interpreter that dominates emulated
time: reaching retrace 400 costs **171 M blocks instead of 14.6 M** (~12x) and
the game's progress is otherwise unchanged. So by default only
`/title/00010000/...` (the title's own save directory) is served from the NAND;
`COLORS_NAND_ALL=1` serves the whole tree.

The game does not open its own save directory in the current flow (it never
touches `/title/00010000/534e4345/`), so the NAND does not change the stall yet —
but the save files (`game_0..5.sav`, `system.sav`) are now reachable when the
flow gets there.

### Correction: the "home directory" message is not the font

The OSReport line `Failed to set home directory.` is emitted from
`0x803986A0`, inside the **NWC24/WiiConnect24** initialisation path
(`0x80391EC0`, `0x803BC5C0`, `0x803BCA70`, `0x803BC9D0`), not by the font
loader. There is also **no font anywhere in the NAND dump** (`shared2` holds
`SYSCONF`, `wc24/*`, `sys/net/*` only), so the game's UI text must come from
CPK data. The font lead is closed.
