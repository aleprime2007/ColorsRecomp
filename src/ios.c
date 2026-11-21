#include "ios.h"

#include "disc.h"
#include "interrupt.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static u32 g_ipc_regs[IPC_SIZE / 4];
static bool g_ipc_pending;

// COLORS_TRACE_IO=1 logs every IOS request (type, fd, buffers, path).
static int g_io_trace;
static int g_trace_files;
static int g_save_store;
static int g_save_debug;
static u64 g_di_bytes;
static u32 g_di_reads;

void ios_init(void) {
    g_io_trace = getenv("COLORS_TRACE_IO") != NULL;
    g_trace_files = getenv("COLORS_TRACE_FILES") != NULL;
    // Writable guest paths (/tmp/*.sav) are backed by host memory. Without
    // this the game retries its save-file opens every frame and never reaches
    // the movie player.
    g_save_store = getenv("COLORS_NO_SAVE_STORE") == NULL;
    g_save_debug = getenv("COLORS_SAVE_DEBUG") != NULL;
}
// Status bits owned by the Starlet (ARM) side. The handshake the guest's IPC
// interrupt handler implements is:
//   0x14 (0x10|0x04) set -> a reply is ready; process it (read IPC_BUFFER).
//   0x22 (0x20|0x02) set -> reply consumed; PPC acks and sends the next queued
//                           request.
// The PPC preserves the 0x30 mask when writing the status register.
static u32 g_ipc_arm_bits;

// ---------------------------------------------------------------------------
// Minimal IOS device service: file descriptors and per-device dispatch.
// ---------------------------------------------------------------------------
#define IOS_MAX_FDS 16
#define IOS_FD_BASE 0x100u

typedef struct {
    bool used;
    char path[64];
    // In-memory backing for writable guest paths (the game's save files live
    // under /tmp/ on this title and are retried every frame when the open
    // fails).
    u8* data;
    u32 size;
    u32 cap;
    u32 pos;
    bool is_store;
    bool is_nand;     // backed by a real file under the NAND root
    FILE* host;
} IosFd;

static IosFd g_ios_fds[IOS_MAX_FDS];

// 32-byte disc boot header ("SNCE8P..."), read once at startup.
static u8 g_disc_id[32];

static int ios_open(const char* path) {
    for (int i = 0; i < IOS_MAX_FDS; i++) {
        if (!g_ios_fds[i].used) {
            memset(&g_ios_fds[i], 0, sizeof(g_ios_fds[i]));
            g_ios_fds[i].used = true;
            g_ios_fds[i].pos = 0;
            snprintf(g_ios_fds[i].path, sizeof(g_ios_fds[i].path), "%s", path);
            return (int)(IOS_FD_BASE + (u32)i);
        }
    }
    return -1;
}

// Find an existing backing store or create an empty one (a new save).
static IosFd* ios_store_open(const char* path) {
    for (int i = 0; i < IOS_MAX_FDS; i++) {
        IosFd* f = &g_ios_fds[i];
        if (f->used && f->is_store && strcmp(f->path, path) == 0) {
            f->pos = 0;
            if (g_save_debug) {
                static u32 rn = 0;
                if (rn++ < 100000u)
                    fprintf(stderr, "[savreuse] %s slot=%d size=%u\n", path, i,
                            f->size);
            }
            return f;
        }
    }
    for (int i = 0; i < IOS_MAX_FDS; i++) {
        IosFd* f = &g_ios_fds[i];
        if (!f->used) {
            memset(f, 0, sizeof(*f));
            f->used = true;
            f->is_store = true;
            if (g_save_debug) {
                static u32 nn = 0;
                if (nn++ < 100000u)
                    fprintf(stderr, "[savnew] %s slot=%d\n", path, i);
            }
            snprintf(f->path, sizeof(f->path), "%s", path);
            return f;
        }
    }
    return NULL;
}

// Host directory backing the emulated NAND (COLORS_NAND, default "nand").
static const char* nand_root(void) {
    const char* env = getenv("COLORS_NAND");
    return (env != NULL && env[0] != '\0') ? env : "nand";
}

