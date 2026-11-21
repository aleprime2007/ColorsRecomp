// Disc image service: reconstructs the game disc from the extracted files in
// game/sys and game/files so IOS /dev/di reads can be served.
#include "disc.h"

#include "gxruntime/guest_memory.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define DISC_ROOT "game"

#define DISC_MAX_REGIONS 64
#define DISC_MAX_HOST_FILES 128
#define DISC_FST_MAX 16384

typedef struct {
    u32 offset;
    u32 length;
    char path[256];   // host path, "" when the data lives in memory
    u8* mem;          // in-memory contents for the small system files
} DiscRegion;

typedef struct {
    u32 size;
    char path[256];
} HostFile;

static DiscRegion g_regions[DISC_MAX_REGIONS];
static u32 g_region_count;
static HostFile g_host_files[DISC_MAX_HOST_FILES];
static u32 g_host_file_count;

static u8 g_fst[DISC_FST_MAX];
static u32 g_fst_size;
static u8 g_disc_id[32];

static u32 be32(const u8* p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

static bool read_file(const char* path, u8** out, u32* size) {
    FILE* f = fopen(path, "rb");
    if (f == NULL)
        return false;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return false;
    }
    long len = ftell(f);
    if (len <= 0) {
        fclose(f);
        return false;
    }
    rewind(f);
    u8* buf = (u8*)malloc((size_t)len);
    if (buf == NULL) {
        fclose(f);
        return false;
    }
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf);
        fclose(f);
        return false;
    }
    fclose(f);
    *out = buf;
    *size = (u32)len;
    return true;
}

static void scan_host_files(const char* dir) {
    DIR* d = opendir(dir);
    if (d == NULL)
        return;
    struct dirent* e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(path, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            scan_host_files(path);
        } else if (S_ISREG(st.st_mode) && g_host_file_count < DISC_MAX_HOST_FILES) {
            HostFile* hf = &g_host_files[g_host_file_count++];
            hf->size = (u32)st.st_size;
            snprintf(hf->path, sizeof(hf->path), "%s", path);
        }
    }
    closedir(d);
}

static const char* host_path_for_size(u32 size) {
    for (u32 i = 0; i < g_host_file_count; i++) {
        if (g_host_files[i].size == size)
            return g_host_files[i].path;
    }
    return NULL;
}

static DiscRegion* add_region(u32 offset, u32 length) {
    if (g_region_count >= DISC_MAX_REGIONS)
        return NULL;
    DiscRegion* r = &g_regions[g_region_count++];
    memset(r, 0, sizeof(*r));
    r->offset = offset;
    r->length = length;
    return r;
}

static void add_memory_region(u32 offset, const char* path) {
    u8* data = NULL;
    u32 size = 0;
    if (!read_file(path, &data, &size))
        return;
    DiscRegion* r = add_region(offset, size);
    if (r == NULL) {
        free(data);
        return;
    }
    r->mem = data;
    fprintf(stderr, "[disc] %s @ 0x%08X (%u bytes)\n", path, offset, size);
}

// Parse the FST (45 entries at 0x0C, file offsets are stored >>2) and map each
// file entry to the extracted host file with the same length.
static void load_fst(void) {
    u8* data = NULL;
    u32 size = 0;
    if (!read_file(DISC_ROOT "/sys/fst.bin", &data, &size)) {
        fprintf(stderr, "[disc] warning: game/sys/fst.bin not found\n");
        return;
    }
    if (size > sizeof(g_fst))
        size = sizeof(g_fst);
    memcpy(g_fst, data, size);
    g_fst_size = size;
    free(data);

    // Iterate entries while they look structurally valid.
    for (u32 i = 0; 0x0Cu + (i + 1u) * 12u <= g_fst_size && i < 64u; i++) {
        const u8* e = &g_fst[0x0Cu + i * 12u];
        const u8 type = e[0];
        if (type > 1u)
            break;
        if (type != 0u)
            continue;  // directory: no payload
        const u32 offset = be32(e + 4) * 4u;
        const u32 length = be32(e + 8);
        if (length == 0u)
            continue;
        const char* host = host_path_for_size(length);
        if (host == NULL) {
            fprintf(stderr, "[disc] warning: no host file for FST entry %u "
                    "(offset 0x%08X, %u bytes)\n", i, offset, length);
            continue;
        }
        DiscRegion* r = add_region(offset, length);
        if (r == NULL)
            break;
        snprintf(r->path, sizeof(r->path), "%s", host);
    }
    fprintf(stderr, "[disc] FST: %u bytes, %u regions mapped\n",
            g_fst_size, g_region_count);
}

