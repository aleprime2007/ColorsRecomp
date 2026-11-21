#ifndef COLORS_COMPAT_CORE_CPU_H
#define COLORS_COMPAT_CORE_CPU_H

// Generated-code ABI glue between GXRuntime and DolRecomp's emitter.
//
// GXRuntime's own <core/cpu.h> owns CPUState, guest memory and the primitive
// FP helpers (ppc_fma, ppc_fctiw, ppc_fres, ppc_ps_res, ...). DolRecomp's
// emitter additionally calls the *instruction-shaped* helpers below, which
// this project implements in src/compat/ppc_abi.c. Including them from the same
// header keeps recompiled/generated compiling unchanged and leaves GXRuntime
// untouched.
//
// This file is found before GXRuntime's include directory, so `#include_next`
// pulls in the real header first.

#include_next <core/cpu.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Instruction-shaped FP unit mirroring Dolphin's interpreter bit-exactly
 * (Interpreter_FloatingPoint/Paired + Interpreter_FPUtils NI_* semantics:
 * PPC NaN propagation, Force25Bit frC rounding, single-precision Fill of
 * both PS lanes, FPSCR FPRF/FI/FR/exception updates, VE/ZE write gating).
 * Generated code calls these; the register indices select CPUState lanes. */
void ppc_fadds(CPUState* cpu, u8 d, u8 a, u8 b);
void ppc_fsubs(CPUState* cpu, u8 d, u8 a, u8 b);
void ppc_fmuls(CPUState* cpu, u8 d, u8 a, u8 c);
void ppc_fdivs(CPUState* cpu, u8 d, u8 a, u8 b);
void ppc_fadd(CPUState* cpu, u8 d, u8 a, u8 b);
void ppc_fsub(CPUState* cpu, u8 d, u8 a, u8 b);
void ppc_fmul(CPUState* cpu, u8 d, u8 a, u8 c);
void ppc_fdiv(CPUState* cpu, u8 d, u8 a, u8 b);
void ppc_fmadd_op(CPUState* cpu, u8 d, u8 a, u8 c, u8 b,
                  bool single, bool subtract, bool negative);
void ppc_frsp(CPUState* cpu, u8 d, u8 b);
void ppc_fres_op(CPUState* cpu, u8 d, u8 b);
void ppc_frsqrte_op(CPUState* cpu, u8 d, u8 b);
void ppc_fctiw_op(CPUState* cpu, u8 d, u8 b, bool toward_zero);
void ppc_fcmp(CPUState* cpu, u8 crfd, f64 a, f64 b, bool ordered);
void ppc_ps_add_op(CPUState* cpu, u8 d, u8 a, u8 b);
void ppc_ps_sub_op(CPUState* cpu, u8 d, u8 a, u8 b);
void ppc_ps_mul_op(CPUState* cpu, u8 d, u8 a, u8 c);
void ppc_ps_div_op(CPUState* cpu, u8 d, u8 a, u8 b);
void ppc_ps_madd_op(CPUState* cpu, u8 d, u8 a, u8 c, u8 b,
                    bool subtract, bool negative);
void ppc_ps_madds0(CPUState* cpu, u8 d, u8 a, u8 c, u8 b);
void ppc_ps_madds1(CPUState* cpu, u8 d, u8 a, u8 c, u8 b);
void ppc_ps_sum0(CPUState* cpu, u8 d, u8 a, u8 c, u8 b);
void ppc_ps_sum1(CPUState* cpu, u8 d, u8 a, u8 c, u8 b);
void ppc_ps_muls0(CPUState* cpu, u8 d, u8 a, u8 c);
void ppc_ps_muls1(CPUState* cpu, u8 d, u8 a, u8 c);
void ppc_ps_res_op(CPUState* cpu, u8 d, u8 b);
void ppc_ps_rsqrte_op(CPUState* cpu, u8 d, u8 b);
/* FP loads/stores with Dolphin's alignment exception + stfs bit-repack
 * (ConvertToSingle) semantics. Return false when an exception was taken
 * (callers skip the update-form RA write-back). */