// Open a guest NAND path ("/shared2/sys/SYSCONF", "/title/.../game_0.sav") on
// the host. Writable first so the game can update its saves, read-only when the
// host file is not writable.
static IosFd* ios_nand_open(const char* path) {
    // Default scope: the title's own save directory. Reading the system-menu
    // files (/shared2/sys/SYSCONF, /title/00000001/...) makes the SDK run a
    // bignum signature verification that dominates emulated time in this
    // interpreter, so those are opt-in via COLORS_NAND_ALL=1.
    static int all = -1;
    if (all < 0)
        all = getenv("COLORS_NAND_ALL") != NULL ? 1 : 0;
    if (!all && strncmp(path, "/title/00010000/", 16) != 0)
        return NULL;
    char full[512];
    snprintf(full, sizeof(full), "%s%s", nand_root(), path);
    FILE* h = fopen(full, "r+b");
    if (h == NULL)
        h = fopen(full, "rb");
    if (h == NULL)
        return NULL;
    for (int i = 0; i < IOS_MAX_FDS; i++) {
        IosFd* f = &g_ios_fds[i];
        if (!f->used) {
            memset(f, 0, sizeof(*f));
            f->used = true;
            f->is_nand = true;
            f->host = h;
            snprintf(f->path, sizeof(f->path), "%s", path);
            if (g_trace_files || g_save_debug)
                fprintf(stderr, "[ios] nand open \"%s\" -> host ok\n", path);
            return f;
        }
    }
    fclose(h);
    return NULL;
}

static IosFd* ios_lookup(u32 fd) {
    if (fd < IOS_FD_BASE || fd >= IOS_FD_BASE + IOS_MAX_FDS)
        return NULL;
    IosFd* f = &g_ios_fds[fd - IOS_FD_BASE];
    return f->used ? f : NULL;
}

static bool ios_path_is(const IosFd* f, const char* prefix) {
    return f != NULL && strncmp(f->path, prefix, strlen(prefix)) == 0;
}

static void ios_read_string(CPUState* cpu, u32 phys, char* out, size_t cap) {
    u32 p = phys + GC_RAM_BASE;
    size_t i = 0;
    for (; i + 1u < cap; i++) {
        char c = (char)(mem_read32(cpu, p + (u32)i) >> 24);
        out[i] = c;
        if (c == '\0')
            break;
    }
    out[i] = '\0';
}

// ---------------------------------------------------------------------------
// Delayed IOS completions.
//
// USB interrupt/control transfers complete once per USB frame (1 ms), not
// instantly. Replying synchronously makes the game's USB driver poll in a tight
// loop and burn all emulated time; deferring the reply models the hardware and
// lets the OS scheduler run other threads in between.
// ---------------------------------------------------------------------------
#define IOS_MAX_DELAYED 8

typedef struct {
    bool active;
    u32 ctx;
    u32 due;
    u32 result;
    u32 type;
} IosDelayed;

static IosDelayed g_ios_delayed[IOS_MAX_DELAYED];
static u32 g_usb_frame_ticks = 40500u;  // 1 ms at 40.5 MHz

static void ios_write_reply(CPUState* cpu, u32 ctx, u32 result, u32 type) {
    mem_write32(cpu, ctx + 4u, result);
    mem_write32(cpu, ctx + 8u, type);
}

// Queue a reply to be delivered after one USB frame. Returns false when the
// queue is full (caller then replies immediately).
static bool ios_defer_reply(CPUState* cpu, u32 ctx, u32 result, u32 type) {
    for (int i = 0; i < IOS_MAX_DELAYED; i++) {
        if (!g_ios_delayed[i].active) {
            g_ios_delayed[i].active = true;
            g_ios_delayed[i].ctx = ctx;
            g_ios_delayed[i].due = (u32)cpu->timebase + g_usb_frame_ticks;
            g_ios_delayed[i].result = result;
            g_ios_delayed[i].type = type;
            return true;
        }
    }
    return false;
}

