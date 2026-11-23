# Ghidra headless recipes

`tools/ghidra_12.1.4_PUBLIC` is a side-by-side Ghidra install used only for
analysis (`rea` keeps its own 12.1.4 copy for decompilation work).

Ghidra's DOL loader rejects this title's `main.dol`, so import a flat MEM1 image
with an explicit base instead.

## 1. Build the flat image

```sh
python3 - <<'PY'
import struct
d = open('game/sys/main.dol', 'rb').read()
h = d[:0x100]
u32 = lambda o: struct.unpack_from('>I', h, o)[0]
secs = []
for i in range(7):                      # text sections
    off, addr, size = u32(0x00 + i*4), u32(0x48 + i*4), u32(0x90 + i*4)
    if size: secs.append((addr, size, off))
for i in range(11):                     # data sections
    off, addr, size = u32(0x1C + i*4), u32(0x64 + i*4), u32(0xAC + i*4)
    if size: secs.append((addr, size, off))
mem1 = [s for s in secs if 0x80000000 <= s[0] < 0x81800000]
lo = min(a for a, _, _ in mem1)
hi = max(a + z for a, z, _ in mem1)
img = bytearray(hi - lo)
for a, sz, off in mem1:
    img[a - lo:a - lo + sz] = d[off:off + sz]
open('/tmp/opencode/main_flat.bin', 'wb').write(img)
print('flat %08X..%08X (%d bytes)' % (lo, hi, hi - lo))
PY
```

## 2. Import and decompile a range

```sh
mkdir -p /tmp/opencode/ghproj /tmp/opencode/decomp
tools/ghidra_12.1.4_PUBLIC/support/analyzeHeadless /tmp/opencode/ghproj Colors \
  -import /tmp/opencode/main_flat.bin \
  -loader BinaryLoader -loader-baseAddr 0x80004000 \
  -processor "PowerPC:BE:32:default" \
  -scriptPath tools/ghidra \
  -postScript DecompileRange.java 800113C0 80012A00 /tmp/opencode/decomp
```

Leave out `-deleteProject` to keep the project for follow-up runs.

## 3. Decompile more addresses without re-analysing

```sh
tools/ghidra_12.1.4_PUBLIC/support/analyzeHeadless /tmp/opencode/ghproj Colors \
  -process main_flat.bin -noanalysis \
  -scriptPath tools/ghidra \
  -postScript DecompileAt.java /tmp/opencode/decomp2 802F37A0 801762BC
```

`DecompileAt` force-disassembles and creates a function at each address, which is
needed because retail code has gaps that break Ghidra's auto-detection (and a
few functions such as `0x800123A0` contain `psq_st`, which makes the decompiler
give up — read those from the recompiled listing instead).

## Notes

* The recompiled chunks in `recompiled/generated/chunks/` carry a full
  instruction listing in comments (`// 800123A0: stwu r1, -64(r1)`), which is the
  authoritative disassembly when Ghidra balks.
* Vtables live in data section 5 (`0x80758D80`+). Itanium layout: `[-2]`
  offset-to-top, `[-1]` typeinfo, slots from `+0x08`.
* Guest memory can be inspected live with `COLORS_MEMHEX=<addr>:<len>:<block>`
  and written once with `COLORS_POKE=<addr>:<value>[:<block>]`.