bool ppc_lfs_op(CPUState* cpu, u8 d, u32 ea, u32 cia);
bool ppc_lfd_op(CPUState* cpu, u8 d, u32 ea, u32 cia);
bool ppc_stfs_op(CPUState* cpu, u8 s, u32 ea, u32 cia);
bool ppc_stfd_op(CPUState* cpu, u8 s, u32 ea, u32 cia);
bool ppc_lwarx_op(CPUState* cpu, u8 d, u32 ea, u32 cia);
void ppc_stwcx_op(CPUState* cpu, u8 s, u32 ea, u32 cia);
/* stswi/stswx store-string: mirrors Dolphin's Helper_StoreString word-based
 * read-modify-write of the head/tail partial words (naive byte stores leave
 * different tail bytes). n = byte count, r = first source GPR (wraps). */
void ppc_stsw(CPUState* cpu, u32 ea, u32 n, u8 r, u32 cia);
/* FPSCR control write (mtfsf/mtfsb/mtfsfi/mcrfs): recompute VX/FEX and
 * re-arm the host FPU rounding/flush mode from RN/NI, mirroring Dolphin's
 * FPSCRUpdated -> RoundingModeUpdated -> SetSIMDMode chain. */
void ppc_fpscr_control_updated(CPUState* cpu);
void ppc_mtfsb0_op(CPUState* cpu, u8 bit);
void ppc_mtfsb1_op(CPUState* cpu, u8 bit);

/* ---------------------------------------------------------------------------
 * Eager floating point.
 *
 * DolRuntime's host ran with eager FP (ppc_lazy_fp_set_enabled(false)):
 * floating-point executes regardless of MSR[FP]. That is required here too,
 * because the guest's FP-unavailable vector at 0x00000800 is *not* part of the
 * recompiled image (it is installed at runtime), so a lazy-FP trap could never
 * be serviced and would stop the machine. Overriding the predicate keeps the
 * historical behaviour and drops a call per generated FP instruction.
 * ------------------------------------------------------------------------- */
#define ppc_fp_available(cpu, cia) (1)

/* ---------------------------------------------------------------------------
 * Guest memory-write journal (debug aid).
 *
 * DolRuntime fired a host callback just before every committed MEM1 store;
 * GXRuntime has no such hook and its own sources must not be touched, so the
 * journal is driven from the generated-code side: the mem_write* wrappers below
 * report the store and then call GXRuntime's implementation. The branch is a
 * single predictable load, and the journal is only ever installed by
 * COLORS_WATCH_WRITE diagnostics.
 * ------------------------------------------------------------------------- */
typedef void (*ColorsMemWriteJournal)(u32 offset, u32 size, void* user);
void ppc_set_mem_write_journal(ColorsMemWriteJournal fn, void* user);

extern ColorsMemWriteJournal colors_mem_write_journal;
void colors_note_mem_write(u32 addr, u32 size);

// Inline so the (always false in normal runs) test costs nothing per store.
static inline void colors_journal_note(u32 addr, u32 size) {
    if (colors_mem_write_journal != NULL)
        colors_note_mem_write(addr, size);
}

static inline void colors_mem_write8(CPUState* cpu, u32 addr, u8 value) {
    colors_journal_note(addr, 1u);
    mem_write8(cpu, addr, value);
}
static inline void colors_mem_write16(CPUState* cpu, u32 addr, u16 value) {
    colors_journal_note(addr, 2u);
    mem_write16(cpu, addr, value);
}
static inline void colors_mem_write32(CPUState* cpu, u32 addr, u32 value) {
    colors_journal_note(addr, 4u);
    mem_write32(cpu, addr, value);
}
static inline void colors_mem_write64(CPUState* cpu, u32 addr, u64 value) {
    colors_journal_note(addr, 8u);
    mem_write64(cpu, addr, value);
}

#define mem_write8(cpu, addr, value) colors_mem_write8((cpu), (addr), (value))
#define mem_write16(cpu, addr, value) colors_mem_write16((cpu), (addr), (value))
#define mem_write32(cpu, addr, value) colors_mem_write32((cpu), (addr), (value))
#define mem_write64(cpu, addr, value) colors_mem_write64((cpu), (addr), (value))

#ifdef __cplusplus
}
#endif

#endif /* COLORS_COMPAT_CORE_CPU_H */