// Handle one IOS request by filling the reply fields of the request context:
//   +4 = result (s32), +8 = reply type, buffers written in place.
static bool ios_dispatch(CPUState* cpu, u32 ctx) {
    u32 type = mem_read32(cpu, ctx + 0u);
    u32 result = 0;
    bool defer = false;

    if (g_io_trace) {
        char path[64] = "";
        if (type == 1u)
            ios_read_string(cpu, mem_read32(cpu, ctx + 12u), path, sizeof path);
        fprintf(stderr, "[io] tb=0x%llX type=%u fd=%u arg=0x%08X in=0x%08X/%u "
                "out=0x%08X/%u%s%s\n",
                (unsigned long long)cpu->timebase, type,
                mem_read32(cpu, ctx + 8u), mem_read32(cpu, ctx + 12u),
                mem_read32(cpu, ctx + 16u), mem_read32(cpu, ctx + 20u),
                mem_read32(cpu, ctx + 24u), mem_read32(cpu, ctx + 28u),
                path[0] ? " \"" : "", path[0] ? path : "");
    }

    if (type == 1u) {  // IOS_Open(path)
        char path[64];
        ios_read_string(cpu, mem_read32(cpu, ctx + 12u), path, sizeof path);
        int fd;
        if (g_save_store && strncmp(path, "/tmp/", 5) == 0) {
            // Writable guest path: back it with host memory so saves survive
            // (and the per-frame retry loop can finish).
            IosFd* sf = ios_store_open(path);
            fd = sf != NULL ? (int)(IOS_FD_BASE + (u32)(sf - g_ios_fds)) : -1;
            result = (fd < 0) ? 0xFFFFFFF4u : (u32)fd;
            if (g_trace_files)
                fprintf(stderr, "[ios] store open \"%s\" -> fd 0x%X size=%u\n",
                        path, result, sf != NULL ? sf->size : 0u);
            fd = -2;  // sentinel: handled
        } else if (path[0] == '/' && strncmp(path, "/dev/", 5) != 0) {
            // NAND file service: back the guest path with the host NAND tree.
            // Anything absent there still reports ENOENT so the game's parsers
            // do not see zero-filled structures.
            IosFd* nf = ios_nand_open(path);
            if (nf != NULL) {
                fd = (int)(IOS_FD_BASE + (u32)(nf - g_ios_fds));
                result = (u32)fd;
            } else {
                fd = -1;
                result = 0xFFFFFFFAu;  // -6 (ENOENT)
            }
        } else if (strncmp(path, "/dev/usb", 8) == 0) {
            // The USB host controller exists but no device is attached, so
            // device nodes (/dev/usb/ohN/vvv/ppp) do not open. Allowing them
            // made the game's USB driver poll a phantom device forever.
            if (strncmp(path, "/dev/usb/", 9) == 0) {
                fd = -1;
                result = 0xFFFFFFFAu;  // -6 (ENOENT): no such device
            } else {
                fd = ios_open(path);
                result = (fd < 0) ? 0xFFFFFFF4u : (u32)fd;
            }
        } else {
            fd = ios_open(path);
            result = (fd < 0) ? 0xFFFFFFF4u : (u32)fd;  // -12 if out of handles
        }
        if (g_mmio_log || g_trace_files)
            fprintf(stderr, "[ios] open \"%s\" -> fd 0x%X\n", path, result);
    } else {
        u32 fd = mem_read32(cpu, ctx + 8u);
        IosFd* f = ios_lookup(fd);
        if (f != NULL && f->is_store && g_save_debug) {
            static u32 n = 0;
            if (n++ < 30u)
                fprintf(stderr,
                        "[savfd] type=%u fd=0x%X a12=0x%08X a16=0x%08X "
                        "a20=0x%08X a24=0x%08X a28=0x%08X\n",
                        type, fd, mem_read32(cpu, ctx + 12u),
                        mem_read32(cpu, ctx + 16u), mem_read32(cpu, ctx + 20u),
                        mem_read32(cpu, ctx + 24u), mem_read32(cpu, ctx + 28u));
        }
        if (type == 5u) {  // IOS_Seek(fd, offset, whence)
            const u32 inp = mem_read32(cpu, ctx + 16u) + GC_RAM_BASE;
            const s32 off = (s32)mem_read32(cpu, inp + 0u);
            const u32 whence = mem_read32(cpu, inp + 4u);
            const int wh = whence == 0u ? SEEK_SET
                          : whence == 1u ? SEEK_CUR
                                         : SEEK_END;
            if (f != NULL && f->is_nand && f->host != NULL) {
                fseek(f->host, off, wh);
                result = (u32)ftell(f->host);
            } else if (f != NULL && f->is_store) {
                s32 np = (s32)f->pos;
                if (wh == SEEK_SET) np = off;
                else if (wh == SEEK_CUR) np += off;
                else np = (s32)f->size + off;
                if (np < 0) np = 0;
                f->pos = (u32)np;
                result = (u32)np;
            } else {
                result = 0;
            }
        } else if (type == 2u) {  // IOS_Close
            if (f != NULL) {
                if (g_mmio_log || g_trace_files)
                    fprintf(stderr, "[ios] close \"%s\"\n", f->path);
                // Keep the backing data (a closed file is still on "disc");
                // only release the handle.
                if (f->is_nand && f->host != NULL) {
                    fclose(f->host);
                    f->host = NULL;
                }
                if (!f->is_store)
                    f->used = false;
            }
        } else if (type == 6u) {  // IOS_Ioctl(fd, cmd, in, inlen, out, outlen)
            u32 cmd = mem_read32(cpu, ctx + 12u);
            u32 in_phys = mem_read32(cpu, ctx + 16u);
            u32 in_len = mem_read32(cpu, ctx + 20u);
            u32 out_phys = mem_read32(cpu, ctx + 24u);
            u32 out_len = mem_read32(cpu, ctx + 28u);
            if (g_mmio_log && f != NULL && in_len != 0u && in_len <= 0x40u) {
                u32 p = in_phys + GC_RAM_BASE;
                fprintf(stderr, "[ios] %s ioctl 0x%X in[%u]=", f->path, cmd, in_len);
                for (u32 i = 0; i < in_len && i < 32u; i++)
                    fprintf(stderr, "%02X", (unsigned)(mem_read32(cpu, p + i) >> 24));
                fprintf(stderr, "\n");
            }
            if (ios_path_is(f, "/dev/usb")) {
                result = 0;  // silent device: control transfer accepted
                defer = true;
            } else if (ios_path_is(f, "/dev/di")) {
                if (cmd == 0x86u) {
                    // DI ReadDiskID: the Starlet DMAs the 32-byte boot header
                    // back into the caller's command block (out vector is
                    // null for this command).
                    u32 dst_phys = (out_phys != 0u && out_len >= 32u) ? out_phys : in_phys;
                    if (dst_phys != 0u) {
                        u32 p = dst_phys + GC_RAM_BASE;
                        for (u32 i = 0; i < 32u; i += 4u) {
                            u32 w = ((u32)g_disc_id[i] << 24) |
                                    ((u32)g_disc_id[i + 1u] << 16) |
                                    ((u32)g_disc_id[i + 2u] << 8) |
                                    (u32)g_disc_id[i + 3u];
                            mem_write32(cpu, p + i, w);
                        }
                    }
                } else if (cmd == 0x71u || cmd == 0x70u) {
                    // DI read: in = {u8 cmd; u32 offset; u32 length}, where the
                    // offset field is kept in the FST's native unit (disc byte
                    // offset >> 2), exactly as the SDK's file layer stores it.
                    // Decoded live: 0x2E55F4A0 -> 0xB957D280 = start of
                    // sonic2010_0.cpk, 0x2E55ED68 -> 0xB957B5A0 = sonic2010.sel.
                    u32 ip = in_phys + GC_RAM_BASE;
                    u32 length = mem_read32(cpu, ip + 4u);
                    u32 offset = mem_read32(cpu, ip + 8u) << 2;
                    u32 dst_phys = (out_phys != 0u && out_len != 0u) ? out_phys : in_phys;
                    if (g_io_trace)
                        fprintf(stderr,
                                "[di] cmd=0x%X in=%08X %08X %08X %08X len=0x%X off=0x%X "
                                "dst=0x%08X outlen=%u\n",
                                cmd, mem_read32(cpu, ip + 0u), mem_read32(cpu, ip + 4u),
                                mem_read32(cpu, ip + 8u), mem_read32(cpu, ip + 12u),
                                length, offset, dst_phys, out_len);
                    if (length == 0u || length > 0x00400000u)
                        length = out_len;
                    if (length > 0u && length <= 0x00400000u) {
                        g_di_bytes += length;
                        g_di_reads++;
                        if (g_trace_files) {
                            const u32 retrace =
                                mem_read32(cpu, cpu->gpr[13] - 22868u);
                            u8 probe[8] = {0};
                            const bool any2 = disc_read(offset, 8u, probe);
                            fprintf(stderr,
                                    "[di] #%u retrace=%u off=0x%08X len=0x%X "
                                    "-> %s first=%02X %02X %02X %02X %02X %02X "
                                    "%02X %02X\n",
                                    g_di_reads, retrace, offset, length,
                                    disc_region_name(offset)
                                        ? disc_region_name(offset)
                                        : "unmapped",
                                    any2 ? probe[0] : 0, any2 ? probe[1] : 0,
                                    any2 ? probe[2] : 0, any2 ? probe[3] : 0,
                                    any2 ? probe[4] : 0, any2 ? probe[5] : 0,
                                    any2 ? probe[6] : 0, any2 ? probe[7] : 0);
                        }
                        u8* buf = (u8*)malloc(length);
                        if (buf != NULL) {
                            const bool any = disc_read(offset, length, buf);
                            if (!any) {
                                // A read outside every mapped FST region is
                                // filled with zeros; a loader waiting on real
                                // data then stalls with nothing to show.
                                static u32 warn_n;
                                if (warn_n < 8u) {
                                    ++warn_n;
                                    fprintf(stderr,
                                            "[ios] WARNING: DI read off=0x%X "
                                            "len=0x%X is UNMAPPED (filled with "
                                            "zeros)\n",
                                            offset, length);
                                }
                            }
                            u32 dp = dst_phys + GC_RAM_BASE;
                            for (u32 i = 0; i < length; i++)
                                mem_write8(cpu, dp + i, buf[i]);
                            free(buf);
                            if (g_mmio_log)
                                fprintf(stderr,
                                        "[ios] di read off=0x%08X len=0x%X -> %s\n",
                                        offset, length,
                                        any ? (disc_region_name(offset) ? disc_region_name(offset) : "mapped")
                                            : "UNMAPPED (zeros)");
                        }
                    }
                } else if (g_mmio_log || g_trace_files) {
                    fprintf(stderr, "[ios] di ioctl 0x%X (in %u/%u out 0x%08X/%u)\n",
                            cmd, in_len, mem_read32(cpu, in_phys + GC_RAM_BASE + 4u),
                            out_phys, out_len);
                }
                // The SDK's DVD state machine treats an odd DI result (bit 0
                // set) as "command completed"; 0 sends it down the fatal
                // ES/retry path.
                result = 1;
            }
        } else if (type == 4u) {  // IOS_Write(fd, buf, len) [SDK layout]
            const u32 buf = mem_read32(cpu, ctx + 12u);
            const u32 len = mem_read32(cpu, ctx + 16u);
            if (g_save_debug) {
                static u32 wn = 0;
                if (wn++ < 10u)
                    fprintf(stderr,
                            "[savwr] fd=0x%X store=%d buf=0x%08X len=%u pos=%u "
                            "size=%u\n",
                            fd, f != NULL ? (int)f->is_store : -1, buf, len,
                            f != NULL ? f->pos : 0u, f != NULL ? f->size : 0u);
            }
            if (f != NULL && f->is_store) {
                const u32 need = f->pos + len;
                if (need > f->cap) {
                    u32 cap = f->cap != 0u ? f->cap : 65536u;
                    while (cap < need)
                        cap *= 2u;
                    u8* nd = (u8*)realloc(f->data, cap);
                    if (nd != NULL) {
                        memset(nd + f->cap, 0, cap - f->cap);
                        f->data = nd;
                        f->cap = cap;
                    }
                }
                if (f->data != NULL && need <= f->cap) {
                    for (u32 i = 0; i < len; i++)
                        f->data[f->pos + i] =
                            (u8)mem_read8(cpu, buf + GC_RAM_BASE + i);
                    f->pos = need;
                    if (need > f->size)
                        f->size = need;
                    result = len;
                    if (g_save_debug) {
                        static u32 pn = 0;
                        if (pn++ < 6u)
                            fprintf(stderr,
                                    "[savwrok] fd=0x%X pos=%u size=%u\n", fd,
                                    f->pos, f->size);
                    }
                } else {
                    result = 0;
                }
            } else if (f != NULL && f->is_nand && f->host != NULL) {
                u8* tmp = (u8*)malloc(len != 0u ? len : 1u);
                if (tmp == NULL) {
                    result = 0;
                } else {
                    for (u32 i = 0; i < len; i++)
                        tmp[i] = mem_read8(cpu, buf + GC_RAM_BASE + i);
                    const size_t put = fwrite(tmp, 1, len, f->host);
                    fflush(f->host);
                    free(tmp);
                    result = (u32)put;
                }
            } else {
                result = len;
            }
            if (ios_path_is(f, "/dev/usb"))
                defer = true;
        } else if (type == 3u) {  // IOS_Read(fd, buf, len)
            u32 buf = mem_read32(cpu, ctx + 16u);
            u32 len = mem_read32(cpu, ctx + 20u);
            if (f != NULL && (f->is_store || f->is_nand)) {
                buf = mem_read32(cpu, ctx + 12u);
                len = mem_read32(cpu, ctx + 16u);
            }
            if (f != NULL && f->is_nand && g_trace_files) {
                static u32 rn = 0;
                if (rn++ < 6u)
                    fprintf(stderr,
                            "[ios] nand read ctx: +12=%08X +16=%08X +20=%08X "
                            "+24=%08X +28=%08X\n",
                            mem_read32(cpu, ctx + 12u), mem_read32(cpu, ctx + 16u),
                            mem_read32(cpu, ctx + 20u), mem_read32(cpu, ctx + 24u),
                            mem_read32(cpu, ctx + 28u));
            }
            if (g_trace_files)
                fprintf(stderr, "[ios] read fd=0x%X buf=0x%08X len=%u pos=%u size=%u\n",
                        fd, buf, len, f != NULL ? f->pos : 0u,
                        f != NULL ? f->size : 0u);
            if (f != NULL && f->is_nand && f->host != NULL) {
                u32 n = len;
                u8* tmp = (u8*)malloc(n != 0u ? n : 1u);
                if (tmp == NULL) {
                    result = 0;
                } else {
                    const size_t got = fread(tmp, 1, n, f->host);
                    for (size_t i = 0; i < got; i++)
                        mem_write8(cpu, buf + GC_RAM_BASE + (u32)i, tmp[i]);
                    free(tmp);
                    result = (u32)got;
                }
            } else if (f != NULL && f->is_store) {
                u32 n = len;
                if (f->pos >= f->size)
                    n = 0;
                else if (n > f->size - f->pos)
                    n = f->size - f->pos;
                for (u32 i = 0; i < n; i++)
                    mem_write8(cpu, buf + GC_RAM_BASE + i, f->data[f->pos + i]);
                f->pos += n;
                result = n;
            } else {
                result = mem_read32(cpu, ctx + 16u);  // legacy device result
            }
            if (ios_path_is(f, "/dev/usb"))
                defer = true;
        } else if (type == 7u) {  // IOS_Write(fd, buf, len)
            u32 buf = mem_read32(cpu, ctx + 16u);
            u32 len = mem_read32(cpu, ctx + 20u);
            if (f != NULL && (f->is_store || f->is_nand)) {
                buf = mem_read32(cpu, ctx + 12u);
                len = mem_read32(cpu, ctx + 16u);
            }
            if (g_trace_files)
                fprintf(stderr, "[ios] write fd=0x%X buf=0x%08X len=%u pos=%u\n",
                        fd, buf, len, f != NULL ? f->pos : 0u);
            if (f != NULL && f->is_store) {
                const u32 need = f->pos + len;
                if (need > f->cap) {
                    u32 cap = f->cap != 0u ? f->cap : 4096u;
                    while (cap < need)
                        cap *= 2u;
                    u8* nd = (u8*)realloc(f->data, cap);
                    if (nd != NULL) {
                        memset(nd + f->cap, 0, cap - f->cap);
                        f->data = nd;
                        f->cap = cap;
                    }
                }
                if (f->data != NULL && need <= f->cap) {
                    for (u32 i = 0; i < len; i++)
                        f->data[f->pos + i] = (u8)mem_read8(cpu, buf + GC_RAM_BASE + i);
                    f->pos = need;
                    if (need > f->size)
                        f->size = need;
                    result = len;
                } else {
                    result = 0;
                }
            } else if (f != NULL && f->is_nand && f->host != NULL) {
                u8* tmp = (u8*)malloc(len != 0u ? len : 1u);
                if (tmp == NULL) {
                    result = 0;
                } else {
                    for (u32 i = 0; i < len; i++)
                        tmp[i] = mem_read8(cpu, buf + GC_RAM_BASE + i);
                    const size_t put = fwrite(tmp, 1, len, f->host);
                    fflush(f->host);
                    free(tmp);
                    result = (u32)put;
                }
            } else {
                result = mem_read32(cpu, ctx + 28u);  // legacy device result
            }
            if (ios_path_is(f, "/dev/usb"))
                defer = true;
        }
    }

    if (defer && ios_defer_reply(cpu, ctx, result, type))
        return false;
    ios_write_reply(cpu, ctx, result, type);
    if (g_mmio_log)
        fprintf(stderr, "[ios] reply type=%u result=0x%08X\n", type, result);
    return true;
}

