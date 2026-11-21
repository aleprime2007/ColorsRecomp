#!/usr/bin/env python3
"""Adapt DolRecomp's generated C to GXRuntime's generated-code ABI.

The recompiled output in ``recompiled/generated`` is emitted against the
DolRecomp CPU ABI. GXRuntime implements that ABI with two intentional
differences, both of which are resolved here rather than by modifying
GXRuntime:

* ``ppc_psq_load`` / ``ppc_psq_store`` return ``void`` in GXRuntime (they
  report faults by taking the guest exception, exactly like the rest of
  GXRuntime's memory helpers), while DolRecomp emits
  ``if (!ppc_psq_load(...)) return;``. Drop the redundant test.

The instruction-shaped FP helpers DolRecomp emits (``ppc_fadds``,
``ppc_lfs_op``, ...) are *not* part of GXRuntime; this project implements them
in ``src/compat/ppc_abi.c`` and declares them in ``src/compat/core/cpu.h``, so
no source rewrite is needed for those.

Usage: python3 tools/fix_generated_gxruntime.py [generated_dir]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

PSQ_IF = re.compile(
    r"if \(!(ppc_psq_(?:load|store)\([^;]*?\))\) return;"
)

MARKER = "GXRuntime ABI"


def fix_chunk(path: Path) -> int:
    text = path.read_text(encoding="utf-8")
    fixed, count = PSQ_IF.subn(r"\1;  // " + MARKER, text)
    if count:
        path.write_text(fixed, encoding="utf-8")
    return count


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "generated_dir",
        nargs="?",
        type=Path,
        default=Path(__file__).resolve().parent.parent / "recompiled" / "generated",
    )
    args = parser.parse_args()
    chunks = sorted((args.generated_dir / "chunks").glob("*.c"))
    if not chunks:
        print(f"no chunks under {args.generated_dir}", file=sys.stderr)
        return 1
    total = 0
    touched = 0
    for chunk in chunks:
        n = fix_chunk(chunk)
        if n:
            touched += 1
            total += n
    print(f"{MARKER}: rewrote {total} psq call site(s) in {touched} chunk(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
