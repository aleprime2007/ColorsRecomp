#!/usr/bin/env python3
"""Generate rea's GameCube DOL section-table file from a main.dol header.

Format consumed by rea's ReaGhidraPrepareDol.java (see rea-agents
dist/ghidra/GhidraDolProfile.js -> ghidraDolSectionFile):
    plan 2 <num_sections> <num_bss_gaps>
    section <kind> <index> 0x<fileOffset> 0x<address> 0x<size>
    bss-gap 0x<address> 0x<size>
    entry 0x<entryPoint>
"""
import struct
import sys


def read_header(path):
    with open(path, "rb") as f:
        header = f.read(0x100)
    if len(header) < 0x100:
        sys.exit("DOL header truncated")

    def u32(off):
        return struct.unpack_from(">I", header, off)[0]

    sections = []
    # 7 text slots
    for i in range(7):
        off = u32(0x00 + i * 4)
        addr = u32(0x48 + i * 4)
        size = u32(0x90 + i * 4)
        if size:
            sections.append(("text", i, off, addr, size))
    # 11 data slots
    for i in range(11):
        off = u32(0x1C + i * 4)
        addr = u32(0x64 + i * 4)
        size = u32(0xAC + i * 4)
        if size:
            sections.append(("data", i, off, addr, size))

    bss_addr = u32(0xD8)
    bss_size = u32(0xDC)
    entry = u32(0xE0)

    # BSS gaps: BSS span minus any overlapping data sections.
    bss_end = bss_addr + bss_size
    data_in_bss = [s for s in sections if s[0] == "data"
                   and s[3] < bss_end and s[3] + s[4] > bss_addr]
    data_in_bss.sort(key=lambda s: s[3])
    gaps = []
    cursor = bss_addr
    for (_k, _i, _o, addr, size) in data_in_bss:
        if cursor < addr:
            gaps.append((cursor, addr - cursor))
        cursor = max(cursor, min(bss_end, addr + size))
    if cursor < bss_end:
        gaps.append((cursor, bss_end - cursor))

    return sections, gaps, entry


def main():
    dol = sys.argv[1] if len(sys.argv) > 1 else "game/sys/main.dol"
    out = sys.argv[2] if len(sys.argv) > 2 else "docs/dol-sections.txt"
    sections, gaps, entry = read_header(dol)
    lines = [f"plan 2 {len(sections)} {len(gaps)}"]
    for kind, idx, off, addr, size in sections:
        lines.append(f"section {kind} {idx} 0x{off:x} 0x{addr:x} 0x{size:x}")
    for addr, size in gaps:
        lines.append(f"bss-gap 0x{addr:x} 0x{size:x}")
    lines.append(f"entry 0x{entry:x}")
    text = "\n".join(lines) + "\n"
    with open(out, "w") as f:
        f.write(text)
    print(f"wrote {out} ({len(sections)} sections, {len(gaps)} bss gaps, "
          f"entry 0x{entry:x})")


if __name__ == "__main__":
    main()