void ios_poll_delayed(CPUState* cpu) {
    for (int i = 0; i < IOS_MAX_DELAYED; i++) {
        IosDelayed* d = &g_ios_delayed[i];
        if (!d->active)
            continue;
        if ((u32)cpu->timebase < d->due)
            continue;
        d->active = false;
        ios_write_reply(cpu, d->ctx, d->result, d->type);
        g_ipc_pending = true;
        g_ipc_arm_bits = 0x00000014u;
        interrupt_assert_ipc();
        if (g_mmio_log)
            fprintf(stderr, "[ios] deferred reply type=%u result=0x%08X\n",
                    d->type, d->result);
    }
}

static void ipc_log_request(CPUState* cpu, u32 virt) {
    u32 type = mem_read32(cpu, virt + 0u);
    u32 arg  = mem_read32(cpu, virt + 12u);
    u32 in_phys = mem_read32(cpu, virt + 16u);
    u32 in_len  = mem_read32(cpu, virt + 20u);
    u32 out_phys = mem_read32(cpu, virt + 24u);
    u32 out_len  = mem_read32(cpu, virt + 28u);
    fprintf(stderr,
            "[ipc] request ctx=0x%08X: %08X %08X %08X %08X | %08X %08X %08X %08X"
            " | cb=%08X arg=%08X\n",
            virt,
            type, mem_read32(cpu, virt + 4u), mem_read32(cpu, virt + 8u), arg,
            in_phys, in_len, out_phys, out_len,
            mem_read32(cpu, virt + 32u), mem_read32(cpu, virt + 36u));
    if (type == 1u) {
        // IOS_Open: arg is a pointer to the device path string.
        u32 p = arg + GC_RAM_BASE;
        char path[64];
        for (u32 i = 0; i < sizeof(path) - 1u; i++) {
            char c = (char)(mem_read32(cpu, p + i) >> 24);
            path[i] = c;
            if (c == '\0') break;
        }
        fprintf(stderr, "[ios] open \"%s\"\n", path);
    } else if (in_len != 0u && in_len <= 0x100u) {
        // Dump the first bytes of the request payload (ioctl command block).
        u32 p = in_phys + GC_RAM_BASE;
        fprintf(stderr, "[ios] %s fd=%u arg=0x%X in[%u]=",
                type == 6u ? "ioctl" : type == 3u ? "read" : type == 7u ? "write" : "op",
                mem_read32(cpu, virt + 8u), arg, in_len);
        for (u32 i = 0; i < in_len && i < 32u; i++)
            fprintf(stderr, "%02X", (unsigned)(mem_read32(cpu, p + i) >> 24));
        fprintf(stderr, "\n");
    }
}

