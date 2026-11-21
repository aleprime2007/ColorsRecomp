#include "mem.h"

#include <stdlib.h>

static u8* g_mem2 = NULL;

bool mem2_init(void) {
    g_mem2 = (u8*)calloc(1, WII_MEM2_SIZE);
    return g_mem2 != NULL;
}

void mem2_shutdown(void) {
    free(g_mem2);
    g_mem2 = NULL;
}

bool mem2_ready(void) { return g_mem2 != NULL; }

u32 mem2_offset(u32 addr) {
    u32 base = 0;
    if (addr >= WII_MEM2_BASE && addr < WII_MEM2_BASE + WII_MEM2_SIZE)
        base = WII_MEM2_BASE;
    else if (addr >= WII_MEM2_UNCACHED && addr < WII_MEM2_UNCACHED + WII_MEM2_SIZE)
        base = WII_MEM2_UNCACHED;
    else
        return (u32)-1;
    return addr - base;
}

u8* mem2_host_pointer(u32 off) {
    if (g_mem2 == NULL || off >= WII_MEM2_SIZE)
        return NULL;
    return g_mem2 + off;
}

u64 mem2_read(u32 off, u8 size) {
    if (g_mem2 == NULL || off >= WII_MEM2_SIZE)
        return 0;
    if (size > WII_MEM2_SIZE - off)      // truncate an access at the very end
        size = (u8)(WII_MEM2_SIZE - off);
    u64 v = 0;
    for (u32 i = 0; i < size; i++) v = (v << 8) | g_mem2[off + i];
    return v;
}

void mem2_write(u32 off, u64 val, u8 size) {
    if (g_mem2 == NULL || off >= WII_MEM2_SIZE)
        return;
    if (size > WII_MEM2_SIZE - off)      // truncate an access at the very end
        size = (u8)(WII_MEM2_SIZE - off);
    for (u32 i = size; i > 0; i--) {
        g_mem2[off + i - 1] = (u8)(val & 0xFFu);
        val >>= 8;
    }
}

u64 ram_read(CPUState* cpu, u32 off, u8 size) {
    if (off >= cpu->ram_size || size > cpu->ram_size - off)
        return 0;
    u64 v = 0;
    for (u32 i = 0; i < size; i++) v = (v << 8) | cpu->ram[off + i];
    return v;
}

void ram_write(CPUState* cpu, u32 off, u64 val, u8 size) {
    if (off >= cpu->ram_size || size > cpu->ram_size - off)
        return;
    for (u32 i = size; i > 0; i--) {
        cpu->ram[off + i - 1] = (u8)(val & 0xFFu);
        val >>= 8;
    }
}
