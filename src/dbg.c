#include "dbg.h"

#include "ios.h"
#include "dvd.h"
#include "window.h"

#include "dsp.h"
#include "gx.h"
#include "interrupt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// PC ring buffer
// ---------------------------------------------------------------------------
enum { PC_HIST = 24 };

// OSContext field offsets (dolphin/os/OSContext.h); OSThread embeds one at +0.
#define CTX_LR   0x084u
#define CTX_SRR0 0x198u

static u32 g_pc_hist[PC_HIST];
static unsigned g_pc_hist_i;

void dbg_pc_push(u32 pc) {
    g_pc_hist[g_pc_hist_i++ % PC_HIST] = pc;
}

void dbg_print_pc_history(void) {
    for (unsigned k = 0; k < PC_HIST; k++)
        fprintf(stderr, " 0x%08X", g_pc_hist[(g_pc_hist_i + k) % PC_HIST]);
}

// ---------------------------------------------------------------------------
// Flags and watch state
// ---------------------------------------------------------------------------
static bool g_trace_halt;
static bool g_trace_ax;
static bool g_clear_fontflag;
static bool g_clamp_copy;
static bool g_first_wait_dump;
static bool g_force_audio;
static bool g_dsp_task_hle;

static bool g_memcpy_reported;
static bool g_wait_dumped;
static bool g_halt_reported;
static bool g_trace_ref;
static bool g_trace_idle;
static bool g_sleep_log;
static unsigned long long g_sleep_after;
static unsigned long long g_pc_hist_after;
static u32 g_memcheck_addr;
static u32 g_memcheck_len = 0x100000u;
static unsigned long long g_memcheck_after;
static bool g_memcheck_trace;
static const char* g_memdump_path;
static bool g_wwatch_active;
static u32 g_wwatch_lo, g_wwatch_hi;
static unsigned long long g_cur_blocks;
static u32 g_memdump_addr;
static unsigned long long g_memdump_at;
static u32 g_memhex_addr;
static u32 g_memhex_len;
static unsigned long long g_memhex_at;
#define PCS_SLOTS 8192u
static unsigned long long g_pcs_start, g_pcs_end;
static u32 g_pcs_key[PCS_SLOTS];
static u32 g_pcs_cnt[PCS_SLOTS];
static u32 g_pcs_dumped;
static u32 g_pchit_pc[16];
static unsigned long long g_pchit_hits[16];
static u32 g_pchit_count;
static const char* g_gray_path;
static u32 g_gray_addr, g_gray_w = 640u, g_gray_h = 480u;
static unsigned long long g_gray_at;
static unsigned g_gray_series = 1u;
static unsigned g_gray_series_done;
static unsigned long long g_gray_stride = 4000000ull;
// Media pipeline entry points, one slot each (COLORS_MEDIA_DEBUG).
static const u32 g_media_pcs[10] = {
    0x8067595Cu,  // frame pump
    0x80675200u,  // status predicate
    0x80675148u,  // "playing" getter (*(player+0x64))
    0x80675760u,  // command state machine
    0x8063FB88u,  // CRI media task
    0x80012560u,  // framework loop's movie-manager gate
    0x80640A0Cu,  // just after mw_sfd_start_ex's reset call (r3 = result)
    0x806409B8u,  // mw_sfd_start_ex entry
    0x80641398u,  // mwPly create(config) entry
    0x8064019Cu,  // FUN_806400B8: after its reset(config) call
};
static u32 g_media_seen[10];
static bool g_media_debug;

static u32 g_poke_addr[8];
static u32 g_poke_value[8];
static unsigned long long g_poke_at[8];
static bool g_poke_done[8];
static unsigned g_poke_count;
static unsigned long long g_threaddump_at[8];
static unsigned g_threaddump_count;
static unsigned g_threaddump_done_count;
static u32 g_pc_hist_pc[64];
static u32 g_pc_hist_n[64];
static u32 g_pc_hist_used;
static unsigned g_cri_post_hits;
static unsigned g_wpad_hits;
static bool g_fake_wpad;
static u32 g_fake_wpad_btn;
static u32 g_wpad_pc[12];

// Hot-path switches (read once in dbg_init). dbg_block_enter runs for every
// translated block, so getenv() there costs more than the emulated CPU.
static bool g_env_movie_dump;
static bool g_env_fake_input;
static bool g_env_force_any;
static bool g_env_input_debug;
static bool g_env_fake_input_pulse;
static bool g_env_fake_input_all;
static bool g_env_fake_wpad_work;
static bool g_env_sfd_pool_fix;
static bool g_env_movie_clock;
static u32 g_env_movie_clock_inc;
static bool g_env_no_skip_movie;
static unsigned long long g_env_movie_end = 30000000ull;
static bool g_env_watch_all;
static bool g_env_watch_from_set;
static unsigned long long g_env_watch_from;

static unsigned g_cri_enqueue_hits;
static unsigned long long g_last_draw_blk;
static unsigned long long g_last_ax_blk;
static u32 g_seen_draws;
static u32 g_seen_ax;
static bool g_trace_files;
static bool g_osreport;
static bool g_hbm_module;
static bool g_gx_wait_hle;
static unsigned g_osreport_lines;
static unsigned g_ai_handler_hits;
static u32 g_main_pc_ring[12];
static unsigned g_gxwake_hits, g_gxwake_qhead, g_gxwake_state, g_gxwait_zero;
static unsigned g_main_pc_i;
static unsigned g_ax_dma_hits;

// Optional one-shot entry trace for a small set of interesting guest PCs.
static const u32 g_trace_pcs[] = {
    0x8034A0C0u,  // DVD error/retry handler
    0x8034F490u,  // __DVDLowOpenPartition
    0x8034F5F0u,  // fatal disc-error handler
    0x8034F740u,  // alarm trampoline -> error handler
    0x8034F9F0u,  // DI command completion dispatcher
    0x8033E2A0u,  // disc-error splash setup
    0x80351740u,  // low-level DI read send
    0x80351C10u,  // DI OpenPartition send
    0x80351F20u,  // DI ReadDiskID send
    0x806E62F4u,  // font init: IPC send result check
    0x806E6300u,  // font init: subtype init result check
    0x806E63D8u,  // font init: alloc/load result check
    0x806E647Cu,  // font init: success (state=4, flag=1)
    0x806EF820u,  // sound state machine dispatch
    0x806EF480u,  // sound init
    0x806E6FD8u,  // sound state machine call site
    0x806E6DA0u,  // sound update thread body
    0x806E6BF0u,  // sound thread spawn (__OSSwitchStack)
    0x806EFE00u,  // sound command handler (sets state 5)
    0x806F7E6Cu,  // sound command dispatcher
    0x800AF1A8u,  // game sound-object ctor (sends 0x1005)
    0x8053E5ACu,  // sound command poster?
    0x806E7120u,  // sound queue post(msg,type,arg)
    0x806E43D0u,  // sound queue get
    0x806E50E0u,  // sound queue signal
    0x806F871Cu,  // sound system init (top level)
    0x806F0E08u,  // sound cmd 0x1002 setup
    0x8070BA2Cu,  // game sound post API
    0x800AF1A8u,  // 0x1005 sender ctor
};
static bool g_traced[sizeof(g_trace_pcs) / sizeof(g_trace_pcs[0])];