bool ios_ipc_contains(u32 addr) {
    return addr >= IPC_BASE && addr < IPC_BASE + IPC_SIZE;
}

u64 ios_ipc_read(CPUState* cpu, u32 off, u8 size) {
    (void)size;
    if (off + 4 <= IPC_SIZE) {
        u32 v = g_ipc_regs[off / 4];
        // Report the Starlet-owned status bits (reply ready) on the IPC status
        // register (0xCD000004) while a reply is outstanding.
        if (off == 0x04u)
            v |= g_ipc_arm_bits;
        if (g_mmio_log && off == 0x04u)
            fprintf(stderr, "[ipc] status read -> 0x%08X (stored=0x%08X arm=0x%08X lr=0x%08X)\n",
                    v, g_ipc_regs[1], g_ipc_arm_bits, cpu->lr);
        return v;
    }
    if (g_mmio_log)
        fprintf(stderr, "[ipc] read%u  0x%08X (pc=0x%08X)\n", size,
                IPC_BASE + off, cpu->pc);
    return 0;
}

void ios_ipc_write(CPUState* cpu, u32 off, u8 size, u64 val) {
    if (g_mmio_log)
        fprintf(stderr, "[ipc] write%u 0x%08X = 0x%llX (pc=0x%08X lr=0x%08X)\n",
                size, IPC_BASE + off, (unsigned long long)val, cpu->pc, cpu->lr);
    if (off + 4 <= IPC_SIZE)
        g_ipc_regs[off / 4] = (u32)val;
    if (off == 0x04u) {
        u32 v = (u32)val;
        // Reply processing finished (0x08): tell the PPC the reply is consumed
        // and it may ack + send the next request.
        if ((v & 0x08u) != 0u && g_ipc_pending)
            g_ipc_arm_bits = 0x00000022u;
        // The PPC acked (0x02): the Starlet has nothing left to report.
        if ((v & 0x02u) != 0u && g_ipc_pending) {
            g_ipc_pending = false;
            g_ipc_arm_bits = 0u;
            interrupt_clear_ipc();
        }
    }
    // A write to IPC_CMD (0x00) is a request; the Starlet would reply and raise
    // the IPC interrupt. Emulate that by asserting the interrupt here.
    if (off == 0x00u) {
        // IPC_CMD carries the request-context physical address (virtual =
        // +0x80000000). Reply through the same context: fill the result/type
        // fields the transaction callback reads, preserving the caller's fields.
        u32 ctx_phys = (u32)val;
        u32 ctx_virt = ctx_phys + GC_RAM_BASE;
        bool replied = true;
        g_ipc_regs[2] = ctx_phys;  // 0xCD000008 = reply-context address
        // The request context is guest memory: either the MEM1 window or the
        // MEM2 window (the SDK's IPC heap lives at the top of MEM2, see the
        // low-memory map at 0x80003130).
        const bool ctx_in_mem1 = ctx_virt >= GC_RAM_BASE &&
                                 ctx_virt + 0x40u <= GC_RAM_BASE + cpu->ram_size;
        const bool ctx_in_mem2 = ctx_virt >= 0x90000000u &&
                                 ctx_virt + 0x40u <= 0x94000000u;
        if (ctx_in_mem1 || ctx_in_mem2) {
            if (g_mmio_log)
                ipc_log_request(cpu, ctx_virt);
            replied = ios_dispatch(cpu, ctx_virt);
        }
        g_ipc_pending = true;
        if (replied) {
            g_ipc_arm_bits = 0x00000014u;  // reply ready: 0x10 (ARM) | 0x04
            interrupt_assert_ipc();
        } else {
            g_ipc_arm_bits = 0u;  // completion is deferred (USB frame)
        }
    }
}

void ios_set_disc_id(const u8 id[32]) {
    memcpy(g_disc_id, id, sizeof(g_disc_id));
}

void ios_di_stats(u64* bytes, u32* reads) {
    if (bytes != NULL)
        *bytes = g_di_bytes;
    if (reads != NULL)
        *reads = g_di_reads;
}