void disc_init(void) {
    g_region_count = 0;
    g_host_file_count = 0;

    scan_host_files(DISC_ROOT "/files");

    // Disc header (32 bytes) for DI ReadDiskID, and the boot/bi2/apploader
    // areas the DVD layer may read.
    u8* id = NULL;
    u32 id_size = 0;
    if (read_file(DISC_ROOT "/disc/header.bin", &id, &id_size) && id_size >= 32u) {
        memcpy(g_disc_id, id, 32u);
        free(id);
    } else {
        memset(g_disc_id, 0, sizeof(g_disc_id));
        memcpy(g_disc_id, "SNCE8P", 6);
    }

    add_memory_region(0x000000u, DISC_ROOT "/sys/boot.bin");
    add_memory_region(0x000440u, DISC_ROOT "/sys/bi2.bin");
    add_memory_region(0x002440u, DISC_ROOT "/sys/apploader.img");

    load_fst();

    // The FST itself: boot.bin +0x424 holds (offset >> 2).
    {
        u32 fst_off = 0;
        DiscRegion* boot = NULL;
        for (u32 i = 0; i < g_region_count; i++) {
            if (g_regions[i].offset == 0u && g_regions[i].mem != NULL)
                boot = &g_regions[i];
        }
        if (boot != NULL && boot->length >= 0x428u)
            fst_off = be32(boot->mem + 0x424u) * 4u;
        if (fst_off == 0u)
            fst_off = 0x80D900u;
        DiscRegion* r = add_region(fst_off, g_fst_size);
        if (r != NULL) {
            r->mem = (u8*)malloc(g_fst_size);
            if (r->mem != NULL)
                memcpy(r->mem, g_fst, g_fst_size);
            fprintf(stderr, "[disc] fst.bin @ 0x%08X (%u bytes)\n", fst_off, g_fst_size);
        }
    }
}

const u8* disc_fst(u32* size) {
    if (size != NULL)
        *size = g_fst_size;
    return g_fst;
}

const u8* disc_disk_id(void) {
    return g_disc_id;
}

const char* disc_region_name(u32 offset) {
    for (u32 i = 0; i < g_region_count; i++) {
        if (offset >= g_regions[i].offset &&
            offset < g_regions[i].offset + g_regions[i].length) {
            return g_regions[i].path[0] != '\0' ? g_regions[i].path : "(memory)";
        }
    }
    return NULL;
}

static bool read_region(const DiscRegion* r, u32 offset, u32 length, u8* dst) {
    if (offset >= r->offset + r->length || offset + length <= r->offset)
        return false;
    const u32 start = offset > r->offset ? offset : r->offset;
    const u32 end = (offset + length) < (r->offset + r->length)
                        ? (offset + length)
                        : (r->offset + r->length);
    const u32 n = end - start;
    const u32 dst_off = start - offset;
    const u32 src_off = start - r->offset;
    if (r->mem != NULL) {
        memcpy(dst + dst_off, r->mem + src_off, n);
        return true;
    }
    FILE* f = fopen(r->path, "rb");
    if (f == NULL)
        return false;
    if (fseek(f, (long)src_off, SEEK_SET) != 0) {
        fclose(f);
        return false;
    }
    size_t got = fread(dst + dst_off, 1, n, f);
    fclose(f);
    return got > 0u;
}

bool disc_read(u32 offset, u32 length, u8* dst) {
    memset(dst, 0, length);
    bool any = false;
    for (u32 i = 0; i < g_region_count; i++) {
        if (read_region(&g_regions[i], offset, length, dst))
            any = true;
    }
    return any;
}