void dbg_init(void) {
    g_trace_halt = getenv("COLORS_TRACE_HALT") != NULL;
    g_trace_ax = getenv("COLORS_TRACE_AX") != NULL;
    g_clear_fontflag =
        getenv("COLORS_CLEAR_FONTFLAG") != NULL || window_active();
    // Diagnostic: COLORS_CLAMP_COPY=1 replaces absurd-length memcpy/cache-op
    // calls with no-ops so the machine state survives for inspection.
    g_clamp_copy = getenv("COLORS_CLAMP_COPY") != NULL;
    g_first_wait_dump = getenv("COLORS_DBG_FONT") != NULL;
    // Audio driver init shortcut: the game's sound middleware waits for a
    // 0x1005 init command that it only sends later (see docs/PLAN.md). Until
    // the message flow is HLE'd, satisfy the wait so the rest of boot proceeds.
    g_force_audio = getenv("COLORS_NO_AUDIO_HLE") == NULL;
    g_dsp_task_hle = getenv("COLORS_NO_DSP_TASK_HLE") == NULL;
    // Reference-count tracing for the smart-pointer control blocks
    // (COLORS_TRACE_REF=1). Off by default so the normal trace is unchanged.
    g_trace_ref = getenv("COLORS_TRACE_REF") != NULL;
    g_trace_idle = getenv("COLORS_TRACE_IDLE") != NULL;
    g_sleep_log = getenv("COLORS_SLEEP_LOG") != NULL;
    g_media_debug = getenv("COLORS_MEDIA_DEBUG") != NULL;
    g_fake_wpad = getenv("COLORS_FAKE_WPAD") != NULL;
    g_env_movie_dump = getenv("COLORS_MOVIE_DUMP") != NULL;
    g_env_fake_input = getenv("COLORS_FAKE_INPUT") != NULL;
    g_env_force_any = getenv("COLORS_FORCE_ANY") != NULL;
    g_env_input_debug = getenv("COLORS_INPUT_DEBUG") != NULL;
    g_env_fake_input_pulse = getenv("COLORS_FAKE_INPUT_PULSE") != NULL;
    g_env_fake_input_all = getenv("COLORS_FAKE_INPUT_ALL") != NULL;
    g_env_fake_wpad_work = getenv("COLORS_FAKE_WPAD_WORK") != NULL;
    g_env_sfd_pool_fix = getenv("COLORS_SFD_POOL_FIX") != NULL;
    g_env_no_skip_movie = getenv("COLORS_NO_SKIP_MOVIE") != NULL;
    {
        const char* mv = getenv("COLORS_MOVIE_END");
        if (mv != NULL)
            g_env_movie_end = strtoull(mv, NULL, 0);
        const char* mc = getenv("COLORS_MOVIE_CLOCK");
        g_env_movie_clock = mc != NULL;
        g_env_movie_clock_inc = mc != NULL ? (u32)strtoul(mc, NULL, 0) : 0u;
        const char* wf = getenv("COLORS_WATCH_FROM");
        g_env_watch_from_set = wf != NULL;
        g_env_watch_from = wf != NULL ? strtoull(wf, NULL, 0) : 0ull;
    }
    g_env_watch_all = getenv("COLORS_WATCH_ALL") != NULL;
    {
        const char* b = getenv("COLORS_FAKE_WPAD_BTN");
        g_fake_wpad_btn = b != NULL ? (u32)strtoul(b, NULL, 0) : 0x0008u;
    }
    {
        const char* after = getenv("COLORS_SLEEP_AFTER");
        if (after != NULL)
            g_sleep_after = strtoull(after, NULL, 0);
        const char* pc_after = getenv("COLORS_PC_HIST_AFTER");
        if (pc_after != NULL)
            g_pc_hist_after = strtoull(pc_after, NULL, 0);
        const char* pcsample = getenv("COLORS_PCSAMPLE");
        if (pcsample != NULL) {
            unsigned long long lo = 0, hi = 0;
            if (sscanf(pcsample, "%llu:%llu", &lo, &hi) == 2 && hi > lo) {
                g_pcs_start = lo;
                g_pcs_end = hi;
            }
        }
        const char* pchits = getenv("COLORS_PCHITS");
        if (pchits != NULL) {
            char buf[256];
            snprintf(buf, sizeof(buf), "%s", pchits);
            char* save = NULL;
            for (char* tok = strtok_r(buf, ",", &save);
                 tok != NULL && g_pchit_count < 16u;
                 tok = strtok_r(NULL, ",", &save)) {
                g_pchit_pc[g_pchit_count++] = (u32)strtoul(tok, NULL, 0);
            }
        }
        const char* graydump = getenv("COLORS_GRAYDUMP");
        if (graydump != NULL) {
            const char* a = getenv("COLORS_GRAYDUMP_ADDR");
            const char* w = getenv("COLORS_GRAYDUMP_W");
            const char* h = getenv("COLORS_GRAYDUMP_H");
            const char* at = getenv("COLORS_GRAYDUMP_AT");
            g_gray_path = graydump;
            g_gray_addr = a != NULL ? (u32)strtoul(a, NULL, 0) : 0u;
            if (w != NULL) g_gray_w = (u32)strtoul(w, NULL, 0);
            if (h != NULL) g_gray_h = (u32)strtoul(h, NULL, 0);
            if (at != NULL) g_gray_at = strtoull(at, NULL, 0);
            const char* n = getenv("COLORS_GRAYDUMP_SERIES");
            const char* st = getenv("COLORS_GRAYDUMP_STRIDE");
            if (n != NULL) {
                unsigned v = (unsigned)strtoul(n, NULL, 0);
                if (v > 0u) g_gray_series = v;
            }
            if (st != NULL) {
                unsigned long long v = strtoull(st, NULL, 0);
                if (v > 0ull) g_gray_stride = v;
            }
        }
        {
            // COLORS_POKE=<addr>:<value>[:<block>][,<addr>:<value>[:<block>]]
            // One-shot guest memory pokes, for A/B testing a state hypothesis.
            const char* pk = getenv("COLORS_POKE");
            if (pk != NULL) {
                char buf[256];
                snprintf(buf, sizeof(buf), "%s", pk);
                char* save = NULL;
                for (char* tok = strtok_r(buf, ",", &save);
                     tok != NULL && g_poke_count < 8u;
                     tok = strtok_r(NULL, ",", &save)) {
                    unsigned long a = 0, v = 0;
                    unsigned long long at = 0;
                    const int n = sscanf(tok, "%li:%li:%llu", &a, &v, &at);
                    if (n >= 2) {
                        g_poke_addr[g_poke_count] = (u32)a;
                        g_poke_value[g_poke_count] = (u32)v;
                        g_poke_at[g_poke_count] = (unsigned long long)at;
                        g_poke_count++;
                    }
                }
            }
        }
        {
            const char* td = getenv("COLORS_THREADDUMP");
            if (td != NULL) {
                char buf[256];
                snprintf(buf, sizeof(buf), "%s", td);
                char* save = NULL;
                for (char* tok = strtok_r(buf, ",", &save);
                     tok != NULL && g_threaddump_count < 8u;
                     tok = strtok_r(NULL, ",", &save)) {
                    const unsigned long long v = strtoull(tok, NULL, 0);
                    if (v != 0ull)
                        g_threaddump_at[g_threaddump_count++] = v;
                }
            }
        }
        const char* memhex = getenv("COLORS_MEMHEX");
        if (memhex != NULL) {
            unsigned long a = 0, n = 0, at = 0;
            if (sscanf(memhex, "%li:%li:%li", &a, &n, &at) == 3) {
                g_memhex_addr = (u32)a;
                g_memhex_len = (u32)n;
                g_memhex_at = at;
            }
        }
        const char* memdump = getenv("COLORS_MEMDUMP");
        if (memdump != NULL) {
            g_memdump_path = memdump;
            g_memdump_addr = (u32)strtoul(
                getenv("COLORS_MEMDUMP_ADDR") ? getenv("COLORS_MEMDUMP_ADDR")
                                              : "0x011EDEA0",
                NULL, 0);
            const char* at = getenv("COLORS_MEMDUMP_AT");
            if (at != NULL)
                g_memdump_at = strtoull(at, NULL, 0);
        }
        const char* wwatch = getenv("COLORS_WATCH_WRITE");
        if (wwatch != NULL) {
            unsigned long lo = 0, hi = 0;
            if (sscanf(wwatch, "%li:%li", &lo, &hi) == 2 && hi > lo) {
                g_wwatch_lo = (u32)lo;
                g_wwatch_hi = (u32)hi;
                g_wwatch_active = true;
            }
        }
        const char* memchk = getenv("COLORS_MEMCHECK");
        if (memchk != NULL) {
            g_memcheck_addr = (u32)strtoul(memchk, NULL, 0);
            const char* len = getenv("COLORS_MEMCHECK_LEN");
            if (len != NULL)
                g_memcheck_len = (u32)strtoul(len, NULL, 0);
            const char* after = getenv("COLORS_MEMCHECK_AFTER");
            if (after != NULL)
                g_memcheck_after = strtoull(after, NULL, 0);
            g_memcheck_trace = getenv("COLORS_MEMCHECK_TRACE") != NULL;
        }
    }
    g_trace_files = getenv("COLORS_TRACE_FILES") != NULL;
    g_osreport = getenv("COLORS_OSREPORT") != NULL;
    // The HBM (Home Button Menu) module is a dynamically loaded RSO whose code
    // lives in MEM2 and therefore cannot be executed by a DOL-only static
    // recompilation. By default report "module unavailable" (null), which is
    // the game's own graceful path; COLORS_HBM_MODULE=1 loads it for future
    // interpreter/JIT work.
    g_hbm_module = getenv("COLORS_HBM_MODULE") != NULL;
    // Headless "the GPU always finishes": the draw-done wait at FUN_80369F40
    // clears its flag and then sleeps until an interrupt sets it. With no real
    // GPU we simply satisfy the flag before the loop. COLORS_NO_GX_WAIT_HLE=1
    // keeps the interrupt-driven behaviour for debugging.
    g_gx_wait_hle = getenv("COLORS_NO_GX_WAIT_HLE") == NULL;
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------
const char* dbg_exception_name(u32 exc) {
    if (exc & PPC_EXC_PROGRAM)       return "PROGRAM";
    if (exc & PPC_EXC_DSI)           return "DSI";
    if (exc & PPC_EXC_ALIGNMENT)     return "ALIGNMENT";
    if (exc & PPC_EXC_SYSTEM_CALL)   return "SYSTEM_CALL";
    if (exc & PPC_EXC_MACHINE_CHECK) return "MACHINE_CHECK";
    if (exc & PPC_EXC_FP_UNAVAILABLE)return "FP_UNAVAILABLE";
    return "none";
}

void dbg_dump_backchain(CPUState* cpu) {
    u32 sp = cpu->gpr[1];
    fprintf(stderr, "[run] guest backchain:\n");
    for (int f = 0; f < 16 && sp >= GC_RAM_BASE && sp < GC_RAM_BASE + cpu->ram_size; f++) {
        u32 caller = mem_read32(cpu, sp);
        if (caller <= sp || caller < GC_RAM_BASE) break;
        u32 ret = mem_read32(cpu, caller + 4u);
        fprintf(stderr, "    #%-2d sp=0x%08X ret=0x%08X\n", f, sp, ret);
        sp = caller;
    }
}

// Walk a suspended thread's chain from its saved stack pointer.
static void dbg_dump_backchain_at(CPUState* cpu, u32 sp, u32 thread) {
    fprintf(stderr, "[dbg]   thread 0x%08X chain sp=0x%08X:", thread, sp);
    for (int f = 0; f < 8 && sp >= GC_RAM_BASE && sp < GC_RAM_BASE + cpu->ram_size; f++) {
        u32 caller = mem_read32(cpu, sp);
        if (caller <= sp || caller < GC_RAM_BASE) break;
        u32 ret = mem_read32(cpu, caller + 4u);
        fprintf(stderr, " [0x%08X]", ret);
        sp = caller;
    }
    fprintf(stderr, "\n");
}

// ---------------------------------------------------------------------------
// OSReport HLE
//
// 0x8033BFB0 is the SDK's formatted report (all callers do `crxor 6,6,6` for
// varargs). Routing it to the host makes the game's own diagnostics visible:
// CriWare, Metrowerks RSO loader and filesystem errors all go through here.
// ---------------------------------------------------------------------------
static u32 report_arg_string(CPUState* cpu, u32 ptr, char* out, size_t cap) {
    size_t i = 0;
    if (cap == 0)
        return 0;
    for (; i + 1u < cap; i++) {
        char c = (char)mem_read8(cpu, ptr + (u32)i);
        out[i] = c;
        if (c == '\0')
            break;
    }
    out[i] = '\0';
    return (u32)i;
}

// Guest memory-write journal installed into the generated-code ABI shim
// (src/compat/ppc_abi.c), which reports every committed MEM1 store.
typedef void (*ColorsMemWriteJournal)(u32 offset, u32 size, void* user);
extern void ppc_set_mem_write_journal(ColorsMemWriteJournal fn, void* user);

// Memory-write journal: reports the first writes landing in a guest range,
// which is how the movie player's placeholder fill was located.
static void dbg_write_watch(u32 offset, u32 size, void* user) {
    if (!g_wwatch_active)
        return;
    CPUState* cpu = (CPUState*)user;
    const u32 addr = 0x80000000u + offset;
    if (addr < g_wwatch_lo || addr >= g_wwatch_hi)
        return;
    // Ignore the SDK's dcbz-based block zeroing (0x8033B1AC) so the first
    // *content* write is visible.
    if (!g_env_watch_all && cpu != NULL &&
        (cpu->pc == 0x8033B1ACu ||
         (cpu->pc >= 0x800046B8u && cpu->pc < 0x800046E0u)))
        return;
    static u32 n = 0;
    if (!g_env_watch_from_set || g_cur_blocks >= g_env_watch_from) {
    if (n++ < 100000u)
        fprintf(stderr,
                "[wwatch] blk=%llu addr=0x%08X size=%u pc=0x%08X lr=0x%08X "
                "before=%08X\n",
                g_cur_blocks, addr, size, cpu != NULL ? cpu->pc : 0u,
                cpu != NULL ? cpu->lr : 0u,
                cpu != NULL ? mem_read32(cpu, addr & ~3u) : 0u);
    }
}

void dbg_install_write_watch(CPUState* cpu) {
    ppc_set_mem_write_journal(dbg_write_watch, cpu);
}

static void dbg_osreport(CPUState* cpu) {
    char fmt[512];
    report_arg_string(cpu, cpu->gpr[3], fmt, sizeof fmt);

    u32 gpr[8];
    for (int i = 0; i < 8; i++)
        gpr[i] = cpu->gpr[3 + (u32)i];
    int gi = 1;   // next integer arg (gpr[0] is the format pointer)
    int fi = 1;   // next float arg (f1..)
    char out[1024];
    size_t o = 0;

    for (size_t i = 0; fmt[i] != '\0' && o + 1u < sizeof out; i++) {
        if (fmt[i] != '%') { out[o++] = fmt[i]; continue; }
        i++;
        if (fmt[i] == '%') { out[o++] = '%'; continue; }
        // skip flags/width/precision
        while (fmt[i] != '\0' &&
               (fmt[i] == '-' || fmt[i] == '+' || fmt[i] == ' ' || fmt[i] == '#' ||
                fmt[i] == '0' || fmt[i] == '.' || (fmt[i] >= '1' && fmt[i] <= '9')))
            i++;
        if (fmt[i] == '\0') break;
        char conv = fmt[i];
        char tmp[64];
        if (conv == 's') {
            u32 p = (gi < 8) ? gpr[gi++] : 0u;
            char str[256];
            report_arg_string(cpu, p, str, sizeof str);
            snprintf(tmp, sizeof tmp, "%s", str);
        } else if (conv == 'c') {
            u32 v = (gi < 8) ? gpr[gi++] : 0u;
            snprintf(tmp, sizeof tmp, "%c", (char)(v & 0xFFu));
        } else if (conv == 'd' || conv == 'i') {
            s32 v = (s32)((gi < 8) ? gpr[gi++] : 0u);
            snprintf(tmp, sizeof tmp, "%d", v);
        } else if (conv == 'u') {
            u32 v = (gi < 8) ? gpr[gi++] : 0u;
            snprintf(tmp, sizeof tmp, "%u", v);
        } else if (conv == 'x') {
            u32 v = (gi < 8) ? gpr[gi++] : 0u;
            snprintf(tmp, sizeof tmp, "%x", v);
        } else if (conv == 'X') {
            u32 v = (gi < 8) ? gpr[gi++] : 0u;
            snprintf(tmp, sizeof tmp, "%X", v);
        } else if (conv == 'p') {
            u32 v = (gi < 8) ? gpr[gi++] : 0u;
            snprintf(tmp, sizeof tmp, "0x%08X", v);
        } else if (conv == 'f' || conv == 'g' || conv == 'e') {
            double v = (fi < 32) ? cpu->fpr[fi++] : 0.0;
            snprintf(tmp, sizeof tmp, "%g", v);
        } else {
            snprintf(tmp, sizeof tmp, "%%%c", conv);
            gi++;
        }
        size_t n = strlen(tmp);
        if (o + n + 1u >= sizeof out) n = sizeof out - o - 1u;
        memcpy(out + o, tmp, n);
        o += n;
    }
    out[o] = '\0';
    g_osreport_lines++;
    fprintf(stderr, "[os] (%08X) %s", cpu->lr, out);
    if (o == 0 || out[o - 1] != '\n')
        fputc('\n', stderr);
}

// ---------------------------------------------------------------------------
// Per-block watch hooks
//
// Two groups live here:
//   - pure diagnostics (always on; they only read guest memory)
//   - temporary HLE shortcuts for hardware we do not model yet (audio init and
//     DSP task completion). Both are gated by COLORS_NO_* environment flags.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// OS thread dump
// ---------------------------------------------------------------------------
// The RVL SDK keeps every OSThread on a singly linked list whose head lives at
// __OSThreadHead (0x800000DC); the list link is at OSThread+0x2FC. A sleeping
// thread's saved OSContext holds the PC it will resume at, which is what makes
// a deadlock readable: it names the wait the thread is parked in.
#define OS_THREAD_HEAD 0x800000DCu
#define TH_NEXT   0x2FCu
#define TH_STATE  0x2C8u
#define TH_PRIO   0x2CCu
#define TH_EPRIO  0x2D4u
#define TH_QUEUE  0x2DCu
#define TH_SUSPEND 0x2D0u
#define CTX_LR_OFF 0x084u
#define CTX_SRR0_OFF 0x198u
#define CTX_SRR1_OFF 0x19Cu

static const char* thread_state_name(u32 st) {
    switch (st) {
    case 1: return "ready";
    case 2: return "running";
    case 4: return "waiting";
    case 8: return "moribund";
    default: return "?";
    }
}

static void dbg_dump_backchain_at(CPUState* cpu, u32 sp, u32 thread);

static void dbg_thread_dump(CPUState* cpu, unsigned long long blocks) {
    u32 t = mem_read32(cpu, OS_THREAD_HEAD);
    fprintf(stderr,
            "[threads] at blk=%llu current=0x%08X readyMask=0x%08X "
            "runQFlag=0x%08X intMask=0x%08X\n",
            blocks, mem_read32(cpu, 0x800000E4u),
            mem_read32(cpu, cpu->gpr[13] - 23432u),
            mem_read32(cpu, cpu->gpr[13] - 23436u),
            mem_read32(cpu, 0x800000C4u));
    int n = 0;
    while (t >= 0x80000000u && t < 0x94000000u && n < 64) {
        const u32 state = (u32)mem_read16(cpu, t + TH_STATE);
        const u32 prio = mem_read32(cpu, t + TH_PRIO);
        const u32 eprio = mem_read32(cpu, t + TH_EPRIO);
        const u32 susp = mem_read32(cpu, t + TH_SUSPEND);
        const u32 queue = mem_read32(cpu, t + TH_QUEUE);
        const u32 lr = mem_read32(cpu, t + CTX_LR_OFF);
        const u32 srr0 = mem_read32(cpu, t + CTX_SRR0_OFF);
        fprintf(stderr,
                "[threads]   %2d t=0x%08X state=%u(%s) prio=%u eprio=%u susp=%u "
                "queue=0x%08X srr0=0x%08X lr=0x%08X raw=",
                n, t, state, thread_state_name(state), prio, eprio, susp, queue,
                srr0, lr);
        for (u32 off = 0x2C8u; off < 0x2E8u; off += 4u)
            fprintf(stderr, "%08X ", mem_read32(cpu, t + off));
        fprintf(stderr, "\n");
        {
            // Saved volatile/callee-saved registers at the switch point: r3 is
            // the current call's first argument and r31 usually holds `this`
            // for the framework's virtual dispatch.
            const u32 r3 = mem_read32(cpu, t + 3u * 4u);
            const u32 r31 = mem_read32(cpu, t + 31u * 4u);
            if (state == 4u && (r3 != 0u || r31 != 0u)) {
                const u32 vt = (r31 >= 0x80000000u && r31 < 0x94000000u)
                                   ? mem_read32(cpu, r31) : 0u;
                fprintf(stderr,
                        "[dbg]   regs r3=0x%08X r31=0x%08X vptr=0x%08X",
                        r3, r31, vt);
                if (vt >= 0x80000000u && vt < 0x80800000u) {
                    fprintf(stderr, " slots:");
                    for (u32 i = 0; i < 8u; i++)
                        fprintf(stderr, " %08X", mem_read32(cpu, vt + i * 4u));
                }
                fprintf(stderr, "\n");
            }
        }
        if (state == 4u) {
            // A blocking thread's OSContext holds r1 at the switch point, so the
            // stack chain names the call it is parked in.
            const u32 sp = mem_read32(cpu, t + 4u);   // context.gpr[1]
            dbg_dump_backchain_at(cpu, sp, t);
        }
        t = mem_read32(cpu, t + TH_NEXT);
        n++;
    }
    fprintf(stderr, "[threads] %d thread(s)\n", n);
}

static void dbg_fake_input_write(CPUState* cpu) {
    const u32 mgr = mem_read32(cpu, cpu->gpr[13] - 25000u);
    if (mgr < 0x80000000u || mgr >= 0x94000000u)
        return;
    const u32 base = mgr + 0x10000u - 30592u;
    for (u32 ch = 0; ch < 4u; ch++) {
        const u32 p = mem_read32(cpu, base + ch * 16u + 12u);
        if (p < 0x80000000u || p >= 0x94000000u)
            continue;
        mem_write32(cpu, base + ch * 16u, 0u);
        mem_write32(cpu, p + 4u, mem_read32(cpu, p + 4u) | 0x8000u);
        mem_write32(cpu, p + 100u, mem_read32(cpu, p + 100u) | 0x800u);
    }
}

void dbg_block_enter(CPUState* cpu, u32 pc, unsigned long long blocks, u32 last_pc) {
    g_cur_blocks = blocks;
    for (unsigned i = 0; i < g_poke_count; i++) {
        if (!g_poke_done[i] && blocks >= g_poke_at[i]) {
            g_poke_done[i] = true;
            fprintf(stderr, "[poke] 0x%08X = 0x%08X at blk=%llu\n",
                    g_poke_addr[i], g_poke_value[i], blocks);
            mem_write32(cpu, g_poke_addr[i], g_poke_value[i]);
        }
    }
    while (g_threaddump_done_count < g_threaddump_count &&
           blocks >= g_threaddump_at[g_threaddump_done_count]) {
        dbg_thread_dump(cpu, blocks);
        g_threaddump_done_count++;
    }
    if (blocks >= g_pcs_start && blocks < g_pcs_end && g_pcs_dumped == 0u &&
        (blocks & 0xFFFu) == 0ull) {
        u32 slot = (pc * 2654435761u) & (PCS_SLOTS - 1u);
        for (u32 probe = 0; probe < 64u; probe++) {
            const u32 at = (slot + probe) & (PCS_SLOTS - 1u);
            if (g_pcs_cnt[at] == 0u) {
                g_pcs_key[at] = pc;
                g_pcs_cnt[at] = 1u;
                break;
            }
            if (g_pcs_key[at] == pc) {
                g_pcs_cnt[at]++;
                break;
            }
        }
    }
    if (g_pcs_dumped == 0u && g_pcs_end != 0ull && blocks >= g_pcs_end) {
        g_pcs_dumped = 1u;
        fprintf(stderr, "[pcsample] top block-entry PCs in [%llu,%llu):\n",
                g_pcs_start, g_pcs_end);
        for (u32 rank = 0; rank < 40u; rank++) {
            u32 best = 0u, best_cnt = 0u;
            for (u32 i = 0; i < PCS_SLOTS; i++)
                if (g_pcs_cnt[i] > best_cnt) {
                    best_cnt = g_pcs_cnt[i];
                    best = i;
                }
            if (best_cnt == 0u)
                break;
            fprintf(stderr, "[pcsample]   pc=0x%08X samples=%u\n",
                    g_pcs_key[best], best_cnt);
            g_pcs_cnt[best] = 0u;
        }
    }
    if (g_pchit_count != 0u) {
        for (u32 i = 0; i < g_pchit_count; i++) {
            if (g_pchit_pc[i] != pc)
                continue;
            const unsigned long long n = ++g_pchit_hits[i];
            if ((n & (n - 1ull)) == 0ull)
                fprintf(stderr,
                        "[pc hit] pc=0x%08X n=%llu blk=%llu lr=0x%08X "
                        "r3=0x%08X r4=0x%08X r5=0x%08X r15=0x%08X\n",
                        pc, n, blocks, cpu->lr, cpu->gpr[3], cpu->gpr[4],
                        cpu->gpr[5], cpu->gpr[15]);
        }
    }
    if (g_gray_path != NULL && blocks >= g_gray_at &&
        (blocks & 0xFFFFFull) == 0ull) {
        // COLORS_GRAYDUMP_SERIES=N dumps N frames, one every
        // COLORS_GRAYDUMP_STRIDE blocks (default 4M) starting at _AT. The
        // files are named "<path>.<i>".
        char path[512];
        const char* dst = g_gray_path;
        if (g_gray_series > 1u) {
            snprintf(path, sizeof(path), "%s.%u", g_gray_path, g_gray_series_done);
            dst = path;
        }
        FILE* f = fopen(dst, "wb");
        if (f != NULL) {
            fprintf(f, "P5\n%u %u\n255\n", g_gray_w, g_gray_h);
            for (u32 i = 0; i < g_gray_w * g_gray_h; i++)
                fputc(mem_read8(cpu, g_gray_addr + i), f);
            fclose(f);
            fprintf(stderr, "[graydump] %s at blk=%llu (%ux%u)\n",
                    dst, blocks, g_gray_w, g_gray_h);
            g_gray_series_done++;
            if (g_gray_series_done >= g_gray_series)
                g_gray_path = NULL;
            else
                g_gray_at = blocks + g_gray_stride;
        }
    }
    if (g_memhex_len != 0u && blocks >= g_memhex_at) {
        fprintf(stderr, "[memhex] addr=0x%08X blk=%llu:", g_memhex_addr, blocks);
        for (u32 i = 0; i < g_memhex_len; i++)
            fprintf(stderr, " %02X", mem_read8(cpu, g_memhex_addr + i));
        fprintf(stderr, "\n");
        g_memhex_len = 0;
    }
    if (g_memdump_path != NULL && blocks >= g_memdump_at &&
        (blocks & 0xFFFFFull) == 0ull) {
        // Dump a 640x480 RGBA8 guest buffer once (movie frame inspection).
        FILE* f = fopen(g_memdump_path, "wb");
        if (f != NULL) {
            fprintf(f, "P6\n640 480\n255\n");
            for (u32 i = 0; i < 640u * 480u; i++) {
                const u8 px[3] = {mem_read8(cpu, g_memdump_addr + i * 4u),
                                  mem_read8(cpu, g_memdump_addr + i * 4u + 1u),
                                  mem_read8(cpu, g_memdump_addr + i * 4u + 2u)};
                fwrite(px, 1, 3, f);
            }
            fclose(f);
            fprintf(stderr, "[memdump] wrote %s at blk=%llu\n",
                    g_memdump_path, blocks);
            g_memdump_path = NULL;
        }
    }
    if (g_memcheck_addr != 0u && blocks >= g_memcheck_after &&
        (blocks & 0xFFFFFull) == 0ull) {
        u32 sum = 0, nz = 0;
        for (u32 off = 0; off < g_memcheck_len; off += 64u) {
            const u8 b = mem_read8(cpu, g_memcheck_addr + off);
            sum += b;
            if (b != 0u)
                nz++;
        }
        static u32 last_sum = 0xFFFFFFFFu;
        static unsigned long long last_blk = 0;
        if (g_memcheck_trace && (sum != last_sum || (blocks - last_blk) > 0x4000000ull)) {
            last_sum = sum;
            last_blk = blocks;
            fprintf(stderr, "[memchk] blk=%llu addr=0x%08X sum=%u nz=%u/%u\n",
                    blocks, g_memcheck_addr, sum, nz, g_memcheck_len / 64u);
        }
    }
    if (g_pc_hist_after != 0ull && blocks >= g_pc_hist_after &&
        (pc == 0x8000452Cu || pc == 0x800046B8u || pc == 0x80004604u)) {
        static u32 probe_n = 0;
        if ((blocks & 0x3FFFFFu) == 0ull && probe_n < 12u) {
            ++probe_n;
            fprintf(stderr,
                    "[memprobe] pc=0x%08X blk=%llu r3=0x%08X r4=0x%08X "
                    "r5=0x%08X r6=0x%08X lr=0x%08X\n",
                    pc, blocks, cpu->gpr[3], cpu->gpr[4], cpu->gpr[5],
                    cpu->gpr[6], cpu->lr);
        }
    }
    if (g_pc_hist_after != 0ull && blocks >= g_pc_hist_after &&
        (blocks & 0x3FFu) == 0ull &&
        mem_read32(cpu, 0x800000E4u) == 0x807DABB8u) {
        u32 i = 0;
        for (; i < g_pc_hist_used; i++)
            if (g_pc_hist_pc[i] == pc)
                break;
        if (i == g_pc_hist_used && g_pc_hist_used < 64u)
            g_pc_hist_pc[g_pc_hist_used++] = pc;
        if (i < 64u)
            g_pc_hist_n[i]++;
    }
    if ((blocks & 0xFFFFFu) == 0ull) {
        GxStats gs;
        gx_get_stats(&gs);
        if (gs.draws != g_seen_draws) {
            g_seen_draws = gs.draws;
            g_last_draw_blk = blocks;
        }
        if (g_ax_dma_hits != g_seen_ax) {
            g_seen_ax = g_ax_dma_hits;
            g_last_ax_blk = blocks;
        }
    }
    // Reference-count tracing for the smart-pointer control block at
    // 0x80AE6A60 (the object whose destructor jumps through a NULL vtable).
    if (g_trace_idle && pc == 0x803402E0u) {
        static u32 last_c;
        u32 c = mem_read32(cpu, 0xCC003000u);
        if (c != last_c) {
            last_c = c;
            fprintf(stderr, "[irq] dispatch cause=0x%08X pimask=0x%08X dscr=%04X "
                    "h5=0x%08X osmask=0x%08X\n",
                    c, mem_read32(cpu, 0xCC003004u),
                    (unsigned)mem_read16(cpu, 0xCC00500Au),
                    mem_read32(cpu, 0x80003040u + 5u * 4u),
                    mem_read32(cpu, 0x800000C4u) | mem_read32(cpu, 0x800000C8u));
        }
    }
    if (g_trace_idle && pc == 0x80369FACu) {
        static unsigned hits;
        hits++;
        if (mem_read8(cpu, cpu->gpr[13] - 22280u) == 0u)
            g_gxwait_zero++;
        if (hits <= 3 || mem_read8(cpu, cpu->gpr[13] - 22280u) == 0u) {
            u32 q = cpu->gpr[13] - 22288u;
            fprintf(stderr,
                    "[gxwait] #%u blk=%llu flag=%u cb=0x%08X qhead=0x%08X qtail=0x%08X main=0x%08X\n",
                    hits, blocks, mem_read8(cpu, cpu->gpr[13] - 22280u),
                    mem_read32(cpu, cpu->gpr[13] - 22744u),
                    mem_read32(cpu, q), mem_read32(cpu, q + 4u),
                    mem_read32(cpu, 0x800000E4u));
            if (hits > 2000000u) hits = 2000000u;
        }
    }
    if (g_trace_idle && mem_read32(cpu, 0x800000E4u) == 0x807DABB8u)
        g_main_pc_ring[g_main_pc_i++ % 12u] = pc;
    if (g_trace_idle && pc == 0x80343140u) {
        static unsigned rs;
        rs++;
        if (rs <= 10) {
            u32 main_t = 0x807DABB8u;
            fprintf(stderr,
                    "[resched] #%u blk=%llu cur=0x%08X r3=%u readyMask=0x%08X "
                    "flag=%u main_state=%u main_q=0x%08X main_next=0x%08X\n",
                    rs, blocks, mem_read32(cpu, 0x800000E4u), cpu->gpr[3],
                    mem_read32(cpu, cpu->gpr[13] - 23432u),
                    mem_read32(cpu, cpu->gpr[13] - 23436u),
                    mem_read16(cpu, main_t + 712u), mem_read32(cpu, main_t + 732u),
                    mem_read32(cpu, main_t + 736u));
        }
    }
    if (g_sleep_log && pc == 0x80343F80u && blocks >= g_sleep_after) {
        static u32 n = 0;
        if (n++ < 300u)
            fprintf(stderr, "[sleeplog] blk=%llu thread=0x%08X q=0x%08X lr=0x%08X\n",
                    blocks, mem_read32(cpu, 0x800000E4u), cpu->gpr[3], cpu->lr);
    }
    if (g_trace_idle && pc == 0x80343F80u) {
        // OSSleepThread: log the first distinct (queue, caller) pairs so a
        // deadlocked subsystem shows up as a queue nothing ever wakes.
        static u32 seen_q[32];
        static int seen_n;
        const u32 q = cpu->gpr[3];
        if (q != cpu->gpr[13] - 21336u) {  // skip the OS idle thread's own queue
            bool dup = false;
            for (int i = 0; i < seen_n; i++)
                if (seen_q[i] == q) {
                    dup = true;
                    break;
                }
            if (!dup && seen_n < 32) {
                seen_q[seen_n++] = q;
                fprintf(stderr,
                        "[sleep] q=0x%08X lr=0x%08X thread=0x%08X blk=%llu\n",
                        q, cpu->lr, mem_read32(cpu, 0x800000E4u), blocks);
            }
        }
    }
    if (g_trace_idle && pc == 0x80343F80u && cpu->gpr[3] == cpu->gpr[13] - 22288u) {
        g_gxwake_hits++;
        g_gxwake_qhead = mem_read32(cpu, cpu->gpr[3]);
        g_gxwake_state = mem_read16(cpu, 0x807DABB8u + 712u);
    }
    if (g_trace_idle && pc == 0x80369F88u) {
        static int waits;
        if (waits++ < 6)
            fprintf(stderr, "[audioirq] AX wait entry #%d at block %llu queue=0x%08X\n",
                    waits, blocks, cpu->gpr[13] - 22288u);
    }
    if (g_trace_idle && pc == 0x8036A290u) {
        static int cbn;
        if (cbn++ < 6 || (cbn % 1000) == 0)
            fprintf(stderr, "[audioirq] AX callback #%d at block %llu\n", cbn, blocks);
    }
    if (pc == 0x8039E07Cu || pc == 0x803A0084u || pc == 0x803B4380u ||
        pc == 0x803B48C8u || pc == 0x803B4A4Cu || pc == 0x803B4B0Cu ||
        pc == 0x803B5434u)
        g_wpad_hits++;
    if (g_fake_wpad && pc == 0x803A13F0u) {
        // WPADProbe(chan, &type): the SDK reads its per-channel work struct
        // from a static pointer table. With no Bluetooth adapter emulated the
        // first 0xFFFFFFFF means "not connected". Present one connected Wii
        // Remote so the game leaves its "connect a controller" wait.
        const u32 chan = cpu->gpr[3];
        if (chan < 4u) {
            // The SDK's per-channel work struct lives in the WPAD static
            // array (0x80610908 + chan*1672), not the connection table at
            // 0x8060A750 (which points into MEM2).
            u32 base = mem_read32(cpu, 0x8060A750u + chan * 4u);
            if (g_env_fake_wpad_work)
                base = 0x80610908u + chan * 1672u;
            if (base >= 0x80000000u && base < 0x94000000u) {
                static u32 wn = 0;
                if (wn++ < 3u)
                    fprintf(stderr,
                            "[fakewpad] chan=%u base=0x%08X expect=0x%08X\n",
                            chan, base, 0x80610908u + chan * 1672u);
                mem_write32(cpu, base + 2304u, 0u);   // connected
                mem_write8(cpu, base + 2309u, 0u);    // WPAD_DEV_CORE (remote)
                mem_write32(cpu, base + 2336u, 0u);
                if (g_fake_wpad_btn != 0u) {
                    // Feed the raw HID button word used by the report path.
                    mem_write16(cpu, base + 2344u, (u16)g_fake_wpad_btn);
                }
            }
        }
    }
    if (g_env_movie_dump &&
        (pc == 0x8017577Cu || pc == 0x80175748u)) {
        static u32 bn = 0;
        if (bn++ < 12u)
            fprintf(stderr, "[moviedraw] body pc=0x%08X\n", pc);
    }
    if (g_env_movie_dump && pc == 0x80175AB4u) {
        static u32 last = 99u;
        const u32 st = cpu->gpr[0];
        if (st != last) {
            fprintf(stderr, "[moviestate] blk=%llu task=0x%08X %u -> %u\n",
                    blocks, cpu->gpr[31], last, st);
            last = st;
        }
    }
    if (g_env_movie_dump && pc == 0x80175728u) {
        static u32 dn = 0;
        ++dn;
        if (dn <= 8u || (dn & 63u) == 0u) {
            const u32 w = cpu->gpr[3];
            const u32 t = mem_read32(cpu, w + 8u);
            fprintf(stderr,
                    "[moviedraw] n=%u blk=%llu task=0x%08X state=%u "
                    "tex0=0x%08X tex1=0x%08X\n",
                    dn, blocks, t, mem_read32(cpu, t + 8u),
                    mem_read32(cpu, t + 36u), mem_read32(cpu, t + 40u));
        }
    }
    if (g_env_fake_input && pc == 0x80016DD4u) {
        static u32 n = 0;
        if (n++ < 3u) {
            const u32 pp = mem_read32(cpu, cpu->gpr[1] + 20u);
            fprintf(stderr, "[fakeinput] check ptr=0x%08X v4=%08X v100=%08X\n",
                    pp, mem_read32(cpu, pp + 4u),
                    mem_read32(cpu, pp + 100u));
        }
    }
    if (g_env_fake_input &&
        (pc == 0x80040880u || pc == 0x800123C8u))
        dbg_fake_input_write(cpu);
    if (g_env_force_any && pc == 0x80012900u) {
        cpu->gpr[3] = 1u;
    }
    if (g_env_fake_input && pc == 0x80012900u) {
        static u32 n = 0;
        if (n++ < 3u) {
            const u32 mgr = mem_read32(cpu, cpu->gpr[13] - 25000u);
            const u32 st = mgr + 0x10000u - 30592u;
            const u32 pp = mem_read32(cpu, st + 12u);
            fprintf(stderr,
                    "[fakeinput] ret=%u st=0x%08X pp=0x%08X v4=%08X v100=%08X\n",
                    cpu->gpr[3], st, pp, mem_read32(cpu, pp + 4u),
                    mem_read32(cpu, pp + 100u));
        }
    }
    if (g_env_input_debug && pc == 0x80016CA0u) {
        static u32 n = 0;
        if (n++ < 2u) {
            const u32 m = cpu->gpr[3];
            const u32 vt = mem_read32(cpu, m);
            fprintf(stderr, "[input] manager=0x%08X vt=0x%08X slots:", m, vt);
            for (u32 i = 0; i < 12u; i++)
                fprintf(stderr, " %u:%08X", i,
                        mem_read32(cpu, vt + i * 4u));
            fprintf(stderr, "\n");
        }
    }
    if (g_env_fake_input && pc == 0x80016D74u) {
        // Force the game's per-channel controller state (the struct
        // FUN_80016D74 reads) so a button press is visible even when the
        // emulated WPAD report path never fills it.
        const u32 mgr = mem_read32(cpu, cpu->gpr[13] - 25000u);
        if (mgr >= 0x80000000u && mgr < 0x94000000u) {
            const u32 base = mgr + 0x10000u - 30592u;
            for (u32 ch = 0; ch < 4u; ch++) {
                // Mark the channel as connected: struct+0 is the status code
                // (0xFFFFFFFF = no controller), and FUN_80016D74 only looks
                // at the buttons when it is 0 (or -2..-4).
                mem_write32(cpu, base + ch * 16u, 0u);
                const u32 p = mem_read32(cpu, base + ch * 16u + 12u);
                if (p < 0x80000000u || p >= 0x94000000u)
                    continue;
                if (g_env_fake_input_pulse &&
                    (blocks % 2000000ull) >= 200000ull) {
                    /* release: leave the previous state untouched */
                } else if (g_env_fake_input_all) {
                    mem_write32(cpu, p + 4u, 0xFFFFFFFFu);
                    mem_write32(cpu, p + 100u, 0xFFFFFFFFu);
                } else {
                    mem_write32(cpu, p + 4u,
                                mem_read32(cpu, p + 4u) | 0x8000u);
                    mem_write32(cpu, p + 100u,
                                mem_read32(cpu, p + 100u) | 0x800u);
                }
            }
            static u32 fn = 0;
            if (fn++ < 3u) {
                const u32 p0 = mem_read32(cpu, base + 12u);
                fprintf(stderr,
                        "[fakeinput] mgr=0x%08X base=0x%08X p0=0x%08X "
                        "btn=%08X b100=%08X\n",
                        mgr, base, p0,
                        p0 >= 0x80000000u ? mem_read32(cpu, p0 + 4u) : 0u,
                        p0 >= 0x80000000u ? mem_read32(cpu, p0 + 100u) : 0u);
            }
        }
    }
    if (g_env_input_debug && pc == 0x80016D74u) {
        static u32 n = 0;
        if (n++ < 6u) {
            const u32 mgr = mem_read32(cpu, cpu->gpr[13] - 25000u);
            fprintf(stderr,
                    "[input] scan arg=%u r13=0x%08X mgr=0x%08X base=0x%08X\n",
                    cpu->gpr[3], cpu->gpr[13], mgr, mgr + 0x10000u - 30592u);
            for (u32 ch = 0; ch < 4u; ch++) {
                const u32 b = mgr + 0x10000u - 30592u + ch * 16u;
                const u32 p = mem_read32(cpu, b + 12u);
                fprintf(stderr,
                        "[input]  ch%u: %08X %08X %08X %08X | p4=%08X p100=%08X\n",
                        ch, mem_read32(cpu, b), mem_read32(cpu, b + 4u),
                        mem_read32(cpu, b + 8u), p,
                        (p >= 0x80000000u && p < 0x94000000u)
                            ? mem_read32(cpu, p + 4u) : 0u,
                        (p >= 0x80000000u && p < 0x94000000u)
                            ? mem_read32(cpu, p + 100u) : 0u);
            }
        }
    }
    if (g_env_input_debug && pc == 0x80016E24u) {
        static u32 n, yes;
        ++n;
        if (cpu->gpr[3] != 0u) ++yes;
        if ((n & 1023u) == 0u || yes <= 3u)
            fprintf(stderr, "[input] check n=%u blk=%llu r3=%u yes=%u\n",
                    n, blocks, cpu->gpr[3], yes);
    }
    if (g_env_movie_dump && pc == 0x8067595Cu) {
        static u32 n = 0;
        if (n++ < 4u) {
            const u32 p = cpu->gpr[5];
            fprintf(stderr,
                    "[pump] p3=0x%08X p4=0x%08X p5=0x%08X cb=0x%08X\n",
                    cpu->gpr[3], cpu->gpr[4], p,
                    p >= 0x80000000u ? mem_read32(cpu, p + 104u) : 0u);
        }
    }
    if (g_env_movie_dump && pc == 0x80660418u) {
        static u32 n = 0;
        if (n++ < 6u) {
            const u32 h = cpu->gpr[3];
            const u32 si = cpu->gpr[4];
            const u32 base = h + si * 116u;
            fprintf(stderr,
                    "[stream] h=0x%08X si=%u flag=0x%08X ctx=0x%08X "
                    "a5056=0x%08X a5060=0x%08X\n",
                    h, si, mem_read32(cpu, base + 5052u),
                    mem_read32(cpu, base + 5124u),
                    mem_read32(cpu, base + 5056u),
                    mem_read32(cpu, base + 5060u));
        }
    }
    if (g_env_movie_dump && pc == 0x8066F478u) {
        static u32 fn = 0;
        if (cpu->gpr[4] == 2u && fn++ < 6u) {
            const u32 tbl = mem_read32(cpu, cpu->gpr[3] + cpu->gpr[4] * 68u + 8164u);
            fprintf(stderr,
                    "[codec] h=0x%08X idx=%u tbl=0x%08X cb=0x%08X\n",
                    cpu->gpr[3], cpu->gpr[4], tbl,
                    tbl >= 0x80000000u ? mem_read32(cpu, tbl + cpu->gpr[5] * 4u)
                                       : 0u);
        }
    }
    if (g_env_movie_dump && pc == 0x80674BC8u &&
        (cpu->lr == 0x8066E8CCu || cpu->lr == 0x8066E8F4u ||
         cpu->lr == 0x8066E988u)) {
        static u32 n = 0;
        if (n++ < 12u)
            fprintf(stderr,
                    "[range] lr=0x%08X a=%u b=%u c=%u d=%u\n",
                    cpu->lr, cpu->gpr[3], cpu->gpr[4], cpu->gpr[5],
                    cpu->gpr[6]);
    }
    // The retail Sofdec player stalls after its first frame (the playback
    // clock never leaves 0, so no later frame is ever "due"). That leaves the
    // boot movie spinning forever and the game stuck on the black loading
    // screen. Treat the movie as finished once it has had time to play so the
    // rest of boot (and the title screen) can run. Disable with
    // COLORS_NO_SKIP_MOVIE=1, or tune the threshold with COLORS_MOVIE_END=<blk>.
    if (!g_env_no_skip_movie && pc == 0x80175A44u &&
        blocks >= g_env_movie_end &&
        cpu->gpr[3] == 2u) {
        // Experiment: pretend the SFD reached end-of-stream so the game's
        // movie task can leave state 3 and the scene can advance.
        static u32 n = 0;
        if (n++ < 4u)
            fprintf(stderr, "[movieend] force stat=3 at blk=%llu\n", blocks);
        cpu->gpr[3] = 3u;
    }
    if (g_env_movie_clock && pc == 0x80175AECu) {
        // Experiment: the SFD playback clock (handle+4136) stays at 0, so no
        // frame after the first is ever "due". Advance it by one 32 kHz tick
        // per decode attempt and see whether the decoder starts producing.
        const u32 h = 0x92F06560u;
        const u32 inc = g_env_movie_clock_inc;
        mem_write32(cpu, h + 4136u,
                    mem_read32(cpu, h + 4136u) + inc);
    }
    if (g_env_movie_dump && pc == 0x80175AECu) {
        static u32 dec_n, dec_ok;
        ++dec_n;
        if (cpu->gpr[3] != 0u) ++dec_ok;
        if ((dec_n & 63u) == 0u || dec_ok <= 3u)
            fprintf(stderr, "[moviedec] n=%u blk=%llu r3=%u ok=%u "
                    "hstate=%u data=%u a92=%u npic=%u pic0=%08X/%d "
                    "a4140=%u a4136=%u a4204=%u a2644=%u tcb=%08X\n",
                    dec_n, blocks, cpu->gpr[3], dec_ok,
                    mem_read32(cpu, 0x92F06560u + 84u),
                    mem_read32(cpu, 0x92F06560u + 2648u),
                    mem_read32(cpu, 0x92F06560u + 92u),
                    mem_read32(cpu, 0x92F06560u + 10220u),
                    mem_read32(cpu, 0x92F06560u + 10232u),
                    (s32)mem_read32(cpu, 0x92F06560u + 10232u + 100u),
                    mem_read32(cpu, 0x92F06560u + 4140u),
                    mem_read32(cpu, 0x92F06560u + 4136u),
                    mem_read32(cpu, 0x92F06560u + 4204u),
                    mem_read32(cpu, 0x92F06560u + 2644u),
                    mem_read32(cpu, 0x92F06560u + 3488u));
    }
    if (g_env_movie_dump && pc == 0x80175AECu &&
        cpu->gpr[3] != 0u) {
        const u32 t = cpu->gpr[31];
        static u32 once = 0;
        if (once++ < 4u && t >= 0x80000000u && t < 0x94000000u) {
            fprintf(stderr, "[movie] task=0x%08X:", t);
            for (u32 i = 0; i < 0x40u; i += 4u)
                fprintf(stderr, " %08X", mem_read32(cpu, t + i));
            fprintf(stderr, "\n[movie]  buf0=0x%08X buf1=0x%08X out=0x%08X\n",
                    mem_read32(cpu, t + 12u), mem_read32(cpu, t + 16u),
                    mem_read32(cpu, t + 20u));
            const u32 b0 = mem_read32(cpu, t + 12u);
            const u32 b1 = mem_read32(cpu, t + 16u);
            for (u32 k = 0; k < 2u; k++) {
                const u32 b = k == 0u ? b0 : b1;
                if (b >= 0x80000000u && b < 0x94000000u) {
                    fprintf(stderr, "[movie]  b%u:", k);
                    for (u32 i = 0; i < 32u; i += 4u)
                        fprintf(stderr, " %08X", mem_read32(cpu, b + i));
                    fprintf(stderr, "\n");
                }
            }
        }
    }
    if (g_env_sfd_pool_fix && pc == 0x8064163Cu) {
        // Experiment: mwPlyCreateSofdec is about to validate the config with
        // FUN_806402CC, which reads the frame-pool descriptor from +40/+44/+48.
        // The game only fills +24 (pointer) / +28 (size) / +16 (count), so mirror
        // them into the descriptor the reset looks at.
        // r29 = the config, r30 = the player object whose +8 holds the 76-byte
        // copy mw_sfd_start_ex validates.
        const u32 cfg = cpu->gpr[29];
        const u32 ply = cpu->gpr[30];
        if (cfg >= 0x80000000u && cfg < 0x94000000u &&
            ply >= 0x80000000u && ply < 0x94000000u) {
            const u32 ptr = mem_read32(cpu, cfg + 24u);
            const u32 size = mem_read32(cpu, cfg + 28u);
            const u32 count = mem_read32(cpu, cfg + 16u);
            static u32 n = 0;
            if (n++ < 4u)
                fprintf(stderr,
                        "[sfdpool] cfg=0x%08X player=0x%08X pool=0x%08X "
                        "size=0x%X count=%u\n",
                        cfg, ply, ptr, size, count);
            mem_write32(cpu, ply + 8u + 40u, ptr);
            mem_write32(cpu, ply + 8u + 44u, size);
            mem_write32(cpu, ply + 8u + 48u, count);
        }
    }
    if (g_media_debug) {
        int slot = -1;
        for (u32 i = 0; i < 10u; i++)
            if (g_media_pcs[i] == pc) slot = (int)i;
        if (slot >= 0) {
            if ((g_media_seen[slot]++ & 0xFFFu) == 0u || g_media_seen[slot] == 1u)
                fprintf(stderr,
                        "[media] pc=0x%08X n=%u blk=%llu r3=0x%08X r4=0x%08X "
                        "lr=0x%08X\n",
                        pc, g_media_seen[slot], blocks, cpu->gpr[3], cpu->gpr[4],
                        cpu->lr);
            if (pc == 0x8064019Cu) {
                const u32 c = cpu->gpr[30];
                fprintf(stderr,
                        "[media] builder reset -> r3=%u cfg=0x%08X "
                        "+0=%u +16=%u +24=%u +28=%u +36=%u +40=%u +44=%u "
                        "+48=%u\n",
                        cpu->gpr[3], c,
                        mem_read32(cpu, c + 0u), mem_read32(cpu, c + 16u),
                        mem_read32(cpu, c + 24u), mem_read32(cpu, c + 28u),
                        mem_read32(cpu, c + 36u), mem_read32(cpu, c + 40u),
                        mem_read32(cpu, c + 44u), mem_read32(cpu, c + 48u));
            }
            if (pc == 0x80641398u) {
                const u32 c = cpu->gpr[3];
                fprintf(stderr,
                        "[media] mwPly create(config=0x%08X) +16=%u +24=%u "
                        "+36=%u +40=%u +44=%u +48=%u +52=%u\n",
                        c,
                        c >= 0x80000000u ? mem_read32(cpu, c + 16u) : 0u,
                        c >= 0x80000000u ? mem_read32(cpu, c + 24u) : 0u,
                        c >= 0x80000000u ? mem_read32(cpu, c + 36u) : 0u,
                        c >= 0x80000000u ? mem_read32(cpu, c + 40u) : 0u,
                        c >= 0x80000000u ? mem_read32(cpu, c + 44u) : 0u,
                        c >= 0x80000000u ? mem_read32(cpu, c + 48u) : 0u,
                        c >= 0x80000000u ? mem_read32(cpu, c + 52u) : 0u);
                fprintf(stderr, "[media]   create lr=0x%08X chain:", cpu->lr);
                dbg_print_pc_history();
                fprintf(stderr, "\n");
                fprintf(stderr, "[media]   config dump:");
                for (u32 i = 0; i < 0x40u && c >= 0x80000000u; i += 4u)
                    fprintf(stderr, " %08X", mem_read32(cpu, c + i));
                fprintf(stderr, "\n");
            }
            if (pc == 0x806409B8u && g_media_seen[slot] == 1u) {
                fprintf(stderr,
                        "[media] sfd_start_ex(ctx=0x%08X, 0x%08X) lr=0x%08X "
                        "chain:", cpu->gpr[3], cpu->gpr[4], cpu->lr);
                dbg_print_pc_history();
                fprintf(stderr, "\n");
            }
            if (pc == 0x80640A0Cu) {
                const u32 c = cpu->gpr[25];
                fprintf(stderr,
                        "[media] sfd_start_ex reset -> r3=%u ctx=0x%08X "
                        "w=%u h=%u f0=%u f1=%u f2=%u | +8=%u +12=%u "
                        "+40=%u +44=%u +48=%u\n",
                        cpu->gpr[3], c,
                        mem_read32(cpu, c + 8u), mem_read32(cpu, c + 12u),
                        mem_read32(cpu, c + 40u), mem_read32(cpu, c + 44u),
                        mem_read32(cpu, c + 48u),
                        mem_read32(cpu, c + 8u), mem_read32(cpu, c + 12u),
                        mem_read32(cpu, c + 40u), mem_read32(cpu, c + 44u),
                        mem_read32(cpu, c + 48u));
            }
            if (pc == 0x8063FB88u && g_media_seen[slot] == 1u) {
                fprintf(stderr, "[media]   caller chain (oldest first):");
                dbg_print_pc_history();
                fprintf(stderr, "\n");
            }
        }
        if (slot >= 0 && g_media_seen[slot] == 1u &&
            cpu->gpr[3] >= 0x80000000u &&
            cpu->gpr[3] < 0x94000000u) {
            const u32 o = cpu->gpr[3];
            fprintf(stderr, "[media]   object 0x%08X:", o);
            for (u32 i = 0; i < 0xb0u; i += 4u)
                fprintf(stderr, " %08X", mem_read32(cpu, o + i));
            fprintf(stderr, "\n");
            const u32 o2 = cpu->gpr[4];
            if (o2 >= 0x80000000u && o2 < 0x94000000u) {
                fprintf(stderr, "[media]   arg2 0x%08X:", o2);
                for (u32 i = 0; i < 0xb0u; i += 4u)
                    fprintf(stderr, " %08X", mem_read32(cpu, o2 + i));
                fprintf(stderr, "\n");
            }
        }
    }
    if (g_wwatch_active && pc == 0x80004714u) {
        const u32 dst = cpu->gpr[3];
        if (dst >= g_wwatch_lo && dst < g_wwatch_hi) {
            static u32 n = 0;
            if (n++ < 12u)
                fprintf(stderr,
                        "[memset] dst=0x%08X value=0x%08X len=0x%X lr=0x%08X\n",
                        dst, cpu->gpr[4], cpu->gpr[5], cpu->lr);
        }
    }
    switch (pc) {
    case 0x803B2FB0u: g_wpad_pc[0]++; break;
    case 0x803AE820u: g_wpad_pc[1]++; break;
    case 0x803B2EA0u: g_wpad_pc[2]++; break;
    case 0x803A68A0u: g_wpad_pc[3]++; break;
    case 0x803A6C20u: g_wpad_pc[4]++; break;
    case 0x803AF090u: g_wpad_pc[5]++; break;
    case 0x803A13F0u: g_wpad_pc[6]++; break;
    case 0x803B4450u: g_wpad_pc[7]++; break;
    case 0x803A6610u: g_wpad_pc[8]++; break;
    case 0x803AE740u: g_wpad_pc[9]++; break;
    case 0x803A6EE0u: g_wpad_pc[10]++; break;
    case 0x803B5070u: g_wpad_pc[11]++; break;
    default: break;
    }
    if (pc == 0x80217294u)
        g_cri_post_hits++;
    if (pc == 0x80216A60u)
        g_cri_enqueue_hits++;
    if (pc == 0x80358330u)
        g_ai_handler_hits++;
    if (pc == 0x8036A290u)
        g_ax_dma_hits++;
    if (g_trace_idle && (pc == 0x80358330u || pc == 0x8036A290u)) {
        static int h_ai;
        static int h_cb;
        if (pc == 0x80358330u) {
            h_ai++;
            if (h_ai <= 3 || (h_ai % 10) == 0)
                fprintf(stderr,
                        "[audioirq] AI handler hit %d cb=0x%08X cb2=0x%08X dscr=%04X\n",
                        h_ai, mem_read32(cpu, cpu->gpr[13] - 22744u),
                        mem_read32(cpu, cpu->gpr[13] - 22748u),
                        mem_read16(cpu, 0xCC00500Au));
        }
        if (pc == 0x8036A290u && h_cb++ < 3)
            fprintf(stderr, "[audioirq] AX DMA callback hit %d\n", h_cb);
    }
    if (g_osreport && pc == 0x8033BFB0u && g_osreport_lines < 4000u) {
        dbg_osreport(cpu);
    }
    if (g_trace_files && (pc == 0x80019C3Cu || pc == 0x800A6A38u)) {
        // Game-side file wrapper. The path comes from a per-version register,
        // so probe r3..r6 and keep the first that looks like a path string.
        char name[96] = {0};
        int best = -1;
        for (int r = 3; r <= 6; r++) {
            const u32 sp = cpu->gpr[r];
            if (sp < 0x80000000u || sp >= 0x94000000u)
                continue;
            char cand[64];
            size_t i = 0;
            bool printable = true;
            for (; i + 1u < sizeof cand; i++) {
                char c = (char)mem_read8(cpu, sp + (u32)i);
                cand[i] = c;
                if (c == '\0')
                    break;
                if (c < 0x20 || c > 0x7E || i > 40u) {
                    printable = false;
                    break;
                }
            }
            cand[i] = '\0';
            if (printable && i >= 3u) {
                best = r;
                snprintf(name, sizeof name, "%s", cand);
                break;
            }
        }
        fprintf(stderr, "[file] pc=0x%08X r%d \"%s\" lr=0x%08X\n", pc, best,
                name, cpu->lr);
    }
    if (g_trace_idle && pc == 0x803BD094u) {
        static int spin_hits;
        if (spin_hits++ < 3) {
            fprintf(stderr, "[spin] block %llu r31=0x%08X count=[+8]=0x%08X idx=%u\n",
                    blocks, cpu->gpr[31], mem_read32(cpu, cpu->gpr[31] + 8u),
                    cpu->gpr[17]);
            fprintf(stderr, "[spin]   buf:");
            for (u32 i = 0; i < 0x20u; i++)
                fprintf(stderr, " %02X", (unsigned)mem_read8(cpu, cpu->gpr[31] + i));
            fprintf(stderr, "\n");
            dbg_dump_backchain(cpu);
        }
    }
    if (g_trace_idle && pc == 0x803EBF48u) {
        static bool idle_reported;
        if (!idle_reported) {
            idle_reported = true;
            fprintf(stderr, "[idle] system idle at block %llu\n", blocks);
            fprintf(stderr, "[idle]   pcs:");
            dbg_print_pc_history();
            fprintf(stderr, "\n[idle]   curThread=0x%08X r13-21332/36=%08X/%08X\n",
                    mem_read32(cpu, 0x800000E4u),
                    mem_read32(cpu, cpu->gpr[13] - 21332u),
                    mem_read32(cpu, cpu->gpr[13] - 21336u));
            fprintf(stderr, "[idle]   sleepQ=0x%08X timebase=0x%llX dec=%08X\n",
                    mem_read32(cpu, 0x808F3910u),
                    (unsigned long long)cpu->timebase, interrupt_get_decrementer());
            for (u32 a = 0x807C6500u; a + 0x300u < 0x8091DC20u; a += 4u) {
                u16 st = mem_read16(cpu, a + 712u);
                u16 at = mem_read16(cpu, a + 714u);
                s32 su = (s32)mem_read32(cpu, a + 716u);
                s32 pr = (s32)mem_read32(cpu, a + 720u);
                if ((st == 1u || st == 2u || st == 4u) && (at & 0xFFFEu) == 0u &&
                    su >= 0 && su < 8 && pr >= 0 && pr < 32)
                    fprintf(stderr, "[idle]   thr 0x%08X st=%u prio=%d q=0x%08X next=0x%08X\n",
                            a, st, pr, mem_read32(cpu, a + 732u),
                            mem_read32(cpu, a + 736u));
            }
        }
    }
    if (g_trace_ref) {
        const u32 block = 0x80AE6A60u;
        if (pc == 0x8033AB20u || pc == 0x8033AB40u || pc == 0x8033AB50u ||
            pc == 0x8033AB60u || pc == 0x8033AB70u) {
            fprintf(stderr, "[reg] set pc=0x%08X value=0x%08X lr=0x%08X\n",
                    pc, cpu->gpr[3], cpu->lr);
        }
        if (pc == 0x803DD930u) {
            fprintf(stderr, "[pool] init @%llu pool=0x%08X base=0x%08X size=0x%08X lr=0x%08X\n",
                    blocks, cpu->gpr[3], cpu->gpr[4], cpu->gpr[5], cpu->lr);
        }
        if (pc == 0x80012948u) {           // ref assign(dst, src)
            u32 dst = cpu->gpr[3], src = cpu->gpr[4];
            u32 s = mem_read32(cpu, src);
            u32 d = mem_read32(cpu, dst);
            if (s == block || d == block)
                fprintf(stderr, "[ref] assign dst=0x%08X(%08X) src=0x%08X(%08X) lr=0x%08X\n",
                        dst, d, src, s, cpu->lr);
        }
        if (pc == 0x8001296Cu) {           // ref release(holder)
            u32 holder = cpu->gpr[3];
            u32 b = holder ? mem_read32(cpu, holder) : 0u;
            if (b == block)
                fprintf(stderr, "[ref] release holder=0x%08X block=0x%08X strong=%u weak=%u lr=0x%08X\n",
                        holder, b, mem_read32(cpu, block), mem_read32(cpu, block + 4u), cpu->lr);
        }
        if (pc == 0x80012A08u) {           // atomic decrement
            u32 a = cpu->gpr[3];
            if (a == block || a == block + 4u)
                fprintf(stderr, "[ref] dec 0x%08X %u->%u lr=0x%08X\n",
                        a, mem_read32(cpu, a), mem_read32(cpu, a) - 1u, cpu->lr);
        }
    }
    if (pc == 0x806E6C54u && g_first_wait_dump && !g_wait_dumped) {
        g_wait_dumped = true;
        fprintf(stderr, "[wait] first hit: flag=%u state=%u b28=%u b29=%u blk0=%08X%08X\n",
                mem_read32(cpu, cpu->gpr[13] - 25224u),
                mem_read8(cpu, 0x808E5480u + 41u),
                mem_read8(cpu, 0x808E5480u + 28u),
                mem_read8(cpu, 0x808E5480u + 29u),
                mem_read32(cpu, 0x808E5480u),
                mem_read32(cpu, 0x808E5484u));
    }
    // Minimal DSP task-completion HLE.
    //
    // The AX driver builds an AXPB at 0x807E4D40 whose +40 callback is
    // 0x8035A290 ("li r0,1; stw r0,-22568(r13)") and then waits at
    // 0x8035A3E8 for that flag. On hardware the DSP ucode runs the task
    // and calls the callback. We have no ucode, so perform the callback's
    // effect (set the AX busy/complete flag) once the guest is waiting.
    if (g_trace_ref && pc == 0x8011C77Cu && !g_memcpy_reported) {
        u32 stream = mem_read32(cpu, cpu->gpr[13] - 22360u);
        fprintf(stderr, "[stream] global=0x%08X", stream);
        if (stream >= 0x80000000u && stream < 0x94000000u) {
            for (u32 i = 0; i < 0x28u; i += 4u)
                fprintf(stderr, " %08X", mem_read32(cpu, stream + i));
        }
        fprintf(stderr, "\n[stream] obj@0x807FB230:");
        for (u32 i = 0; i < 0x28u; i += 4u)
            fprintf(stderr, " %08X", mem_read32(cpu, 0x807FB230u + i));
        fprintf(stderr, "\n");
    }
    // Defensive clamp for the cache-op helpers (DCStore/DCInvalidate/
    // DCZeroRange): absurd lengths make the no-op loops run for hundreds of
    // millions of dispatches (same bogus-size source as the memcpy).
    if (g_clamp_copy &&
        (pc == 0x8033B0C0u || pc == 0x8033B0D0u || pc == 0x8033B100u) &&
        cpu->gpr[4] >= 0x10000000u) {
        fprintf(stderr, "[cache-clamp] pc=0x%08X addr=0x%08X len=0x%08X lr=0x%08X\n",
                pc, cpu->gpr[3], cpu->gpr[4], cpu->lr);
        cpu->gpr[4] = 0u;
    }
    if (g_trace_ref && (pc == 0x803DA754u || pc == 0x803DA7ACu || pc == 0x803DA7C0u)) {
        fprintf(stderr, "[ref] texbuf @%llu pc=0x%08X obj=0x%08X buf=0x%08X size=0x%08X lr=0x%08X\n",
                blocks, pc, cpu->gpr[29], cpu->gpr[3], cpu->gpr[31], cpu->lr);
    }
    if (g_trace_ref && pc == 0x803CC354u) {
        fprintf(stderr, "[ref] refblock @%llu -> 0x%08X (requested 16)\n",
                blocks, cpu->gpr[3]);
    }
    if (g_trace_ref && pc == 0x80004660u && cpu->gpr[5] >= 0x10000u) {
        fprintf(stderr, "[ref] big fill dst=0x%08X len=0x%08X val=0x%02X lr=0x%08X msr=0x%08X\n",
                cpu->gpr[3], cpu->gpr[5], (unsigned)(cpu->gpr[4] & 0xFFu),
                cpu->lr, cpu->msr);
        dbg_dump_backchain(cpu);
    }
    if (g_trace_ref && pc == 0x800046B8u) {
        u32 r3 = cpu->gpr[3];
        if (r3 <= 0x80AE6A60u && r3 + 0x8000u > 0x80AE6A60u)
            fprintf(stderr, "[ref] fill-loop r3=0x%08X r7=0x%08X r5=0x%08X lr=0x%08X\n",
                    r3, cpu->gpr[7], cpu->gpr[5], cpu->lr);
    }
    if (g_trace_ref && pc == 0x800046B8u) {
        static bool hit2;
        u32 lo = cpu->gpr[3] + 4u, hi = cpu->gpr[3] + 33u;
        if (!hit2 && lo <= 0x80AE6A60u && 0x80AE6A60u < hi) {
            hit2 = true;
            fprintf(stderr, "[fill-loop] cur=0x%08X val=0x%08X lr=0x%08X r5=0x%08X\n",
                    cpu->gpr[3], cpu->gpr[7], cpu->lr, cpu->gpr[5]);
        }
    }
    if (g_trace_ref && pc == 0x80004660u) {
        static bool hit;
        static int big_hits;
        u32 dst = cpu->gpr[3], len = cpu->gpr[5];
        if (big_hits < 8 && len >= 0x1000u)
            fprintf(stderr, "[fill] dst=0x%08X len=0x%08X val=0x%02X lr=0x%08X\n",
                    dst, len, (unsigned)(cpu->gpr[4] & 0xFFu), cpu->lr), big_hits++;
        if (!hit && dst <= 0x80AE6A60u && (u64)dst + len > 0x80AE6A60u) {
            hit = true;
            fprintf(stderr, "[fill6A60] dst=0x%08X len=0x%08X val=0x%02X lr=0x%08X\n",
                    dst, len, (unsigned)(cpu->gpr[4] & 0xFFu), cpu->lr);
        }
    }
    if (g_trace_ref && pc == 0x80004714u) {
        static bool hit;
        u32 dst = cpu->gpr[3], len = cpu->gpr[5];
        if (!hit && dst <= 0x80AE6A60u && (u64)dst + len > 0x80AE6A60u) {
            hit = true;
            fprintf(stderr, "[memset6A60] dst=0x%08X len=0x%08X lr=0x%08X\n", dst, len, cpu->lr);
        }
    }
    if (g_trace_ref) {
        static u32 o8_prev, o0_prev;
        const u32 o8 = mem_read32(cpu, 0x80AE6A60u + 8u);
        const u32 o0 = mem_read32(cpu, 0x80AE6A60u);
        if (o8 != o8_prev || (o0 != o0_prev && (o0 == 0u || o8 == 0u))) {
            fprintf(stderr, "[obj6A60] refc %08X->%08X iface %08X->%08X at pc=0x%08X lr=0x%08X\n",
                    o0_prev, o0, o8_prev, o8, last_pc, cpu->lr);
            o8_prev = o8;
        }
        o0_prev = o0;
    }
    if (g_trace_ref && pc == 0x800129A0u) {
        static int vt_hits;
        u32 obj = cpu->gpr[31];
        u32 vt = (obj >= 0x80000000u && obj < 0x94000000u)
                     ? mem_read32(cpu, obj + 8u) : 0xDEADBEEFu;
        vt_hits++;
        if (vt == 0u && vt_hits < 1000000) {
            fprintf(stderr, "[vcall] NULL iface: obj=0x%08X refcount=%d (hit %d)\n",
                    obj, (int)mem_read32(cpu, obj), vt_hits);
            for (u32 i = 0; i < 0x40u; i += 4u)
                fprintf(stderr, "    +%02X %08X\n", i, mem_read32(cpu, obj + i));
            vt_hits = 1000000;  // report once
        }
    }
    if (g_trace_ref) {
        static u32 d24_prev;
        const u32 d24 = mem_read32(cpu, 0x807FAF84u + 24u);
        if (d24 != d24_prev && d24 >= 0x80000000u) {
            fprintf(stderr, "[d24] 0x%08X -> 0x%08X at pc=0x%08X lr=0x%08X\n",
                    d24_prev, d24, last_pc, cpu->lr);
            d24_prev = d24;
        } else if (d24 != d24_prev) {
            d24_prev = d24;
        }
    }
    if (g_trace_ref && pc == 0x8036853Cu) {
        static int desc_hits;
        if (desc_hits++ < 4) {
            DspDmaSnapshot dma;
            dsp_dma_snapshot(&dma);
            fprintf(stderr, "[desc] flags=%u/%u d8=%08X d20=%08X d24=%08X d28=%08X d32=%08X "
                    "dma30=%04X%04X dma38=%04X%04X\n",
                    mem_read8(cpu, cpu->gpr[13] - 22319u),
                    mem_read8(cpu, cpu->gpr[13] - 22320u),
                    mem_read32(cpu, 0x807FAF84u + 8u),
                    mem_read32(cpu, 0x807FAF84u + 20u),
                    mem_read32(cpu, 0x807FAF84u + 24u),
                    mem_read32(cpu, 0x807FAF84u + 28u),
                    mem_read32(cpu, 0x807FAF84u + 32u),
                    dma.aram_dma_ar_lo, dma.aram_dma_ar_hi,
                    dma.dma_ptr_lo, dma.dma_ptr_hi);
        }
    }
    if (g_trace_ref) {
        // Watch the AX object's size field (0x807FB230+28) for the first
        // write that makes it absurd.
        static u32 ax_size_prev;
        const u32 ax_size = mem_read32(cpu, 0x807FB230u + 28u);
        if (ax_size != ax_size_prev) {
            if (ax_size >= 0x10000000u)
                fprintf(stderr, "[axsize] 0x%08X -> 0x%08X at pc=0x%08X lr=0x%08X\n",
                        ax_size_prev, ax_size, last_pc, cpu->lr);
            ax_size_prev = ax_size;
        }
    }
    if (g_trace_ref && pc == 0x8011C780u) {
        static int sz_hits;
        if (sz_hits++ < 6)
        {
            u32 m = mem_read32(cpu, cpu->gpr[13] - 22360u);
            fprintf(stderr, "[size] lookup returned 0x%08X (r25 <- r3)\n", cpu->gpr[3]);
            if (sz_hits <= 3) {
                fprintf(stderr, "[size] mgr global = 0x%08X\n", m);
            }
            if (sz_hits <= 3 && m >= 0x80000000u && m < 0x94000000u) {
                fprintf(stderr, "[size] mgr@0x%08X:", m);
                for (u32 i = 0; i < 0x28u; i += 4u)
                    fprintf(stderr, " %08X", mem_read32(cpu, m + i));
                fprintf(stderr, "\n[size] obj@0x807FB230:");
                for (u32 i = 0; i < 0x28u; i += 4u)
                    fprintf(stderr, " %08X", mem_read32(cpu, 0x807FB230u + i));
                fprintf(stderr, "\n");
            }
        }
    }
    // Defensive clamp: a runaway memcpy (absurd length) would overwrite OS
    // low memory and destroy the machine state. Turn it into a no-op so the
    // rest of boot stays observable. Diagnostic only.
    if (g_clamp_copy && pc == 0x800043C4u && cpu->gpr[5] >= 0x10000000u) {
        fprintf(stderr, "[memcpy-clamp] dst=0x%08X src=0x%08X len=0x%08X lr=0x%08X\n",
                cpu->gpr[3], cpu->gpr[4], cpu->gpr[5], cpu->lr);
        cpu->gpr[5] = 0u;
    }
    // Catch a memcpy call with a non-guest source pointer or absurd length.
    if (pc == 0x800043C4u && !g_memcpy_reported && cpu->gpr[5] >= 0x10000000u) {
        g_memcpy_reported = true;
        fprintf(stderr, "[badcopy] call dst=0x%08X src=0x%08X len=0x%08X lr=0x%08X\n",
                cpu->gpr[3], cpu->gpr[4], cpu->gpr[5], cpu->lr);
        dbg_dump_backchain(cpu);
        {
            u32 h = mem_read32(cpu, cpu->gpr[13] - 24720u);
            fprintf(stderr, "[badcopy] stream@0x%08X:", h);
            if (h >= 0x80000000u && h < 0x94000000u)
                for (u32 i = 0; i < 0x28u; i += 4u)
                    fprintf(stderr, " %08X", mem_read32(cpu, h + i));
            fprintf(stderr, "\n");
        }
        fprintf(stderr, "[badcopy] filehandle[r13-24720]=0x%08X readcb=[r13-21568]=0x%08X\n",
                mem_read32(cpu, cpu->gpr[13] - 24720u),
                mem_read32(cpu, cpu->gpr[13] - 21568u));
        fprintf(stderr, "[badcopy] info@0x807FB8B0:");
        for (u32 i = 0; i < 0x28u; i += 4u)
            fprintf(stderr, " %08X", mem_read32(cpu, 0x807FB8B0u + i));
        fprintf(stderr, "\n[badcopy] curfile@0x807FAF84:");
        for (u32 i = 0; i < 0x28u; i += 4u)
            fprintf(stderr, " %08X", mem_read32(cpu, 0x807FAF84u + i));
        fprintf(stderr, "\n[badcopy] caller stack:");
        for (u32 i = 0; i < 0x30u; i += 4u)
            fprintf(stderr, " %08X", mem_read32(cpu, cpu->gpr[1] + i));
        fprintf(stderr, "\n");
    }
    if (pc == 0x80004434u && !g_memcpy_reported &&
        (cpu->gpr[4] < 0x80000000u || cpu->gpr[4] >= 0x94000000u)) {
        g_memcpy_reported = true;
        fprintf(stderr, "[badcopy] pc=0x%08X src=0x%08X len=0x%08X dst=0x%08X lr=0x%08X\n",
                pc, cpu->gpr[4], cpu->gpr[5], cpu->gpr[8], cpu->lr);
        dbg_dump_backchain(cpu);
        fprintf(stderr, "[badcopy] stack:");
        for (u32 i = 0; i < 0x40u; i += 4u) {
            u32 v = mem_read32(cpu, cpu->gpr[1] + i);
            fprintf(stderr, " %08X", v);
        }
        fprintf(stderr, "\n");
    }
    if (pc == 0x8035A3E8u && g_dsp_task_hle)
        mem_write32(cpu, cpu->gpr[13] - 22568u, 1u);
    if (pc == 0x806E6CC0u && g_force_audio) {
        // Diagnostic HLE for the sound driver's 0x1005 init command
        // (handler 0x806EFE00): state=5 plus the config fields it fills.
        mem_write8(cpu, 0x808E9866u, 5u);            // +1614 state
        mem_write8(cpu, 0x808E9218u + 1600u, 0u);    // +1600 flags
        mem_write16(cpu, 0x808E9218u + 1620u, 24u);  // +1620 config word
        mem_write32(cpu, 0x808E9218u + 1420u, 0u);   // +1420 pending
        // The real 0x1005 handler also brings up the AI/AID interrupt path:
        // __AIInit registers the AID handler (id 5) and unmasks its source,
        // and the AX library registers the DSP-completion callback. Without
        // this the game's audio thread waits forever for the first AID
        // completion (and the AID source would latch with no handler).
        mem_write32(cpu, 0x80003040u + 5u * 4u, 0x80358330u);  // AID handler
        mem_write32(cpu, 0x800000C4u,
                    mem_read32(cpu, 0x800000C4u) & ~0x04000000u);  // unmask AID
        mem_write32(cpu, cpu->gpr[13] - 22744u, 0x8036A290u);  // AX DMA callback
        mem_write32(cpu, cpu->gpr[13] - 22748u, 0u);           // callback arg
    }
    if (!g_hbm_module && (pc == 0x80016868u || pc == 0x800169A4u)) {
        // GetHbmModule returned a module; force the "not available" result so
        // the caller skips RSO init instead of calling into MEM2 code.
        cpu->gpr[3] = 0u;
    }
    if (pc == 0x80369F94u && g_gx_wait_hle) {
        // Reached just before the wait loop (after the flag was cleared).
        mem_write8(cpu, cpu->gpr[13] - 22280u, 1u);
    }
    if (pc == 0x806E6C54u && g_clear_fontflag) {
        // Diagnostic: release the message/font busy-wait. The loop is
        // `while (flag != 0) sleep(100)` (the flag is "still loading"), so the
        // ready value is zero.
        mem_write32(cpu, cpu->gpr[13] - 25224u, 0u);
    }
    if (pc == 0x80338854u && g_trace_halt && !g_halt_reported) {
        g_halt_reported = true;
        fprintf(stderr, "[halt] entered PPCHalt at block %llu, pc history:", blocks);
        dbg_print_pc_history();
        fprintf(stderr, "\n[halt] r1=0x%08X r3=0x%08X r4=0x%08X lr=0x%08X\n",
                cpu->gpr[1], cpu->gpr[3], cpu->gpr[4], cpu->lr);
    }
    if (g_trace_halt || g_trace_ax) {
        for (unsigned k = 0; k < sizeof(g_trace_pcs) / sizeof(g_trace_pcs[0]); k++) {
            const bool repeat = (pc >= 0x806E62F4u && pc <= 0x806E647Cu) ||
                                pc == 0x806E7120u || pc == 0x806E43D0u ||
                                pc == 0x806E6C54u || pc == 0x806E6CC0u ||
                                pc == 0x806E6D88u || pc == 0x806F871Cu;
            if (pc == g_trace_pcs[k] && (repeat || !g_traced[k])) {
                g_traced[k] = true;
                static unsigned font_hits;
                if (repeat && font_hits++ > 40u) continue;
                fprintf(stderr,
                        "[pc] 0x%08X first hit @block %llu lr=0x%08X "
                        "r3=0x%08X r4=0x%08X r5=0x%08X r6=0x%08X\n",
                        pc, blocks, cpu->lr, cpu->gpr[3], cpu->gpr[4],
                        cpu->gpr[5], cpu->gpr[6]);
            }
        }
        // Watch __OSCurrentThread transitions.
        static u32 cur_thread_prev;
        const u32 cur_thread = mem_read32(cpu, 0x800000E4u);
        if (cur_thread != cur_thread_prev) {
            fprintf(stderr, "[thr] curThread 0x%08X -> 0x%08X at pc=0x%08X lr=0x%08X\n",
                    cur_thread_prev, cur_thread, last_pc, cpu->lr);
            cur_thread_prev = cur_thread;
        }
        // Watch the sound-driver state byte (0x808E9866) transitions.
        static u8 snd_state_prev = 0xFF;
        const u8 snd_state = mem_read8(cpu, 0x808E9866u);
        if (snd_state != snd_state_prev) {
            fprintf(stderr, "[snd] state %u -> %u at pc=0x%08X lr=0x%08X\n",
                    snd_state_prev, snd_state, last_pc, cpu->lr);
            snd_state_prev = snd_state;
        }
        // Watch the AX busy flag ([r13-22568]) changes with the pc.
        static u32 ax_flag_prev;
        const u32 ax_flag = mem_read32(cpu, cpu->gpr[13] - 22568u);
        if (ax_flag != ax_flag_prev) {
            fprintf(stderr, "[ax] flag 0x%08X -> 0x%08X at pc=0x%08X lr=0x%08X\n",
                    ax_flag_prev, ax_flag, last_pc, cpu->lr);
            ax_flag_prev = ax_flag;
        }
        // DVD state machine: log the op code it dispatches on, and every
        // advance-helper entry (which is where the op is re-evaluated).
        static unsigned sm_hits, adv_hits;
        if (pc == 0x8034CC40u) {
            if (sm_hits < 40u)
                fprintf(stderr, "[dvd] SM  op=%u r3=0x%08X block=0x%08X\n",
                        sm_hits++, cpu->gpr[3],
                        mem_read32(cpu, cpu->gpr[13] - 23120u));
        }
        if (pc == 0x8034C920u && adv_hits < 80u) {
            u32 blk = mem_read32(cpu, cpu->gpr[13] - 23120u);
            fprintf(stderr,
                    "[dvd] adv #%u op=0x%X pend=0x%X st2=0x%X blk=0x%08X blkop=0x%X\n",
                    adv_hits++, mem_read32(cpu, cpu->gpr[13] - 23132u),
                    mem_read32(cpu, cpu->gpr[13] - 23244u),
                    mem_read32(cpu, cpu->gpr[13] - 23152u), blk,
                    blk ? mem_read32(cpu, blk + 8u) : 0u);
        }
    }
}

// ---------------------------------------------------------------------------
// Final state report
// ---------------------------------------------------------------------------
void dbg_post_block(CPUState* cpu, u32 pc) {
    if (!g_trace_ref)
        return;
    static u32 prev[4];
    static bool init;
    const u32 block = 0x80AE6A60u;
    u32 cur[4];
    for (int i = 0; i < 4; i++) cur[i] = mem_read32(cpu, block + (u32)i * 4u);
    if (init && (cur[0] != prev[0] || cur[1] != prev[1] ||
                 cur[2] != prev[2] || cur[3] != prev[3])) {
        fprintf(stderr, "[ref] post-block change @pc=0x%08X: %08X %08X %08X %08X lr=0x%08X\n",
                pc, cur[0], cur[1], cur[2], cur[3], cpu->lr);
        fprintf(stderr, "[ref]   pcs:");
        dbg_print_pc_history();
        fprintf(stderr, "\n[ref]   gpr3=0x%08X gpr4=0x%08X gpr5=0x%08X gpr7=0x%08X\n",
                cpu->gpr[3], cpu->gpr[4], cpu->gpr[5], cpu->gpr[7]);
    }
    for (int i = 0; i < 4; i++) prev[i] = cur[i];
    init = true;
}

void dbg_report_final(CPUState* cpu, unsigned long long blocks, const char* stop_reason) {
    printf("[run] stopped: %s\n", stop_reason);
    printf("[run] blocks executed : %llu\n", blocks);
    printf("[run] final pc         : 0x%08X\n", cpu->pc);
    printf("[run] link register    : 0x%08X\n", cpu->lr);
    printf("[run] stack (r1)       : 0x%08X\n", cpu->gpr[1]);
    printf("[run] exception        : %s (0x%08X)\n",
           dbg_exception_name(cpu->exception), cpu->exception);
    fprintf(stderr, "[run] r3=0x%08X r4=0x%08X r5=0x%08X r6=0x%08X r29=0x%08X r30=0x%08X\n",
            cpu->gpr[3], cpu->gpr[4], cpu->gpr[5], cpu->gpr[6],
            cpu->gpr[29], cpu->gpr[30]);
    fprintf(stderr, "[dbg] r13=0x%08X ipcHeapStart=0x%08X ipcHeapEnd=0x%08X\n",
            cpu->gpr[13],
            mem_read32(cpu, cpu->gpr[13] - 27712u),
            mem_read32(cpu, cpu->gpr[13] - 23424u));
    fprintf(stderr, "[dbg] PI cause=0x%08X mask=0x%08X  ipcFlag@0x808F3998=0x%08X\n",
            (u32)interrupt_mmio_read(0xCC003000u, 4),
            (u32)interrupt_mmio_read(0xCC003004u, 4),
            mem_read32(cpu, 0x808F3998u));
    fprintf(stderr, "[dbg] VI DI0/DI1=0x%08X/0x%08X  retraceCounter[r13-22868]=0x%08X\n",
            (u32)interrupt_mmio_read(0xCC002030u, 2),
            (u32)interrupt_mmio_read(0xCC002038u, 2),
            cpu->gpr[13] ? mem_read32(cpu, cpu->gpr[13] - 22868u) : 0u);
    fprintf(stderr, "[dbg] __OSInterruptMask@0x800000C4=0x%08X  @0x800000C8=0x%08X\n",
            mem_read32(cpu, 0x800000C4u), mem_read32(cpu, 0x800000C8u));
    {
        u64 skips = 0, skipped = 0;
        interrupt_idle_stats(&skips, &skipped);
        if (skips != 0ull)
            printf("[dbg] idle: skips=%llu guest_ticks=%llu (%.2f s emulated)\n",
                   (unsigned long long)skips,
                   (unsigned long long)skipped,
                   (double)skipped / 40500000.0);
    }
    {
        u64 retraces = 0, ext = 0, dec = 0;
        interrupt_stats(&retraces, &ext, &dec);
        printf("[dbg] irq: retraces=%llu external=%llu decrementer=%llu\n",
               (unsigned long long)retraces, (unsigned long long)ext,
               (unsigned long long)dec);
    }
    {
        GxStats gx;
        gx_get_stats(&gx);
        fprintf(stderr,
                "[dbg] gx: fifo_writes=%u draws=%u state=%u resources=%u "
                "trace=%u flush_fail=%u pending=%u resync=%u skip=%u\n",
                gx.fifo_writes, gx.draws, gx.state_packets, gx.resource_packets,
                gx.trace_events, gx.flush_failures, gx.pending_bytes, gx.resyncs,
            gx.skip_resyncs);
    }
    {
        u64 di_bytes = 0;
        u32 di_reads = 0;
        u32 di_mmio_r = 0, di_mmio_w = 0;
        ios_di_stats(&di_bytes, &di_reads);
        dvd_mmio_stats(&di_mmio_r, &di_mmio_w);
        {
            GxStats gsx;
            gx_get_stats(&gsx);
            fprintf(stderr,
                    "[dbg] wpad: hits=%u\n", g_wpad_hits);
        { u32 any = 0; for (u32 i = 0; i < 12; i++) if (g_wpad_pc[i]) any = 1;
          if (any) { fprintf(stderr, "[dbg] wpadpc:"); for (u32 i = 0; i < 12; i++) fprintf(stderr, " %u:%u", i, g_wpad_pc[i]); fprintf(stderr, "\n"); } }

            fprintf(stderr,
                    "[dbg] cri: post=%u enqueue=%u draws=%u (post/frame=%.1f)\n",
                    g_cri_post_hits, g_cri_enqueue_hits, gsx.draws,
                    gsx.draws != 0u
                        ? (double)g_cri_post_hits / (double)(gsx.draws / 70)
                        : 0.0);
        }
        for (u32 i = 0; i < g_pc_hist_used; i++)
            fprintf(stderr, "[dbg] main_pc 0x%08X x%u\n", g_pc_hist_pc[i],
                    g_pc_hist_n[i]);
        fprintf(stderr,
                "[dbg] timeline: last_draw_blk=%llu last_ax_dma_blk=%llu "
                "ai_hits=%u ax_hits=%u\n",
                g_last_draw_blk, g_last_ax_blk, g_ai_handler_hits,
                g_ax_dma_hits);
        fprintf(stderr,
                "[dbg] disc: ios_reads=%u bytes=%llu (%.1f MiB) "
                "di_mmio=%uR/%uW\n",
                di_reads, (unsigned long long)di_bytes,
                (double)di_bytes / (1024.0 * 1024.0), di_mmio_r, di_mmio_w);
    }
    fprintf(stderr, "[dbg] audio: AI handler hits=%u AX DMA callback hits=%u\n",
            g_ai_handler_hits, g_ax_dma_hits);
    fprintf(stderr, "[dbg] gx: wakes=%u last_qhead=0x%08X last_main_state=%u "
            "gxwait_zero=%u\n", g_gxwake_hits, g_gxwake_qhead, g_gxwake_state,
            g_gxwait_zero);
    fprintf(stderr, "[dbg] main-thread pcs:");
    for (unsigned k = 0; k < 12u; k++)
        fprintf(stderr, " 0x%08X", g_main_pc_ring[(g_main_pc_i + k) % 12u]);
    fprintf(stderr, "\n");
    {
        u32 thread = mem_read32(cpu, 0x800000E4u);
        fprintf(stderr,
                "[dbg] curThread=0x%08X intDisable=%u thrState@+712=%u prio@+720=%u\n",
                thread,
                mem_read32(cpu, cpu->gpr[13] - 23440u),
                thread ? mem_read16(cpu, thread + 712u) : 0u,
                thread ? mem_read32(cpu, thread + 720u) : 0u);
        // Walk the active-thread list via OSThread.link (+736 next, +740 prev)
        // and report each thread's scheduling fields.
        u32 t = thread;
        for (int i = 0; i < 12 && t; i++) {
            fprintf(stderr,
                    "[dbg]   thread[%d] 0x%08X state=%u attr=%u susp=%d prio=%d base=%d "
                    "val=%08X queue=%08X next=%08X prev=%08X\n",
                    i, t,
                    mem_read16(cpu, t + 712u), mem_read16(cpu, t + 714u),
                    (int)mem_read32(cpu, t + 716u), (int)mem_read32(cpu, t + 720u),
                    (int)mem_read32(cpu, t + 724u),
                    mem_read32(cpu, t + 728u), mem_read32(cpu, t + 732u),
                    mem_read32(cpu, t + 736u), mem_read32(cpu, t + 740u));
            u32 nxt = mem_read32(cpu, t + 736u);
            if (nxt == thread || nxt == t) break;
            t = nxt;
        }
    }
    fprintf(stderr, "[dbg] ipcCtx@0x81600020: %08X %08X %08X %08X | cb+32=%08X arg+36=%08X\n",
            mem_read32(cpu, 0x81600020u), mem_read32(cpu, 0x81600024u),
            mem_read32(cpu, 0x81600028u), mem_read32(cpu, 0x8160002Cu),
            mem_read32(cpu, 0x81600040u), mem_read32(cpu, 0x81600044u));
    fprintf(stderr, "[dbg] buf@0x807913D0: %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X\n",
            mem_read32(cpu, 0x807913D0u), mem_read32(cpu, 0x807913D4u),
            mem_read32(cpu, 0x807913D8u), mem_read32(cpu, 0x807913DCu),
            mem_read32(cpu, 0x807913E0u), mem_read32(cpu, 0x807913E4u),
            mem_read32(cpu, 0x807913E8u), mem_read32(cpu, 0x807913ECu),
            mem_read32(cpu, 0x807913F0u), mem_read32(cpu, 0x807913F4u));
    if (getenv("COLORS_DBG_FONT") != NULL) {
        fprintf(stderr, "[font] flag@r13-25224=0x%08X blk@0x808E5480:",
                mem_read32(cpu, cpu->gpr[13] - 25224u));
        for (u32 i = 0; i < 0x50u; i++)
            fprintf(stderr, " %02X", (unsigned)mem_read8(cpu, 0x808E5480u + i));
        {
            u32 qh = mem_read32(cpu, 0x808E8D70u);
            fprintf(stderr, "[sndq] head=0x%08X", qh);
            for (int i = 0; i < 4 && qh; i++) {
                fprintf(stderr, " [n%08X next=%08X cd=%d type=%u fl=%u]",
                        qh, mem_read32(cpu, qh),
                        (int)mem_read32(cpu, qh + 12u),
                        mem_read16(cpu, qh + 20u),
                        mem_read16(cpu, qh + 22u));
                qh = mem_read32(cpu, qh);
            }
            fprintf(stderr, "\n");
        }
        fprintf(stderr, "[font] dmapool@0x807FE730:");
        for (u32 i = 0; i < 8u; i++)
            fprintf(stderr, " [%u]=%08X", i,
                    mem_read32(cpu, 0x807FE730u + i * 16u));
        fprintf(stderr, "\n[font] slot=-25064: 0x%08X\n",
                mem_read32(cpu, cpu->gpr[13] - 25064u));
        fprintf(stderr, "[font] ioheap: base=0x%08X cur=0x%08X end=0x%08X pend=0x%08X\n",
                mem_read32(cpu, cpu->gpr[13] - 21988u),
                mem_read32(cpu, cpu->gpr[13] - 21984u),
                mem_read32(cpu, cpu->gpr[13] - 21980u),
                mem_read32(cpu, cpu->gpr[13] - 21976u));
        fprintf(stderr, "\n[font] r13-25220=0x%08X r13-25216=0x%08X r13-25212=0x%08X r13-25208=0x%08X\n",
                mem_read32(cpu, cpu->gpr[13] - 25220u),
                mem_read32(cpu, cpu->gpr[13] - 25216u),
                mem_read32(cpu, cpu->gpr[13] - 25212u),
                mem_read32(cpu, cpu->gpr[13] - 25208u));
    }
    {
        u32 q = mem_read32(cpu, 0x808F3910u);
        fprintf(stderr, "[dbg] sleepQ head=0x%08X timebase=0x%llX", q,
                (unsigned long long)cpu->timebase);
        for (int i = 0; i < 6 && q; i++) {
            fprintf(stderr, " [%08X n=%08X w=%08X%08X]", q,
                    mem_read32(cpu, q + 20u), mem_read32(cpu, q + 8u),
                    mem_read32(cpu, q + 12u));
            q = mem_read32(cpu, q + 20u);
        }
        fprintf(stderr, "\n");
    }
    {
        u32 tbl = mem_read32(cpu, 0x808F3968u);
        fprintf(stderr, "[dbg] handlerTbl=0x%08X osMask=0x%08X\n",
                tbl,
                mem_read32(cpu, 0x800000C4u) | mem_read32(cpu, 0x800000C8u));
        if (tbl) {
            fprintf(stderr, "[dbg] irq handlers:");
            for (u32 i = 0; i < 32u; i++) {
                u32 h = mem_read32(cpu, tbl + i * 4u);
                if (h) fprintf(stderr, " [%u]=0x%08X", i, h);
            }
            fprintf(stderr, "\n");
        }
    }
    // Scan for OSThread structures (state at +712, priority at +720) so we can
    // see what the game's threads are blocked on when the CPU goes idle.
    {
        const u32 lo = 0x807C6500u, hi = 0x8091DC20u;
        int found = 0;
        for (u32 a = lo; a + 0x300u < hi && found < 16; a += 4u) {
            u16 state = mem_read16(cpu, a + 712u);
            u16 attr  = mem_read16(cpu, a + 714u);
            s32 susp  = (s32)mem_read32(cpu, a + 716u);
            s32 prio  = (s32)mem_read32(cpu, a + 720u);
            if ((state == 1u || state == 2u || state == 4u || state == 8u) &&
                (attr & 0xFFFEu) == 0u && susp >= 0 && susp < 8 &&
                prio >= 0 && prio < 32) {
                fprintf(stderr,
                        "[dbg] OSThread? 0x%08X state=%u attr=%u susp=%d prio=%d "
                        "base=%d queue=0x%08X next=0x%08X prev=0x%08X stackEnd=0x%08X "
                        "pc=0x%08X lr=0x%08X r1=0x%08X r3=0x%08X\n",
                        a, state, attr, susp, prio,
                        (int)mem_read32(cpu, a + 724u),
                        mem_read32(cpu, a + 732u),
                        mem_read32(cpu, a + 736u), mem_read32(cpu, a + 740u),
                        mem_read32(cpu, a + 0x2C4u),
                        mem_read32(cpu, a + CTX_SRR0), mem_read32(cpu, a + CTX_LR),
                        mem_read32(cpu, a + 4u), mem_read32(cpu, a + 12u));
                if (state == 4u || susp == 1) {
                    u32 sp = mem_read32(cpu, a + 4u);
                    if (sp >= GC_RAM_BASE && sp < 0x94000000u)
                        dbg_dump_backchain_at(cpu, sp, a);
                }
                found++;
            }
        }
        fprintf(stderr, "[dbg] thread scan: %d candidate(s)\n", found);
    }
    fprintf(stderr, "[dbg] DVD context array @0x807E0860:\n");
    for (int i = 0; i < 4; i++) {
        fprintf(stderr, "  ctx[%d] @0x%08X: %08X %08X %08X %08X\n", i,
                0x807E0860u + (u32)i * 0x20,
                mem_read32(cpu, 0x807E0860u + (u32)i * 0x20 + 0),
                mem_read32(cpu, 0x807E0860u + (u32)i * 0x20 + 4),
                mem_read32(cpu, 0x807E0860u + (u32)i * 0x20 + 8),
                mem_read32(cpu, 0x807E0860u + (u32)i * 0x20 + 12));
    }
    if (g_media_debug) {
        static const char* names[10] = {"pump", "status", "playing", "cmd_fsm",
                                       "cri_task", "mgr_gate", "sfd_reset",
                                       "sfd_start", "mwply_create",
                                       "build_reset"};
        fprintf(stderr, "[dbg] media calls:");
        for (u32 i = 0; i < 10u; i++)
            fprintf(stderr, " %s=%u", names[i], g_media_seen[i]);
        fprintf(stderr, "\n");
    }
    fprintf(stderr, "[run] recent block entry PCs (oldest first):");
    dbg_print_pc_history();
    fprintf(stderr, "\n");
    dbg_dump_backchain(cpu);
}
