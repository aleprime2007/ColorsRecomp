// DolRecomp output
// cpu: broadway

#define DOLRECOMP_CPU_BROADWAY 1
#define DOLRECOMP_CPU_NAME "broadway"

#include <string.h>
#include <math.h>
#include "core/cpu.h"

static inline u32 dolrecomp_rotl32(u32 value, u32 sh) {
    sh &= 31u;
    return sh ? ((value << sh) | (value >> (32u - sh))) : value;
}

static inline f32 dolrecomp_f32_from_bits(u32 bits) {
    f32 value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static inline u32 dolrecomp_f32_to_bits(f32 value) {
    u32 bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static inline f64 dolrecomp_f64_from_bits(u64 bits) {
    f64 value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static inline u64 dolrecomp_f64_to_bits(f64 value) {
    u64 bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static inline f64 dolrecomp_ps_round(f64 value) {
    return (f64)(f32)value;
}

static inline f64 dolrecomp_ps_from_bits(u32 bits) {
    return (f64)dolrecomp_f32_from_bits(bits);
}

static inline u32 dolrecomp_ps_to_bits(f64 value) {
    return dolrecomp_f32_to_bits((f32)value);
}


// Function entry points
void func_80004000(CPUState* ctx);
void func_800113C0(CPUState* ctx);
void func_800153C0(CPUState* ctx);
void func_800193C0(CPUState* ctx);
void func_8001D3C0(CPUState* ctx);
void func_800213C0(CPUState* ctx);
void func_800253C0(CPUState* ctx);
void func_800293C0(CPUState* ctx);
void func_8002D3C0(CPUState* ctx);
void func_800313C0(CPUState* ctx);
void func_800353C0(CPUState* ctx);
void func_800393C0(CPUState* ctx);
void func_8003D3C0(CPUState* ctx);
void func_800413C0(CPUState* ctx);
void func_800453C0(CPUState* ctx);
void func_800493C0(CPUState* ctx);
void func_8004D3C0(CPUState* ctx);
void func_800513C0(CPUState* ctx);
void func_800553C0(CPUState* ctx);
void func_800593C0(CPUState* ctx);
void func_8005D3C0(CPUState* ctx);
void func_800613C0(CPUState* ctx);
void func_800653C0(CPUState* ctx);
void func_800693C0(CPUState* ctx);
void func_8006D3C0(CPUState* ctx);
void func_800713C0(CPUState* ctx);
void func_800753C0(CPUState* ctx);
void func_800793C0(CPUState* ctx);
void func_8007D3C0(CPUState* ctx);
void func_800813C0(CPUState* ctx);
void func_800853C0(CPUState* ctx);
void func_800893C0(CPUState* ctx);
void func_8008D3C0(CPUState* ctx);
void func_800913C0(CPUState* ctx);
void func_800953C0(CPUState* ctx);
void func_800993C0(CPUState* ctx);
void func_8009D3C0(CPUState* ctx);
void func_800A13C0(CPUState* ctx);
void func_800A53C0(CPUState* ctx);
void func_800A93C0(CPUState* ctx);
void func_800AD3C0(CPUState* ctx);
void func_800B13C0(CPUState* ctx);
void func_800B53C0(CPUState* ctx);
void func_800B93C0(CPUState* ctx);
void func_800BD3C0(CPUState* ctx);
void func_800C13C0(CPUState* ctx);
void func_800C53C0(CPUState* ctx);
void func_800C93C0(CPUState* ctx);
void func_800CD3C0(CPUState* ctx);
void func_800D13C0(CPUState* ctx);
void func_800D53C0(CPUState* ctx);
void func_800D93C0(CPUState* ctx);
void func_800DD3C0(CPUState* ctx);
void func_800E13C0(CPUState* ctx);
void func_800E53C0(CPUState* ctx);
void func_800E93C0(CPUState* ctx);
void func_800ED3C0(CPUState* ctx);
void func_800F13C0(CPUState* ctx);
void func_800F53C0(CPUState* ctx);
void func_800F93C0(CPUState* ctx);
void func_800FD3C0(CPUState* ctx);
void func_801013C0(CPUState* ctx);
void func_801053C0(CPUState* ctx);
void func_801093C0(CPUState* ctx);
void func_8010D3C0(CPUState* ctx);
void func_801113C0(CPUState* ctx);
void func_801153C0(CPUState* ctx);
void func_801193C0(CPUState* ctx);
void func_8011D3C0(CPUState* ctx);
void func_801213C0(CPUState* ctx);
void func_801253C0(CPUState* ctx);
void func_801293C0(CPUState* ctx);
void func_8012D3C0(CPUState* ctx);
void func_801313C0(CPUState* ctx);
void func_801353C0(CPUState* ctx);
void func_801393C0(CPUState* ctx);
void func_8013D3C0(CPUState* ctx);
void func_801413C0(CPUState* ctx);
void func_801453C0(CPUState* ctx);
void func_801493C0(CPUState* ctx);
void func_8014D3C0(CPUState* ctx);
void func_801513C0(CPUState* ctx);
void func_801553C0(CPUState* ctx);
void func_801593C0(CPUState* ctx);
void func_8015D3C0(CPUState* ctx);
void func_801613C0(CPUState* ctx);
void func_801653C0(CPUState* ctx);
void func_801693C0(CPUState* ctx);
void func_8016D3C0(CPUState* ctx);
void func_801713C0(CPUState* ctx);
void func_801753C0(CPUState* ctx);
void func_801793C0(CPUState* ctx);
void func_8017D3C0(CPUState* ctx);
void func_801813C0(CPUState* ctx);
void func_801853C0(CPUState* ctx);
void func_801893C0(CPUState* ctx);
void func_8018D3C0(CPUState* ctx);
void func_801913C0(CPUState* ctx);
void func_801953C0(CPUState* ctx);
void func_801993C0(CPUState* ctx);
void func_8019D3C0(CPUState* ctx);
void func_801A13C0(CPUState* ctx);
void func_801A53C0(CPUState* ctx);
void func_801A93C0(CPUState* ctx);
void func_801AD3C0(CPUState* ctx);
void func_801B13C0(CPUState* ctx);
void func_801B53C0(CPUState* ctx);
void func_801B93C0(CPUState* ctx);
void func_801BD3C0(CPUState* ctx);
void func_801C13C0(CPUState* ctx);
void func_801C53C0(CPUState* ctx);
void func_801C93C0(CPUState* ctx);
void func_801CD3C0(CPUState* ctx);
void func_801D13C0(CPUState* ctx);
void func_801D53C0(CPUState* ctx);
void func_801D93C0(CPUState* ctx);
void func_801DD3C0(CPUState* ctx);
void func_801E13C0(CPUState* ctx);
void func_801E53C0(CPUState* ctx);
void func_801E93C0(CPUState* ctx);
void func_801ED3C0(CPUState* ctx);
void func_801F13C0(CPUState* ctx);
void func_801F53C0(CPUState* ctx);
void func_801F93C0(CPUState* ctx);
void func_801FD3C0(CPUState* ctx);
void func_802013C0(CPUState* ctx);
void func_802053C0(CPUState* ctx);
void func_802093C0(CPUState* ctx);
void func_8020D3C0(CPUState* ctx);
void func_802113C0(CPUState* ctx);
void func_802153C0(CPUState* ctx);
void func_802193C0(CPUState* ctx);
void func_8021D3C0(CPUState* ctx);
void func_802213C0(CPUState* ctx);
void func_802253C0(CPUState* ctx);
void func_802293C0(CPUState* ctx);
void func_8022D3C0(CPUState* ctx);
void func_802313C0(CPUState* ctx);
void func_802353C0(CPUState* ctx);
void func_802393C0(CPUState* ctx);
void func_8023D3C0(CPUState* ctx);
void func_802413C0(CPUState* ctx);
void func_802453C0(CPUState* ctx);
void func_802493C0(CPUState* ctx);
void func_8024D3C0(CPUState* ctx);
void func_802513C0(CPUState* ctx);
void func_802553C0(CPUState* ctx);
void func_802593C0(CPUState* ctx);
void func_8025D3C0(CPUState* ctx);
void func_802613C0(CPUState* ctx);
void func_802653C0(CPUState* ctx);
void func_802693C0(CPUState* ctx);
void func_8026D3C0(CPUState* ctx);
void func_802713C0(CPUState* ctx);
void func_802753C0(CPUState* ctx);
void func_802793C0(CPUState* ctx);
void func_8027D3C0(CPUState* ctx);
void func_802813C0(CPUState* ctx);
void func_802853C0(CPUState* ctx);
void func_802893C0(CPUState* ctx);
void func_8028D3C0(CPUState* ctx);
void func_802913C0(CPUState* ctx);
void func_802953C0(CPUState* ctx);
void func_802993C0(CPUState* ctx);
void func_8029D3C0(CPUState* ctx);
void func_802A13C0(CPUState* ctx);
void func_802A53C0(CPUState* ctx);
void func_802A93C0(CPUState* ctx);
void func_802AD3C0(CPUState* ctx);
void func_802B13C0(CPUState* ctx);
void func_802B53C0(CPUState* ctx);
void func_802B93C0(CPUState* ctx);
void func_802BD3C0(CPUState* ctx);
void func_802C13C0(CPUState* ctx);
void func_802C53C0(CPUState* ctx);
void func_802C93C0(CPUState* ctx);
void func_802CD3C0(CPUState* ctx);
void func_802D13C0(CPUState* ctx);
void func_802D53C0(CPUState* ctx);
void func_802D93C0(CPUState* ctx);
void func_802DD3C0(CPUState* ctx);
void func_802E13C0(CPUState* ctx);
void func_802E53C0(CPUState* ctx);
void func_802E93C0(CPUState* ctx);
void func_802ED3C0(CPUState* ctx);
void func_802F13C0(CPUState* ctx);
void func_802F53C0(CPUState* ctx);
void func_802F93C0(CPUState* ctx);
void func_802FD3C0(CPUState* ctx);
void func_803013C0(CPUState* ctx);
void func_803053C0(CPUState* ctx);
void func_803093C0(CPUState* ctx);
void func_8030D3C0(CPUState* ctx);
void func_803113C0(CPUState* ctx);
void func_803153C0(CPUState* ctx);
void func_803193C0(CPUState* ctx);
void func_8031D3C0(CPUState* ctx);
void func_803213C0(CPUState* ctx);
void func_803253C0(CPUState* ctx);
void func_803293C0(CPUState* ctx);
void func_8032D3C0(CPUState* ctx);
void func_803313C0(CPUState* ctx);
void func_803353C0(CPUState* ctx);
void func_803393C0(CPUState* ctx);
void func_8033D3C0(CPUState* ctx);
void func_803413C0(CPUState* ctx);
void func_803453C0(CPUState* ctx);
void func_803493C0(CPUState* ctx);
void func_8034D3C0(CPUState* ctx);
void func_803513C0(CPUState* ctx);
void func_803553C0(CPUState* ctx);
void func_803593C0(CPUState* ctx);
void func_8035D3C0(CPUState* ctx);
void func_803613C0(CPUState* ctx);
void func_803653C0(CPUState* ctx);
void func_803693C0(CPUState* ctx);
void func_8036D3C0(CPUState* ctx);
void func_803713C0(CPUState* ctx);
void func_803753C0(CPUState* ctx);
void func_803793C0(CPUState* ctx);
void func_8037D3C0(CPUState* ctx);
void func_803813C0(CPUState* ctx);
void func_803853C0(CPUState* ctx);
void func_803893C0(CPUState* ctx);
void func_8038D3C0(CPUState* ctx);
void func_803913C0(CPUState* ctx);
void func_803953C0(CPUState* ctx);
void func_803993C0(CPUState* ctx);
void func_8039D3C0(CPUState* ctx);
void func_803A13C0(CPUState* ctx);
void func_803A53C0(CPUState* ctx);
void func_803A93C0(CPUState* ctx);
void func_803AD3C0(CPUState* ctx);
void func_803B13C0(CPUState* ctx);
void func_803B53C0(CPUState* ctx);
void func_803B93C0(CPUState* ctx);
void func_803BD3C0(CPUState* ctx);
void func_803C13C0(CPUState* ctx);
void func_803C53C0(CPUState* ctx);
void func_803C93C0(CPUState* ctx);
void func_803CD3C0(CPUState* ctx);
void func_803D13C0(CPUState* ctx);
void func_803D53C0(CPUState* ctx);
void func_803D93C0(CPUState* ctx);
void func_803DD3C0(CPUState* ctx);
void func_803E13C0(CPUState* ctx);
void func_803E53C0(CPUState* ctx);
void func_803E93C0(CPUState* ctx);
void func_803ED3C0(CPUState* ctx);
void func_803F13C0(CPUState* ctx);
void func_803F53C0(CPUState* ctx);
void func_803F93C0(CPUState* ctx);
void func_803FD3C0(CPUState* ctx);
void func_804013C0(CPUState* ctx);
void func_804053C0(CPUState* ctx);
void func_804093C0(CPUState* ctx);
void func_8040D3C0(CPUState* ctx);
void func_804113C0(CPUState* ctx);
void func_804153C0(CPUState* ctx);
void func_804193C0(CPUState* ctx);
void func_8041D3C0(CPUState* ctx);
void func_804213C0(CPUState* ctx);
void func_804253C0(CPUState* ctx);
void func_804293C0(CPUState* ctx);
void func_8042D3C0(CPUState* ctx);
void func_804313C0(CPUState* ctx);
void func_804353C0(CPUState* ctx);
void func_804393C0(CPUState* ctx);
void func_8043D3C0(CPUState* ctx);
void func_804413C0(CPUState* ctx);
void func_804453C0(CPUState* ctx);
void func_804493C0(CPUState* ctx);
void func_8044D3C0(CPUState* ctx);
void func_804513C0(CPUState* ctx);
void func_804553C0(CPUState* ctx);
void func_804593C0(CPUState* ctx);
void func_8045D3C0(CPUState* ctx);
void func_804613C0(CPUState* ctx);
void func_804653C0(CPUState* ctx);
void func_804693C0(CPUState* ctx);
void func_8046D3C0(CPUState* ctx);
void func_804713C0(CPUState* ctx);
void func_804753C0(CPUState* ctx);
void func_804793C0(CPUState* ctx);
void func_8047D3C0(CPUState* ctx);
void func_804813C0(CPUState* ctx);
void func_804853C0(CPUState* ctx);
void func_804893C0(CPUState* ctx);
void func_8048D3C0(CPUState* ctx);
void func_804913C0(CPUState* ctx);
void func_804953C0(CPUState* ctx);
void func_804993C0(CPUState* ctx);
void func_8049D3C0(CPUState* ctx);
void func_804A13C0(CPUState* ctx);
void func_804A53C0(CPUState* ctx);
void func_804A93C0(CPUState* ctx);
void func_804AD3C0(CPUState* ctx);
void func_804B13C0(CPUState* ctx);
void func_804B53C0(CPUState* ctx);
void func_804B93C0(CPUState* ctx);
void func_804BD3C0(CPUState* ctx);
void func_804C13C0(CPUState* ctx);
void func_804C53C0(CPUState* ctx);
void func_804C93C0(CPUState* ctx);
void func_804CD3C0(CPUState* ctx);
void func_804D13C0(CPUState* ctx);
void func_804D53C0(CPUState* ctx);
void func_804D93C0(CPUState* ctx);
void func_804DD3C0(CPUState* ctx);
void func_804E13C0(CPUState* ctx);
void func_804E53C0(CPUState* ctx);
void func_804E93C0(CPUState* ctx);
void func_804ED3C0(CPUState* ctx);
void func_804F13C0(CPUState* ctx);
void func_804F53C0(CPUState* ctx);
void func_804F93C0(CPUState* ctx);
void func_804FD3C0(CPUState* ctx);
void func_805013C0(CPUState* ctx);
void func_805053C0(CPUState* ctx);
void func_805093C0(CPUState* ctx);
void func_8050D3C0(CPUState* ctx);
void func_805113C0(CPUState* ctx);
void func_805153C0(CPUState* ctx);
void func_805193C0(CPUState* ctx);
void func_8051D3C0(CPUState* ctx);
void func_805213C0(CPUState* ctx);
void func_805253C0(CPUState* ctx);
void func_805293C0(CPUState* ctx);
void func_8052D3C0(CPUState* ctx);
void func_805313C0(CPUState* ctx);
void func_805353C0(CPUState* ctx);
void func_805393C0(CPUState* ctx);
void func_8053D3C0(CPUState* ctx);
void func_805413C0(CPUState* ctx);
void func_805453C0(CPUState* ctx);
void func_805493C0(CPUState* ctx);
void func_8054D3C0(CPUState* ctx);
void func_805513C0(CPUState* ctx);
void func_805553C0(CPUState* ctx);
void func_805593C0(CPUState* ctx);
void func_8055D3C0(CPUState* ctx);
void func_805613C0(CPUState* ctx);
void func_805653C0(CPUState* ctx);
void func_805693C0(CPUState* ctx);
void func_8056D3C0(CPUState* ctx);
void func_805713C0(CPUState* ctx);
void func_805753C0(CPUState* ctx);
void func_805793C0(CPUState* ctx);
void func_8057D3C0(CPUState* ctx);
void func_805813C0(CPUState* ctx);
void func_805853C0(CPUState* ctx);
void func_805893C0(CPUState* ctx);
void func_8058D3C0(CPUState* ctx);
void func_805913C0(CPUState* ctx);
void func_805953C0(CPUState* ctx);
void func_805993C0(CPUState* ctx);
void func_8059D3C0(CPUState* ctx);
void func_805A13C0(CPUState* ctx);
void func_805A53C0(CPUState* ctx);
void func_805A93C0(CPUState* ctx);
void func_805AD3C0(CPUState* ctx);
void func_805B13C0(CPUState* ctx);
void func_805B53C0(CPUState* ctx);
void func_805B93C0(CPUState* ctx);
void func_805BD3C0(CPUState* ctx);
void func_805C13C0(CPUState* ctx);
void func_805C53C0(CPUState* ctx);
void func_805C93C0(CPUState* ctx);
void func_805CD3C0(CPUState* ctx);
void func_805D13C0(CPUState* ctx);
void func_805D53C0(CPUState* ctx);
void func_805D93C0(CPUState* ctx);
void func_805DD3C0(CPUState* ctx);
void func_805E13C0(CPUState* ctx);
void func_805E53C0(CPUState* ctx);
void func_805E93C0(CPUState* ctx);
void func_805ED3C0(CPUState* ctx);
void func_805F13C0(CPUState* ctx);
void func_805F53C0(CPUState* ctx);
void func_805F93C0(CPUState* ctx);
void func_805FD3C0(CPUState* ctx);
void func_806013C0(CPUState* ctx);
void func_806053C0(CPUState* ctx);
void func_806093C0(CPUState* ctx);
void func_8060D3C0(CPUState* ctx);
void func_806113C0(CPUState* ctx);
void func_806153C0(CPUState* ctx);
void func_806193C0(CPUState* ctx);
void func_8061D3C0(CPUState* ctx);
void func_806213C0(CPUState* ctx);
void func_806253C0(CPUState* ctx);
void func_806293C0(CPUState* ctx);
void func_8062D3C0(CPUState* ctx);
void func_806313C0(CPUState* ctx);
void func_806353C0(CPUState* ctx);
void func_806393C0(CPUState* ctx);
void func_8063D3C0(CPUState* ctx);
void func_806413C0(CPUState* ctx);
void func_806453C0(CPUState* ctx);
void func_806493C0(CPUState* ctx);
void func_8064D3C0(CPUState* ctx);
void func_806513C0(CPUState* ctx);
void func_806553C0(CPUState* ctx);
void func_806593C0(CPUState* ctx);
void func_8065D3C0(CPUState* ctx);
void func_806613C0(CPUState* ctx);
void func_806653C0(CPUState* ctx);
void func_806693C0(CPUState* ctx);
void func_8066D3C0(CPUState* ctx);
void func_806713C0(CPUState* ctx);
void func_806753C0(CPUState* ctx);
void func_806793C0(CPUState* ctx);
void func_8067D3C0(CPUState* ctx);
void func_806813C0(CPUState* ctx);
void func_806853C0(CPUState* ctx);
void func_806893C0(CPUState* ctx);
void func_8068D3C0(CPUState* ctx);
void func_806913C0(CPUState* ctx);
void func_806953C0(CPUState* ctx);
void func_806993C0(CPUState* ctx);
void func_8069D3C0(CPUState* ctx);
void func_806A13C0(CPUState* ctx);
void func_806A53C0(CPUState* ctx);
void func_806A93C0(CPUState* ctx);
void func_806AD3C0(CPUState* ctx);
void func_806B13C0(CPUState* ctx);
void func_806B53C0(CPUState* ctx);
void func_806B93C0(CPUState* ctx);
void func_806BD3C0(CPUState* ctx);
void func_806C13C0(CPUState* ctx);
void func_806C53C0(CPUState* ctx);
void func_806C93C0(CPUState* ctx);
void func_806CD3C0(CPUState* ctx);
void func_806D13C0(CPUState* ctx);
void func_806D53C0(CPUState* ctx);
void func_806D93C0(CPUState* ctx);
void func_806DD3C0(CPUState* ctx);
void func_806E13C0(CPUState* ctx);
void func_806E53C0(CPUState* ctx);
void func_806E93C0(CPUState* ctx);
void func_806ED3C0(CPUState* ctx);
void func_806F13C0(CPUState* ctx);
void func_806F53C0(CPUState* ctx);
void func_806F93C0(CPUState* ctx);
void func_806FD3C0(CPUState* ctx);
void func_807013C0(CPUState* ctx);
void func_807053C0(CPUState* ctx);
void func_807093C0(CPUState* ctx);
void func_8070D3C0(CPUState* ctx);
void func_807113C0(CPUState* ctx);

#define DOLRECOMP_ENTRY_POINT 0x80004050u

typedef void (*DolRecompFunction)(CPUState* ctx);

static inline int dolrecomp_call(CPUState* ctx, u32 address) {
    if (ppc_host_call(ctx, address)) return 1;
    if (address >= 0x80004000u && address < 0x80006720u && ((address - 0x80004000u) & 3u) == 0u) { func_80004000(ctx); return 1; }
    if (address >= 0x800113C0u && address < 0x800153C0u && ((address - 0x800113C0u) & 3u) == 0u) { func_800113C0(ctx); return 1; }
    if (address >= 0x800153C0u && address < 0x800193C0u && ((address - 0x800153C0u) & 3u) == 0u) { func_800153C0(ctx); return 1; }
    if (address >= 0x800193C0u && address < 0x8001D3C0u && ((address - 0x800193C0u) & 3u) == 0u) { func_800193C0(ctx); return 1; }
    if (address >= 0x8001D3C0u && address < 0x800213C0u && ((address - 0x8001D3C0u) & 3u) == 0u) { func_8001D3C0(ctx); return 1; }
    if (address >= 0x800213C0u && address < 0x800253C0u && ((address - 0x800213C0u) & 3u) == 0u) { func_800213C0(ctx); return 1; }
    if (address >= 0x800253C0u && address < 0x800293C0u && ((address - 0x800253C0u) & 3u) == 0u) { func_800253C0(ctx); return 1; }
    if (address >= 0x800293C0u && address < 0x8002D3C0u && ((address - 0x800293C0u) & 3u) == 0u) { func_800293C0(ctx); return 1; }
    if (address >= 0x8002D3C0u && address < 0x800313C0u && ((address - 0x8002D3C0u) & 3u) == 0u) { func_8002D3C0(ctx); return 1; }
    if (address >= 0x800313C0u && address < 0x800353C0u && ((address - 0x800313C0u) & 3u) == 0u) { func_800313C0(ctx); return 1; }
    if (address >= 0x800353C0u && address < 0x800393C0u && ((address - 0x800353C0u) & 3u) == 0u) { func_800353C0(ctx); return 1; }
    if (address >= 0x800393C0u && address < 0x8003D3C0u && ((address - 0x800393C0u) & 3u) == 0u) { func_800393C0(ctx); return 1; }
    if (address >= 0x8003D3C0u && address < 0x800413C0u && ((address - 0x8003D3C0u) & 3u) == 0u) { func_8003D3C0(ctx); return 1; }
    if (address >= 0x800413C0u && address < 0x800453C0u && ((address - 0x800413C0u) & 3u) == 0u) { func_800413C0(ctx); return 1; }
    if (address >= 0x800453C0u && address < 0x800493C0u && ((address - 0x800453C0u) & 3u) == 0u) { func_800453C0(ctx); return 1; }
    if (address >= 0x800493C0u && address < 0x8004D3C0u && ((address - 0x800493C0u) & 3u) == 0u) { func_800493C0(ctx); return 1; }
    if (address >= 0x8004D3C0u && address < 0x800513C0u && ((address - 0x8004D3C0u) & 3u) == 0u) { func_8004D3C0(ctx); return 1; }
    if (address >= 0x800513C0u && address < 0x800553C0u && ((address - 0x800513C0u) & 3u) == 0u) { func_800513C0(ctx); return 1; }
    if (address >= 0x800553C0u && address < 0x800593C0u && ((address - 0x800553C0u) & 3u) == 0u) { func_800553C0(ctx); return 1; }
    if (address >= 0x800593C0u && address < 0x8005D3C0u && ((address - 0x800593C0u) & 3u) == 0u) { func_800593C0(ctx); return 1; }
    if (address >= 0x8005D3C0u && address < 0x800613C0u && ((address - 0x8005D3C0u) & 3u) == 0u) { func_8005D3C0(ctx); return 1; }
    if (address >= 0x800613C0u && address < 0x800653C0u && ((address - 0x800613C0u) & 3u) == 0u) { func_800613C0(ctx); return 1; }
    if (address >= 0x800653C0u && address < 0x800693C0u && ((address - 0x800653C0u) & 3u) == 0u) { func_800653C0(ctx); return 1; }
    if (address >= 0x800693C0u && address < 0x8006D3C0u && ((address - 0x800693C0u) & 3u) == 0u) { func_800693C0(ctx); return 1; }
    if (address >= 0x8006D3C0u && address < 0x800713C0u && ((address - 0x8006D3C0u) & 3u) == 0u) { func_8006D3C0(ctx); return 1; }
    if (address >= 0x800713C0u && address < 0x800753C0u && ((address - 0x800713C0u) & 3u) == 0u) { func_800713C0(ctx); return 1; }
    if (address >= 0x800753C0u && address < 0x800793C0u && ((address - 0x800753C0u) & 3u) == 0u) { func_800753C0(ctx); return 1; }
    if (address >= 0x800793C0u && address < 0x8007D3C0u && ((address - 0x800793C0u) & 3u) == 0u) { func_800793C0(ctx); return 1; }
    if (address >= 0x8007D3C0u && address < 0x800813C0u && ((address - 0x8007D3C0u) & 3u) == 0u) { func_8007D3C0(ctx); return 1; }
    if (address >= 0x800813C0u && address < 0x800853C0u && ((address - 0x800813C0u) & 3u) == 0u) { func_800813C0(ctx); return 1; }
    if (address >= 0x800853C0u && address < 0x800893C0u && ((address - 0x800853C0u) & 3u) == 0u) { func_800853C0(ctx); return 1; }
    if (address >= 0x800893C0u && address < 0x8008D3C0u && ((address - 0x800893C0u) & 3u) == 0u) { func_800893C0(ctx); return 1; }
    if (address >= 0x8008D3C0u && address < 0x800913C0u && ((address - 0x8008D3C0u) & 3u) == 0u) { func_8008D3C0(ctx); return 1; }
    if (address >= 0x800913C0u && address < 0x800953C0u && ((address - 0x800913C0u) & 3u) == 0u) { func_800913C0(ctx); return 1; }
    if (address >= 0x800953C0u && address < 0x800993C0u && ((address - 0x800953C0u) & 3u) == 0u) { func_800953C0(ctx); return 1; }
    if (address >= 0x800993C0u && address < 0x8009D3C0u && ((address - 0x800993C0u) & 3u) == 0u) { func_800993C0(ctx); return 1; }
    if (address >= 0x8009D3C0u && address < 0x800A13C0u && ((address - 0x8009D3C0u) & 3u) == 0u) { func_8009D3C0(ctx); return 1; }
    if (address >= 0x800A13C0u && address < 0x800A53C0u && ((address - 0x800A13C0u) & 3u) == 0u) { func_800A13C0(ctx); return 1; }
    if (address >= 0x800A53C0u && address < 0x800A93C0u && ((address - 0x800A53C0u) & 3u) == 0u) { func_800A53C0(ctx); return 1; }
    if (address >= 0x800A93C0u && address < 0x800AD3C0u && ((address - 0x800A93C0u) & 3u) == 0u) { func_800A93C0(ctx); return 1; }
    if (address >= 0x800AD3C0u && address < 0x800B13C0u && ((address - 0x800AD3C0u) & 3u) == 0u) { func_800AD3C0(ctx); return 1; }
    if (address >= 0x800B13C0u && address < 0x800B53C0u && ((address - 0x800B13C0u) & 3u) == 0u) { func_800B13C0(ctx); return 1; }
    if (address >= 0x800B53C0u && address < 0x800B93C0u && ((address - 0x800B53C0u) & 3u) == 0u) { func_800B53C0(ctx); return 1; }
    if (address >= 0x800B93C0u && address < 0x800BD3C0u && ((address - 0x800B93C0u) & 3u) == 0u) { func_800B93C0(ctx); return 1; }
    if (address >= 0x800BD3C0u && address < 0x800C13C0u && ((address - 0x800BD3C0u) & 3u) == 0u) { func_800BD3C0(ctx); return 1; }
    if (address >= 0x800C13C0u && address < 0x800C53C0u && ((address - 0x800C13C0u) & 3u) == 0u) { func_800C13C0(ctx); return 1; }
    if (address >= 0x800C53C0u && address < 0x800C93C0u && ((address - 0x800C53C0u) & 3u) == 0u) { func_800C53C0(ctx); return 1; }
    if (address >= 0x800C93C0u && address < 0x800CD3C0u && ((address - 0x800C93C0u) & 3u) == 0u) { func_800C93C0(ctx); return 1; }
    if (address >= 0x800CD3C0u && address < 0x800D13C0u && ((address - 0x800CD3C0u) & 3u) == 0u) { func_800CD3C0(ctx); return 1; }
    if (address >= 0x800D13C0u && address < 0x800D53C0u && ((address - 0x800D13C0u) & 3u) == 0u) { func_800D13C0(ctx); return 1; }
    if (address >= 0x800D53C0u && address < 0x800D93C0u && ((address - 0x800D53C0u) & 3u) == 0u) { func_800D53C0(ctx); return 1; }
    if (address >= 0x800D93C0u && address < 0x800DD3C0u && ((address - 0x800D93C0u) & 3u) == 0u) { func_800D93C0(ctx); return 1; }
    if (address >= 0x800DD3C0u && address < 0x800E13C0u && ((address - 0x800DD3C0u) & 3u) == 0u) { func_800DD3C0(ctx); return 1; }
    if (address >= 0x800E13C0u && address < 0x800E53C0u && ((address - 0x800E13C0u) & 3u) == 0u) { func_800E13C0(ctx); return 1; }
    if (address >= 0x800E53C0u && address < 0x800E93C0u && ((address - 0x800E53C0u) & 3u) == 0u) { func_800E53C0(ctx); return 1; }
    if (address >= 0x800E93C0u && address < 0x800ED3C0u && ((address - 0x800E93C0u) & 3u) == 0u) { func_800E93C0(ctx); return 1; }
    if (address >= 0x800ED3C0u && address < 0x800F13C0u && ((address - 0x800ED3C0u) & 3u) == 0u) { func_800ED3C0(ctx); return 1; }
    if (address >= 0x800F13C0u && address < 0x800F53C0u && ((address - 0x800F13C0u) & 3u) == 0u) { func_800F13C0(ctx); return 1; }
    if (address >= 0x800F53C0u && address < 0x800F93C0u && ((address - 0x800F53C0u) & 3u) == 0u) { func_800F53C0(ctx); return 1; }
    if (address >= 0x800F93C0u && address < 0x800FD3C0u && ((address - 0x800F93C0u) & 3u) == 0u) { func_800F93C0(ctx); return 1; }
    if (address >= 0x800FD3C0u && address < 0x801013C0u && ((address - 0x800FD3C0u) & 3u) == 0u) { func_800FD3C0(ctx); return 1; }
    if (address >= 0x801013C0u && address < 0x801053C0u && ((address - 0x801013C0u) & 3u) == 0u) { func_801013C0(ctx); return 1; }
    if (address >= 0x801053C0u && address < 0x801093C0u && ((address - 0x801053C0u) & 3u) == 0u) { func_801053C0(ctx); return 1; }
    if (address >= 0x801093C0u && address < 0x8010D3C0u && ((address - 0x801093C0u) & 3u) == 0u) { func_801093C0(ctx); return 1; }
    if (address >= 0x8010D3C0u && address < 0x801113C0u && ((address - 0x8010D3C0u) & 3u) == 0u) { func_8010D3C0(ctx); return 1; }
    if (address >= 0x801113C0u && address < 0x801153C0u && ((address - 0x801113C0u) & 3u) == 0u) { func_801113C0(ctx); return 1; }
    if (address >= 0x801153C0u && address < 0x801193C0u && ((address - 0x801153C0u) & 3u) == 0u) { func_801153C0(ctx); return 1; }
    if (address >= 0x801193C0u && address < 0x8011D3C0u && ((address - 0x801193C0u) & 3u) == 0u) { func_801193C0(ctx); return 1; }
    if (address >= 0x8011D3C0u && address < 0x801213C0u && ((address - 0x8011D3C0u) & 3u) == 0u) { func_8011D3C0(ctx); return 1; }
    if (address >= 0x801213C0u && address < 0x801253C0u && ((address - 0x801213C0u) & 3u) == 0u) { func_801213C0(ctx); return 1; }
    if (address >= 0x801253C0u && address < 0x801293C0u && ((address - 0x801253C0u) & 3u) == 0u) { func_801253C0(ctx); return 1; }
    if (address >= 0x801293C0u && address < 0x8012D3C0u && ((address - 0x801293C0u) & 3u) == 0u) { func_801293C0(ctx); return 1; }
    if (address >= 0x8012D3C0u && address < 0x801313C0u && ((address - 0x8012D3C0u) & 3u) == 0u) { func_8012D3C0(ctx); return 1; }
    if (address >= 0x801313C0u && address < 0x801353C0u && ((address - 0x801313C0u) & 3u) == 0u) { func_801313C0(ctx); return 1; }
    if (address >= 0x801353C0u && address < 0x801393C0u && ((address - 0x801353C0u) & 3u) == 0u) { func_801353C0(ctx); return 1; }
    if (address >= 0x801393C0u && address < 0x8013D3C0u && ((address - 0x801393C0u) & 3u) == 0u) { func_801393C0(ctx); return 1; }
    if (address >= 0x8013D3C0u && address < 0x801413C0u && ((address - 0x8013D3C0u) & 3u) == 0u) { func_8013D3C0(ctx); return 1; }
    if (address >= 0x801413C0u && address < 0x801453C0u && ((address - 0x801413C0u) & 3u) == 0u) { func_801413C0(ctx); return 1; }
    if (address >= 0x801453C0u && address < 0x801493C0u && ((address - 0x801453C0u) & 3u) == 0u) { func_801453C0(ctx); return 1; }
    if (address >= 0x801493C0u && address < 0x8014D3C0u && ((address - 0x801493C0u) & 3u) == 0u) { func_801493C0(ctx); return 1; }
    if (address >= 0x8014D3C0u && address < 0x801513C0u && ((address - 0x8014D3C0u) & 3u) == 0u) { func_8014D3C0(ctx); return 1; }
    if (address >= 0x801513C0u && address < 0x801553C0u && ((address - 0x801513C0u) & 3u) == 0u) { func_801513C0(ctx); return 1; }
    if (address >= 0x801553C0u && address < 0x801593C0u && ((address - 0x801553C0u) & 3u) == 0u) { func_801553C0(ctx); return 1; }
    if (address >= 0x801593C0u && address < 0x8015D3C0u && ((address - 0x801593C0u) & 3u) == 0u) { func_801593C0(ctx); return 1; }
    if (address >= 0x8015D3C0u && address < 0x801613C0u && ((address - 0x8015D3C0u) & 3u) == 0u) { func_8015D3C0(ctx); return 1; }
    if (address >= 0x801613C0u && address < 0x801653C0u && ((address - 0x801613C0u) & 3u) == 0u) { func_801613C0(ctx); return 1; }
    if (address >= 0x801653C0u && address < 0x801693C0u && ((address - 0x801653C0u) & 3u) == 0u) { func_801653C0(ctx); return 1; }
    if (address >= 0x801693C0u && address < 0x8016D3C0u && ((address - 0x801693C0u) & 3u) == 0u) { func_801693C0(ctx); return 1; }
    if (address >= 0x8016D3C0u && address < 0x801713C0u && ((address - 0x8016D3C0u) & 3u) == 0u) { func_8016D3C0(ctx); return 1; }
    if (address >= 0x801713C0u && address < 0x801753C0u && ((address - 0x801713C0u) & 3u) == 0u) { func_801713C0(ctx); return 1; }
    if (address >= 0x801753C0u && address < 0x801793C0u && ((address - 0x801753C0u) & 3u) == 0u) { func_801753C0(ctx); return 1; }
    if (address >= 0x801793C0u && address < 0x8017D3C0u && ((address - 0x801793C0u) & 3u) == 0u) { func_801793C0(ctx); return 1; }
    if (address >= 0x8017D3C0u && address < 0x801813C0u && ((address - 0x8017D3C0u) & 3u) == 0u) { func_8017D3C0(ctx); return 1; }
    if (address >= 0x801813C0u && address < 0x801853C0u && ((address - 0x801813C0u) & 3u) == 0u) { func_801813C0(ctx); return 1; }
    if (address >= 0x801853C0u && address < 0x801893C0u && ((address - 0x801853C0u) & 3u) == 0u) { func_801853C0(ctx); return 1; }
    if (address >= 0x801893C0u && address < 0x8018D3C0u && ((address - 0x801893C0u) & 3u) == 0u) { func_801893C0(ctx); return 1; }
    if (address >= 0x8018D3C0u && address < 0x801913C0u && ((address - 0x8018D3C0u) & 3u) == 0u) { func_8018D3C0(ctx); return 1; }
    if (address >= 0x801913C0u && address < 0x801953C0u && ((address - 0x801913C0u) & 3u) == 0u) { func_801913C0(ctx); return 1; }
    if (address >= 0x801953C0u && address < 0x801993C0u && ((address - 0x801953C0u) & 3u) == 0u) { func_801953C0(ctx); return 1; }
    if (address >= 0x801993C0u && address < 0x8019D3C0u && ((address - 0x801993C0u) & 3u) == 0u) { func_801993C0(ctx); return 1; }
    if (address >= 0x8019D3C0u && address < 0x801A13C0u && ((address - 0x8019D3C0u) & 3u) == 0u) { func_8019D3C0(ctx); return 1; }
    if (address >= 0x801A13C0u && address < 0x801A53C0u && ((address - 0x801A13C0u) & 3u) == 0u) { func_801A13C0(ctx); return 1; }
    if (address >= 0x801A53C0u && address < 0x801A93C0u && ((address - 0x801A53C0u) & 3u) == 0u) { func_801A53C0(ctx); return 1; }
    if (address >= 0x801A93C0u && address < 0x801AD3C0u && ((address - 0x801A93C0u) & 3u) == 0u) { func_801A93C0(ctx); return 1; }
    if (address >= 0x801AD3C0u && address < 0x801B13C0u && ((address - 0x801AD3C0u) & 3u) == 0u) { func_801AD3C0(ctx); return 1; }
    if (address >= 0x801B13C0u && address < 0x801B53C0u && ((address - 0x801B13C0u) & 3u) == 0u) { func_801B13C0(ctx); return 1; }
    if (address >= 0x801B53C0u && address < 0x801B93C0u && ((address - 0x801B53C0u) & 3u) == 0u) { func_801B53C0(ctx); return 1; }
    if (address >= 0x801B93C0u && address < 0x801BD3C0u && ((address - 0x801B93C0u) & 3u) == 0u) { func_801B93C0(ctx); return 1; }
    if (address >= 0x801BD3C0u && address < 0x801C13C0u && ((address - 0x801BD3C0u) & 3u) == 0u) { func_801BD3C0(ctx); return 1; }
    if (address >= 0x801C13C0u && address < 0x801C53C0u && ((address - 0x801C13C0u) & 3u) == 0u) { func_801C13C0(ctx); return 1; }
    if (address >= 0x801C53C0u && address < 0x801C93C0u && ((address - 0x801C53C0u) & 3u) == 0u) { func_801C53C0(ctx); return 1; }
    if (address >= 0x801C93C0u && address < 0x801CD3C0u && ((address - 0x801C93C0u) & 3u) == 0u) { func_801C93C0(ctx); return 1; }
    if (address >= 0x801CD3C0u && address < 0x801D13C0u && ((address - 0x801CD3C0u) & 3u) == 0u) { func_801CD3C0(ctx); return 1; }
    if (address >= 0x801D13C0u && address < 0x801D53C0u && ((address - 0x801D13C0u) & 3u) == 0u) { func_801D13C0(ctx); return 1; }
    if (address >= 0x801D53C0u && address < 0x801D93C0u && ((address - 0x801D53C0u) & 3u) == 0u) { func_801D53C0(ctx); return 1; }
    if (address >= 0x801D93C0u && address < 0x801DD3C0u && ((address - 0x801D93C0u) & 3u) == 0u) { func_801D93C0(ctx); return 1; }
    if (address >= 0x801DD3C0u && address < 0x801E13C0u && ((address - 0x801DD3C0u) & 3u) == 0u) { func_801DD3C0(ctx); return 1; }
    if (address >= 0x801E13C0u && address < 0x801E53C0u && ((address - 0x801E13C0u) & 3u) == 0u) { func_801E13C0(ctx); return 1; }
    if (address >= 0x801E53C0u && address < 0x801E93C0u && ((address - 0x801E53C0u) & 3u) == 0u) { func_801E53C0(ctx); return 1; }
    if (address >= 0x801E93C0u && address < 0x801ED3C0u && ((address - 0x801E93C0u) & 3u) == 0u) { func_801E93C0(ctx); return 1; }
    if (address >= 0x801ED3C0u && address < 0x801F13C0u && ((address - 0x801ED3C0u) & 3u) == 0u) { func_801ED3C0(ctx); return 1; }
    if (address >= 0x801F13C0u && address < 0x801F53C0u && ((address - 0x801F13C0u) & 3u) == 0u) { func_801F13C0(ctx); return 1; }
    if (address >= 0x801F53C0u && address < 0x801F93C0u && ((address - 0x801F53C0u) & 3u) == 0u) { func_801F53C0(ctx); return 1; }
    if (address >= 0x801F93C0u && address < 0x801FD3C0u && ((address - 0x801F93C0u) & 3u) == 0u) { func_801F93C0(ctx); return 1; }
    if (address >= 0x801FD3C0u && address < 0x802013C0u && ((address - 0x801FD3C0u) & 3u) == 0u) { func_801FD3C0(ctx); return 1; }
    if (address >= 0x802013C0u && address < 0x802053C0u && ((address - 0x802013C0u) & 3u) == 0u) { func_802013C0(ctx); return 1; }
    if (address >= 0x802053C0u && address < 0x802093C0u && ((address - 0x802053C0u) & 3u) == 0u) { func_802053C0(ctx); return 1; }
    if (address >= 0x802093C0u && address < 0x8020D3C0u && ((address - 0x802093C0u) & 3u) == 0u) { func_802093C0(ctx); return 1; }
    if (address >= 0x8020D3C0u && address < 0x802113C0u && ((address - 0x8020D3C0u) & 3u) == 0u) { func_8020D3C0(ctx); return 1; }
    if (address >= 0x802113C0u && address < 0x802153C0u && ((address - 0x802113C0u) & 3u) == 0u) { func_802113C0(ctx); return 1; }
    if (address >= 0x802153C0u && address < 0x802193C0u && ((address - 0x802153C0u) & 3u) == 0u) { func_802153C0(ctx); return 1; }
    if (address >= 0x802193C0u && address < 0x8021D3C0u && ((address - 0x802193C0u) & 3u) == 0u) { func_802193C0(ctx); return 1; }
    if (address >= 0x8021D3C0u && address < 0x802213C0u && ((address - 0x8021D3C0u) & 3u) == 0u) { func_8021D3C0(ctx); return 1; }
    if (address >= 0x802213C0u && address < 0x802253C0u && ((address - 0x802213C0u) & 3u) == 0u) { func_802213C0(ctx); return 1; }
    if (address >= 0x802253C0u && address < 0x802293C0u && ((address - 0x802253C0u) & 3u) == 0u) { func_802253C0(ctx); return 1; }
    if (address >= 0x802293C0u && address < 0x8022D3C0u && ((address - 0x802293C0u) & 3u) == 0u) { func_802293C0(ctx); return 1; }
    if (address >= 0x8022D3C0u && address < 0x802313C0u && ((address - 0x8022D3C0u) & 3u) == 0u) { func_8022D3C0(ctx); return 1; }
    if (address >= 0x802313C0u && address < 0x802353C0u && ((address - 0x802313C0u) & 3u) == 0u) { func_802313C0(ctx); return 1; }
    if (address >= 0x802353C0u && address < 0x802393C0u && ((address - 0x802353C0u) & 3u) == 0u) { func_802353C0(ctx); return 1; }
    if (address >= 0x802393C0u && address < 0x8023D3C0u && ((address - 0x802393C0u) & 3u) == 0u) { func_802393C0(ctx); return 1; }
    if (address >= 0x8023D3C0u && address < 0x802413C0u && ((address - 0x8023D3C0u) & 3u) == 0u) { func_8023D3C0(ctx); return 1; }
    if (address >= 0x802413C0u && address < 0x802453C0u && ((address - 0x802413C0u) & 3u) == 0u) { func_802413C0(ctx); return 1; }
    if (address >= 0x802453C0u && address < 0x802493C0u && ((address - 0x802453C0u) & 3u) == 0u) { func_802453C0(ctx); return 1; }
    if (address >= 0x802493C0u && address < 0x8024D3C0u && ((address - 0x802493C0u) & 3u) == 0u) { func_802493C0(ctx); return 1; }
    if (address >= 0x8024D3C0u && address < 0x802513C0u && ((address - 0x8024D3C0u) & 3u) == 0u) { func_8024D3C0(ctx); return 1; }
    if (address >= 0x802513C0u && address < 0x802553C0u && ((address - 0x802513C0u) & 3u) == 0u) { func_802513C0(ctx); return 1; }
    if (address >= 0x802553C0u && address < 0x802593C0u && ((address - 0x802553C0u) & 3u) == 0u) { func_802553C0(ctx); return 1; }
    if (address >= 0x802593C0u && address < 0x8025D3C0u && ((address - 0x802593C0u) & 3u) == 0u) { func_802593C0(ctx); return 1; }
    if (address >= 0x8025D3C0u && address < 0x802613C0u && ((address - 0x8025D3C0u) & 3u) == 0u) { func_8025D3C0(ctx); return 1; }
    if (address >= 0x802613C0u && address < 0x802653C0u && ((address - 0x802613C0u) & 3u) == 0u) { func_802613C0(ctx); return 1; }
    if (address >= 0x802653C0u && address < 0x802693C0u && ((address - 0x802653C0u) & 3u) == 0u) { func_802653C0(ctx); return 1; }
    if (address >= 0x802693C0u && address < 0x8026D3C0u && ((address - 0x802693C0u) & 3u) == 0u) { func_802693C0(ctx); return 1; }
    if (address >= 0x8026D3C0u && address < 0x802713C0u && ((address - 0x8026D3C0u) & 3u) == 0u) { func_8026D3C0(ctx); return 1; }
    if (address >= 0x802713C0u && address < 0x802753C0u && ((address - 0x802713C0u) & 3u) == 0u) { func_802713C0(ctx); return 1; }
    if (address >= 0x802753C0u && address < 0x802793C0u && ((address - 0x802753C0u) & 3u) == 0u) { func_802753C0(ctx); return 1; }
    if (address >= 0x802793C0u && address < 0x8027D3C0u && ((address - 0x802793C0u) & 3u) == 0u) { func_802793C0(ctx); return 1; }
    if (address >= 0x8027D3C0u && address < 0x802813C0u && ((address - 0x8027D3C0u) & 3u) == 0u) { func_8027D3C0(ctx); return 1; }
    if (address >= 0x802813C0u && address < 0x802853C0u && ((address - 0x802813C0u) & 3u) == 0u) { func_802813C0(ctx); return 1; }
    if (address >= 0x802853C0u && address < 0x802893C0u && ((address - 0x802853C0u) & 3u) == 0u) { func_802853C0(ctx); return 1; }
    if (address >= 0x802893C0u && address < 0x8028D3C0u && ((address - 0x802893C0u) & 3u) == 0u) { func_802893C0(ctx); return 1; }
    if (address >= 0x8028D3C0u && address < 0x802913C0u && ((address - 0x8028D3C0u) & 3u) == 0u) { func_8028D3C0(ctx); return 1; }
    if (address >= 0x802913C0u && address < 0x802953C0u && ((address - 0x802913C0u) & 3u) == 0u) { func_802913C0(ctx); return 1; }
    if (address >= 0x802953C0u && address < 0x802993C0u && ((address - 0x802953C0u) & 3u) == 0u) { func_802953C0(ctx); return 1; }
    if (address >= 0x802993C0u && address < 0x8029D3C0u && ((address - 0x802993C0u) & 3u) == 0u) { func_802993C0(ctx); return 1; }
    if (address >= 0x8029D3C0u && address < 0x802A13C0u && ((address - 0x8029D3C0u) & 3u) == 0u) { func_8029D3C0(ctx); return 1; }
    if (address >= 0x802A13C0u && address < 0x802A53C0u && ((address - 0x802A13C0u) & 3u) == 0u) { func_802A13C0(ctx); return 1; }
    if (address >= 0x802A53C0u && address < 0x802A93C0u && ((address - 0x802A53C0u) & 3u) == 0u) { func_802A53C0(ctx); return 1; }
    if (address >= 0x802A93C0u && address < 0x802AD3C0u && ((address - 0x802A93C0u) & 3u) == 0u) { func_802A93C0(ctx); return 1; }
    if (address >= 0x802AD3C0u && address < 0x802B13C0u && ((address - 0x802AD3C0u) & 3u) == 0u) { func_802AD3C0(ctx); return 1; }
    if (address >= 0x802B13C0u && address < 0x802B53C0u && ((address - 0x802B13C0u) & 3u) == 0u) { func_802B13C0(ctx); return 1; }
    if (address >= 0x802B53C0u && address < 0x802B93C0u && ((address - 0x802B53C0u) & 3u) == 0u) { func_802B53C0(ctx); return 1; }
    if (address >= 0x802B93C0u && address < 0x802BD3C0u && ((address - 0x802B93C0u) & 3u) == 0u) { func_802B93C0(ctx); return 1; }
    if (address >= 0x802BD3C0u && address < 0x802C13C0u && ((address - 0x802BD3C0u) & 3u) == 0u) { func_802BD3C0(ctx); return 1; }
    if (address >= 0x802C13C0u && address < 0x802C53C0u && ((address - 0x802C13C0u) & 3u) == 0u) { func_802C13C0(ctx); return 1; }
    if (address >= 0x802C53C0u && address < 0x802C93C0u && ((address - 0x802C53C0u) & 3u) == 0u) { func_802C53C0(ctx); return 1; }
    if (address >= 0x802C93C0u && address < 0x802CD3C0u && ((address - 0x802C93C0u) & 3u) == 0u) { func_802C93C0(ctx); return 1; }
    if (address >= 0x802CD3C0u && address < 0x802D13C0u && ((address - 0x802CD3C0u) & 3u) == 0u) { func_802CD3C0(ctx); return 1; }
    if (address >= 0x802D13C0u && address < 0x802D53C0u && ((address - 0x802D13C0u) & 3u) == 0u) { func_802D13C0(ctx); return 1; }
    if (address >= 0x802D53C0u && address < 0x802D93C0u && ((address - 0x802D53C0u) & 3u) == 0u) { func_802D53C0(ctx); return 1; }
    if (address >= 0x802D93C0u && address < 0x802DD3C0u && ((address - 0x802D93C0u) & 3u) == 0u) { func_802D93C0(ctx); return 1; }
    if (address >= 0x802DD3C0u && address < 0x802E13C0u && ((address - 0x802DD3C0u) & 3u) == 0u) { func_802DD3C0(ctx); return 1; }
    if (address >= 0x802E13C0u && address < 0x802E53C0u && ((address - 0x802E13C0u) & 3u) == 0u) { func_802E13C0(ctx); return 1; }
    if (address >= 0x802E53C0u && address < 0x802E93C0u && ((address - 0x802E53C0u) & 3u) == 0u) { func_802E53C0(ctx); return 1; }
    if (address >= 0x802E93C0u && address < 0x802ED3C0u && ((address - 0x802E93C0u) & 3u) == 0u) { func_802E93C0(ctx); return 1; }
    if (address >= 0x802ED3C0u && address < 0x802F13C0u && ((address - 0x802ED3C0u) & 3u) == 0u) { func_802ED3C0(ctx); return 1; }
    if (address >= 0x802F13C0u && address < 0x802F53C0u && ((address - 0x802F13C0u) & 3u) == 0u) { func_802F13C0(ctx); return 1; }
    if (address >= 0x802F53C0u && address < 0x802F93C0u && ((address - 0x802F53C0u) & 3u) == 0u) { func_802F53C0(ctx); return 1; }
    if (address >= 0x802F93C0u && address < 0x802FD3C0u && ((address - 0x802F93C0u) & 3u) == 0u) { func_802F93C0(ctx); return 1; }
    if (address >= 0x802FD3C0u && address < 0x803013C0u && ((address - 0x802FD3C0u) & 3u) == 0u) { func_802FD3C0(ctx); return 1; }
    if (address >= 0x803013C0u && address < 0x803053C0u && ((address - 0x803013C0u) & 3u) == 0u) { func_803013C0(ctx); return 1; }
    if (address >= 0x803053C0u && address < 0x803093C0u && ((address - 0x803053C0u) & 3u) == 0u) { func_803053C0(ctx); return 1; }
    if (address >= 0x803093C0u && address < 0x8030D3C0u && ((address - 0x803093C0u) & 3u) == 0u) { func_803093C0(ctx); return 1; }
    if (address >= 0x8030D3C0u && address < 0x803113C0u && ((address - 0x8030D3C0u) & 3u) == 0u) { func_8030D3C0(ctx); return 1; }
    if (address >= 0x803113C0u && address < 0x803153C0u && ((address - 0x803113C0u) & 3u) == 0u) { func_803113C0(ctx); return 1; }
    if (address >= 0x803153C0u && address < 0x803193C0u && ((address - 0x803153C0u) & 3u) == 0u) { func_803153C0(ctx); return 1; }
    if (address >= 0x803193C0u && address < 0x8031D3C0u && ((address - 0x803193C0u) & 3u) == 0u) { func_803193C0(ctx); return 1; }
    if (address >= 0x8031D3C0u && address < 0x803213C0u && ((address - 0x8031D3C0u) & 3u) == 0u) { func_8031D3C0(ctx); return 1; }
    if (address >= 0x803213C0u && address < 0x803253C0u && ((address - 0x803213C0u) & 3u) == 0u) { func_803213C0(ctx); return 1; }
    if (address >= 0x803253C0u && address < 0x803293C0u && ((address - 0x803253C0u) & 3u) == 0u) { func_803253C0(ctx); return 1; }
    if (address >= 0x803293C0u && address < 0x8032D3C0u && ((address - 0x803293C0u) & 3u) == 0u) { func_803293C0(ctx); return 1; }
    if (address >= 0x8032D3C0u && address < 0x803313C0u && ((address - 0x8032D3C0u) & 3u) == 0u) { func_8032D3C0(ctx); return 1; }
    if (address >= 0x803313C0u && address < 0x803353C0u && ((address - 0x803313C0u) & 3u) == 0u) { func_803313C0(ctx); return 1; }
    if (address >= 0x803353C0u && address < 0x803393C0u && ((address - 0x803353C0u) & 3u) == 0u) { func_803353C0(ctx); return 1; }
    if (address >= 0x803393C0u && address < 0x8033D3C0u && ((address - 0x803393C0u) & 3u) == 0u) { func_803393C0(ctx); return 1; }
    if (address >= 0x8033D3C0u && address < 0x803413C0u && ((address - 0x8033D3C0u) & 3u) == 0u) { func_8033D3C0(ctx); return 1; }
    if (address >= 0x803413C0u && address < 0x803453C0u && ((address - 0x803413C0u) & 3u) == 0u) { func_803413C0(ctx); return 1; }
    if (address >= 0x803453C0u && address < 0x803493C0u && ((address - 0x803453C0u) & 3u) == 0u) { func_803453C0(ctx); return 1; }
    if (address >= 0x803493C0u && address < 0x8034D3C0u && ((address - 0x803493C0u) & 3u) == 0u) { func_803493C0(ctx); return 1; }
    if (address >= 0x8034D3C0u && address < 0x803513C0u && ((address - 0x8034D3C0u) & 3u) == 0u) { func_8034D3C0(ctx); return 1; }
    if (address >= 0x803513C0u && address < 0x803553C0u && ((address - 0x803513C0u) & 3u) == 0u) { func_803513C0(ctx); return 1; }
    if (address >= 0x803553C0u && address < 0x803593C0u && ((address - 0x803553C0u) & 3u) == 0u) { func_803553C0(ctx); return 1; }
    if (address >= 0x803593C0u && address < 0x8035D3C0u && ((address - 0x803593C0u) & 3u) == 0u) { func_803593C0(ctx); return 1; }
    if (address >= 0x8035D3C0u && address < 0x803613C0u && ((address - 0x8035D3C0u) & 3u) == 0u) { func_8035D3C0(ctx); return 1; }
    if (address >= 0x803613C0u && address < 0x803653C0u && ((address - 0x803613C0u) & 3u) == 0u) { func_803613C0(ctx); return 1; }
    if (address >= 0x803653C0u && address < 0x803693C0u && ((address - 0x803653C0u) & 3u) == 0u) { func_803653C0(ctx); return 1; }
    if (address >= 0x803693C0u && address < 0x8036D3C0u && ((address - 0x803693C0u) & 3u) == 0u) { func_803693C0(ctx); return 1; }
    if (address >= 0x8036D3C0u && address < 0x803713C0u && ((address - 0x8036D3C0u) & 3u) == 0u) { func_8036D3C0(ctx); return 1; }
    if (address >= 0x803713C0u && address < 0x803753C0u && ((address - 0x803713C0u) & 3u) == 0u) { func_803713C0(ctx); return 1; }
    if (address >= 0x803753C0u && address < 0x803793C0u && ((address - 0x803753C0u) & 3u) == 0u) { func_803753C0(ctx); return 1; }
    if (address >= 0x803793C0u && address < 0x8037D3C0u && ((address - 0x803793C0u) & 3u) == 0u) { func_803793C0(ctx); return 1; }
    if (address >= 0x8037D3C0u && address < 0x803813C0u && ((address - 0x8037D3C0u) & 3u) == 0u) { func_8037D3C0(ctx); return 1; }
    if (address >= 0x803813C0u && address < 0x803853C0u && ((address - 0x803813C0u) & 3u) == 0u) { func_803813C0(ctx); return 1; }
    if (address >= 0x803853C0u && address < 0x803893C0u && ((address - 0x803853C0u) & 3u) == 0u) { func_803853C0(ctx); return 1; }
    if (address >= 0x803893C0u && address < 0x8038D3C0u && ((address - 0x803893C0u) & 3u) == 0u) { func_803893C0(ctx); return 1; }
    if (address >= 0x8038D3C0u && address < 0x803913C0u && ((address - 0x8038D3C0u) & 3u) == 0u) { func_8038D3C0(ctx); return 1; }
    if (address >= 0x803913C0u && address < 0x803953C0u && ((address - 0x803913C0u) & 3u) == 0u) { func_803913C0(ctx); return 1; }
    if (address >= 0x803953C0u && address < 0x803993C0u && ((address - 0x803953C0u) & 3u) == 0u) { func_803953C0(ctx); return 1; }
    if (address >= 0x803993C0u && address < 0x8039D3C0u && ((address - 0x803993C0u) & 3u) == 0u) { func_803993C0(ctx); return 1; }
    if (address >= 0x8039D3C0u && address < 0x803A13C0u && ((address - 0x8039D3C0u) & 3u) == 0u) { func_8039D3C0(ctx); return 1; }
    if (address >= 0x803A13C0u && address < 0x803A53C0u && ((address - 0x803A13C0u) & 3u) == 0u) { func_803A13C0(ctx); return 1; }
    if (address >= 0x803A53C0u && address < 0x803A93C0u && ((address - 0x803A53C0u) & 3u) == 0u) { func_803A53C0(ctx); return 1; }
    if (address >= 0x803A93C0u && address < 0x803AD3C0u && ((address - 0x803A93C0u) & 3u) == 0u) { func_803A93C0(ctx); return 1; }
    if (address >= 0x803AD3C0u && address < 0x803B13C0u && ((address - 0x803AD3C0u) & 3u) == 0u) { func_803AD3C0(ctx); return 1; }
    if (address >= 0x803B13C0u && address < 0x803B53C0u && ((address - 0x803B13C0u) & 3u) == 0u) { func_803B13C0(ctx); return 1; }
    if (address >= 0x803B53C0u && address < 0x803B93C0u && ((address - 0x803B53C0u) & 3u) == 0u) { func_803B53C0(ctx); return 1; }
    if (address >= 0x803B93C0u && address < 0x803BD3C0u && ((address - 0x803B93C0u) & 3u) == 0u) { func_803B93C0(ctx); return 1; }
    if (address >= 0x803BD3C0u && address < 0x803C13C0u && ((address - 0x803BD3C0u) & 3u) == 0u) { func_803BD3C0(ctx); return 1; }
    if (address >= 0x803C13C0u && address < 0x803C53C0u && ((address - 0x803C13C0u) & 3u) == 0u) { func_803C13C0(ctx); return 1; }
    if (address >= 0x803C53C0u && address < 0x803C93C0u && ((address - 0x803C53C0u) & 3u) == 0u) { func_803C53C0(ctx); return 1; }
    if (address >= 0x803C93C0u && address < 0x803CD3C0u && ((address - 0x803C93C0u) & 3u) == 0u) { func_803C93C0(ctx); return 1; }
    if (address >= 0x803CD3C0u && address < 0x803D13C0u && ((address - 0x803CD3C0u) & 3u) == 0u) { func_803CD3C0(ctx); return 1; }
    if (address >= 0x803D13C0u && address < 0x803D53C0u && ((address - 0x803D13C0u) & 3u) == 0u) { func_803D13C0(ctx); return 1; }
    if (address >= 0x803D53C0u && address < 0x803D93C0u && ((address - 0x803D53C0u) & 3u) == 0u) { func_803D53C0(ctx); return 1; }
    if (address >= 0x803D93C0u && address < 0x803DD3C0u && ((address - 0x803D93C0u) & 3u) == 0u) { func_803D93C0(ctx); return 1; }
    if (address >= 0x803DD3C0u && address < 0x803E13C0u && ((address - 0x803DD3C0u) & 3u) == 0u) { func_803DD3C0(ctx); return 1; }
    if (address >= 0x803E13C0u && address < 0x803E53C0u && ((address - 0x803E13C0u) & 3u) == 0u) { func_803E13C0(ctx); return 1; }
    if (address >= 0x803E53C0u && address < 0x803E93C0u && ((address - 0x803E53C0u) & 3u) == 0u) { func_803E53C0(ctx); return 1; }
    if (address >= 0x803E93C0u && address < 0x803ED3C0u && ((address - 0x803E93C0u) & 3u) == 0u) { func_803E93C0(ctx); return 1; }
    if (address >= 0x803ED3C0u && address < 0x803F13C0u && ((address - 0x803ED3C0u) & 3u) == 0u) { func_803ED3C0(ctx); return 1; }
    if (address >= 0x803F13C0u && address < 0x803F53C0u && ((address - 0x803F13C0u) & 3u) == 0u) { func_803F13C0(ctx); return 1; }
    if (address >= 0x803F53C0u && address < 0x803F93C0u && ((address - 0x803F53C0u) & 3u) == 0u) { func_803F53C0(ctx); return 1; }
    if (address >= 0x803F93C0u && address < 0x803FD3C0u && ((address - 0x803F93C0u) & 3u) == 0u) { func_803F93C0(ctx); return 1; }
    if (address >= 0x803FD3C0u && address < 0x804013C0u && ((address - 0x803FD3C0u) & 3u) == 0u) { func_803FD3C0(ctx); return 1; }
    if (address >= 0x804013C0u && address < 0x804053C0u && ((address - 0x804013C0u) & 3u) == 0u) { func_804013C0(ctx); return 1; }
    if (address >= 0x804053C0u && address < 0x804093C0u && ((address - 0x804053C0u) & 3u) == 0u) { func_804053C0(ctx); return 1; }
    if (address >= 0x804093C0u && address < 0x8040D3C0u && ((address - 0x804093C0u) & 3u) == 0u) { func_804093C0(ctx); return 1; }
    if (address >= 0x8040D3C0u && address < 0x804113C0u && ((address - 0x8040D3C0u) & 3u) == 0u) { func_8040D3C0(ctx); return 1; }
    if (address >= 0x804113C0u && address < 0x804153C0u && ((address - 0x804113C0u) & 3u) == 0u) { func_804113C0(ctx); return 1; }
    if (address >= 0x804153C0u && address < 0x804193C0u && ((address - 0x804153C0u) & 3u) == 0u) { func_804153C0(ctx); return 1; }
    if (address >= 0x804193C0u && address < 0x8041D3C0u && ((address - 0x804193C0u) & 3u) == 0u) { func_804193C0(ctx); return 1; }
    if (address >= 0x8041D3C0u && address < 0x804213C0u && ((address - 0x8041D3C0u) & 3u) == 0u) { func_8041D3C0(ctx); return 1; }
    if (address >= 0x804213C0u && address < 0x804253C0u && ((address - 0x804213C0u) & 3u) == 0u) { func_804213C0(ctx); return 1; }
    if (address >= 0x804253C0u && address < 0x804293C0u && ((address - 0x804253C0u) & 3u) == 0u) { func_804253C0(ctx); return 1; }
    if (address >= 0x804293C0u && address < 0x8042D3C0u && ((address - 0x804293C0u) & 3u) == 0u) { func_804293C0(ctx); return 1; }
    if (address >= 0x8042D3C0u && address < 0x804313C0u && ((address - 0x8042D3C0u) & 3u) == 0u) { func_8042D3C0(ctx); return 1; }
    if (address >= 0x804313C0u && address < 0x804353C0u && ((address - 0x804313C0u) & 3u) == 0u) { func_804313C0(ctx); return 1; }
    if (address >= 0x804353C0u && address < 0x804393C0u && ((address - 0x804353C0u) & 3u) == 0u) { func_804353C0(ctx); return 1; }
    if (address >= 0x804393C0u && address < 0x8043D3C0u && ((address - 0x804393C0u) & 3u) == 0u) { func_804393C0(ctx); return 1; }
    if (address >= 0x8043D3C0u && address < 0x804413C0u && ((address - 0x8043D3C0u) & 3u) == 0u) { func_8043D3C0(ctx); return 1; }
    if (address >= 0x804413C0u && address < 0x804453C0u && ((address - 0x804413C0u) & 3u) == 0u) { func_804413C0(ctx); return 1; }
    if (address >= 0x804453C0u && address < 0x804493C0u && ((address - 0x804453C0u) & 3u) == 0u) { func_804453C0(ctx); return 1; }
    if (address >= 0x804493C0u && address < 0x8044D3C0u && ((address - 0x804493C0u) & 3u) == 0u) { func_804493C0(ctx); return 1; }
    if (address >= 0x8044D3C0u && address < 0x804513C0u && ((address - 0x8044D3C0u) & 3u) == 0u) { func_8044D3C0(ctx); return 1; }
    if (address >= 0x804513C0u && address < 0x804553C0u && ((address - 0x804513C0u) & 3u) == 0u) { func_804513C0(ctx); return 1; }
    if (address >= 0x804553C0u && address < 0x804593C0u && ((address - 0x804553C0u) & 3u) == 0u) { func_804553C0(ctx); return 1; }
    if (address >= 0x804593C0u && address < 0x8045D3C0u && ((address - 0x804593C0u) & 3u) == 0u) { func_804593C0(ctx); return 1; }
    if (address >= 0x8045D3C0u && address < 0x804613C0u && ((address - 0x8045D3C0u) & 3u) == 0u) { func_8045D3C0(ctx); return 1; }
    if (address >= 0x804613C0u && address < 0x804653C0u && ((address - 0x804613C0u) & 3u) == 0u) { func_804613C0(ctx); return 1; }
    if (address >= 0x804653C0u && address < 0x804693C0u && ((address - 0x804653C0u) & 3u) == 0u) { func_804653C0(ctx); return 1; }
    if (address >= 0x804693C0u && address < 0x8046D3C0u && ((address - 0x804693C0u) & 3u) == 0u) { func_804693C0(ctx); return 1; }
    if (address >= 0x8046D3C0u && address < 0x804713C0u && ((address - 0x8046D3C0u) & 3u) == 0u) { func_8046D3C0(ctx); return 1; }
    if (address >= 0x804713C0u && address < 0x804753C0u && ((address - 0x804713C0u) & 3u) == 0u) { func_804713C0(ctx); return 1; }
    if (address >= 0x804753C0u && address < 0x804793C0u && ((address - 0x804753C0u) & 3u) == 0u) { func_804753C0(ctx); return 1; }
    if (address >= 0x804793C0u && address < 0x8047D3C0u && ((address - 0x804793C0u) & 3u) == 0u) { func_804793C0(ctx); return 1; }
    if (address >= 0x8047D3C0u && address < 0x804813C0u && ((address - 0x8047D3C0u) & 3u) == 0u) { func_8047D3C0(ctx); return 1; }
    if (address >= 0x804813C0u && address < 0x804853C0u && ((address - 0x804813C0u) & 3u) == 0u) { func_804813C0(ctx); return 1; }
    if (address >= 0x804853C0u && address < 0x804893C0u && ((address - 0x804853C0u) & 3u) == 0u) { func_804853C0(ctx); return 1; }
    if (address >= 0x804893C0u && address < 0x8048D3C0u && ((address - 0x804893C0u) & 3u) == 0u) { func_804893C0(ctx); return 1; }
    if (address >= 0x8048D3C0u && address < 0x804913C0u && ((address - 0x8048D3C0u) & 3u) == 0u) { func_8048D3C0(ctx); return 1; }
    if (address >= 0x804913C0u && address < 0x804953C0u && ((address - 0x804913C0u) & 3u) == 0u) { func_804913C0(ctx); return 1; }
    if (address >= 0x804953C0u && address < 0x804993C0u && ((address - 0x804953C0u) & 3u) == 0u) { func_804953C0(ctx); return 1; }
    if (address >= 0x804993C0u && address < 0x8049D3C0u && ((address - 0x804993C0u) & 3u) == 0u) { func_804993C0(ctx); return 1; }
    if (address >= 0x8049D3C0u && address < 0x804A13C0u && ((address - 0x8049D3C0u) & 3u) == 0u) { func_8049D3C0(ctx); return 1; }
    if (address >= 0x804A13C0u && address < 0x804A53C0u && ((address - 0x804A13C0u) & 3u) == 0u) { func_804A13C0(ctx); return 1; }
    if (address >= 0x804A53C0u && address < 0x804A93C0u && ((address - 0x804A53C0u) & 3u) == 0u) { func_804A53C0(ctx); return 1; }
    if (address >= 0x804A93C0u && address < 0x804AD3C0u && ((address - 0x804A93C0u) & 3u) == 0u) { func_804A93C0(ctx); return 1; }
    if (address >= 0x804AD3C0u && address < 0x804B13C0u && ((address - 0x804AD3C0u) & 3u) == 0u) { func_804AD3C0(ctx); return 1; }
    if (address >= 0x804B13C0u && address < 0x804B53C0u && ((address - 0x804B13C0u) & 3u) == 0u) { func_804B13C0(ctx); return 1; }
    if (address >= 0x804B53C0u && address < 0x804B93C0u && ((address - 0x804B53C0u) & 3u) == 0u) { func_804B53C0(ctx); return 1; }
    if (address >= 0x804B93C0u && address < 0x804BD3C0u && ((address - 0x804B93C0u) & 3u) == 0u) { func_804B93C0(ctx); return 1; }
    if (address >= 0x804BD3C0u && address < 0x804C13C0u && ((address - 0x804BD3C0u) & 3u) == 0u) { func_804BD3C0(ctx); return 1; }
    if (address >= 0x804C13C0u && address < 0x804C53C0u && ((address - 0x804C13C0u) & 3u) == 0u) { func_804C13C0(ctx); return 1; }
    if (address >= 0x804C53C0u && address < 0x804C93C0u && ((address - 0x804C53C0u) & 3u) == 0u) { func_804C53C0(ctx); return 1; }
    if (address >= 0x804C93C0u && address < 0x804CD3C0u && ((address - 0x804C93C0u) & 3u) == 0u) { func_804C93C0(ctx); return 1; }
    if (address >= 0x804CD3C0u && address < 0x804D13C0u && ((address - 0x804CD3C0u) & 3u) == 0u) { func_804CD3C0(ctx); return 1; }
    if (address >= 0x804D13C0u && address < 0x804D53C0u && ((address - 0x804D13C0u) & 3u) == 0u) { func_804D13C0(ctx); return 1; }
    if (address >= 0x804D53C0u && address < 0x804D93C0u && ((address - 0x804D53C0u) & 3u) == 0u) { func_804D53C0(ctx); return 1; }
    if (address >= 0x804D93C0u && address < 0x804DD3C0u && ((address - 0x804D93C0u) & 3u) == 0u) { func_804D93C0(ctx); return 1; }
    if (address >= 0x804DD3C0u && address < 0x804E13C0u && ((address - 0x804DD3C0u) & 3u) == 0u) { func_804DD3C0(ctx); return 1; }
    if (address >= 0x804E13C0u && address < 0x804E53C0u && ((address - 0x804E13C0u) & 3u) == 0u) { func_804E13C0(ctx); return 1; }
    if (address >= 0x804E53C0u && address < 0x804E93C0u && ((address - 0x804E53C0u) & 3u) == 0u) { func_804E53C0(ctx); return 1; }
    if (address >= 0x804E93C0u && address < 0x804ED3C0u && ((address - 0x804E93C0u) & 3u) == 0u) { func_804E93C0(ctx); return 1; }
    if (address >= 0x804ED3C0u && address < 0x804F13C0u && ((address - 0x804ED3C0u) & 3u) == 0u) { func_804ED3C0(ctx); return 1; }
    if (address >= 0x804F13C0u && address < 0x804F53C0u && ((address - 0x804F13C0u) & 3u) == 0u) { func_804F13C0(ctx); return 1; }
    if (address >= 0x804F53C0u && address < 0x804F93C0u && ((address - 0x804F53C0u) & 3u) == 0u) { func_804F53C0(ctx); return 1; }
    if (address >= 0x804F93C0u && address < 0x804FD3C0u && ((address - 0x804F93C0u) & 3u) == 0u) { func_804F93C0(ctx); return 1; }
    if (address >= 0x804FD3C0u && address < 0x805013C0u && ((address - 0x804FD3C0u) & 3u) == 0u) { func_804FD3C0(ctx); return 1; }
    if (address >= 0x805013C0u && address < 0x805053C0u && ((address - 0x805013C0u) & 3u) == 0u) { func_805013C0(ctx); return 1; }
    if (address >= 0x805053C0u && address < 0x805093C0u && ((address - 0x805053C0u) & 3u) == 0u) { func_805053C0(ctx); return 1; }
    if (address >= 0x805093C0u && address < 0x8050D3C0u && ((address - 0x805093C0u) & 3u) == 0u) { func_805093C0(ctx); return 1; }
    if (address >= 0x8050D3C0u && address < 0x805113C0u && ((address - 0x8050D3C0u) & 3u) == 0u) { func_8050D3C0(ctx); return 1; }
    if (address >= 0x805113C0u && address < 0x805153C0u && ((address - 0x805113C0u) & 3u) == 0u) { func_805113C0(ctx); return 1; }
    if (address >= 0x805153C0u && address < 0x805193C0u && ((address - 0x805153C0u) & 3u) == 0u) { func_805153C0(ctx); return 1; }
    if (address >= 0x805193C0u && address < 0x8051D3C0u && ((address - 0x805193C0u) & 3u) == 0u) { func_805193C0(ctx); return 1; }
    if (address >= 0x8051D3C0u && address < 0x805213C0u && ((address - 0x8051D3C0u) & 3u) == 0u) { func_8051D3C0(ctx); return 1; }
    if (address >= 0x805213C0u && address < 0x805253C0u && ((address - 0x805213C0u) & 3u) == 0u) { func_805213C0(ctx); return 1; }
    if (address >= 0x805253C0u && address < 0x805293C0u && ((address - 0x805253C0u) & 3u) == 0u) { func_805253C0(ctx); return 1; }
    if (address >= 0x805293C0u && address < 0x8052D3C0u && ((address - 0x805293C0u) & 3u) == 0u) { func_805293C0(ctx); return 1; }
    if (address >= 0x8052D3C0u && address < 0x805313C0u && ((address - 0x8052D3C0u) & 3u) == 0u) { func_8052D3C0(ctx); return 1; }
    if (address >= 0x805313C0u && address < 0x805353C0u && ((address - 0x805313C0u) & 3u) == 0u) { func_805313C0(ctx); return 1; }
    if (address >= 0x805353C0u && address < 0x805393C0u && ((address - 0x805353C0u) & 3u) == 0u) { func_805353C0(ctx); return 1; }
    if (address >= 0x805393C0u && address < 0x8053D3C0u && ((address - 0x805393C0u) & 3u) == 0u) { func_805393C0(ctx); return 1; }
    if (address >= 0x8053D3C0u && address < 0x805413C0u && ((address - 0x8053D3C0u) & 3u) == 0u) { func_8053D3C0(ctx); return 1; }
    if (address >= 0x805413C0u && address < 0x805453C0u && ((address - 0x805413C0u) & 3u) == 0u) { func_805413C0(ctx); return 1; }
    if (address >= 0x805453C0u && address < 0x805493C0u && ((address - 0x805453C0u) & 3u) == 0u) { func_805453C0(ctx); return 1; }
    if (address >= 0x805493C0u && address < 0x8054D3C0u && ((address - 0x805493C0u) & 3u) == 0u) { func_805493C0(ctx); return 1; }
    if (address >= 0x8054D3C0u && address < 0x805513C0u && ((address - 0x8054D3C0u) & 3u) == 0u) { func_8054D3C0(ctx); return 1; }
    if (address >= 0x805513C0u && address < 0x805553C0u && ((address - 0x805513C0u) & 3u) == 0u) { func_805513C0(ctx); return 1; }
    if (address >= 0x805553C0u && address < 0x805593C0u && ((address - 0x805553C0u) & 3u) == 0u) { func_805553C0(ctx); return 1; }
    if (address >= 0x805593C0u && address < 0x8055D3C0u && ((address - 0x805593C0u) & 3u) == 0u) { func_805593C0(ctx); return 1; }
    if (address >= 0x8055D3C0u && address < 0x805613C0u && ((address - 0x8055D3C0u) & 3u) == 0u) { func_8055D3C0(ctx); return 1; }
    if (address >= 0x805613C0u && address < 0x805653C0u && ((address - 0x805613C0u) & 3u) == 0u) { func_805613C0(ctx); return 1; }
    if (address >= 0x805653C0u && address < 0x805693C0u && ((address - 0x805653C0u) & 3u) == 0u) { func_805653C0(ctx); return 1; }
    if (address >= 0x805693C0u && address < 0x8056D3C0u && ((address - 0x805693C0u) & 3u) == 0u) { func_805693C0(ctx); return 1; }
    if (address >= 0x8056D3C0u && address < 0x805713C0u && ((address - 0x8056D3C0u) & 3u) == 0u) { func_8056D3C0(ctx); return 1; }
    if (address >= 0x805713C0u && address < 0x805753C0u && ((address - 0x805713C0u) & 3u) == 0u) { func_805713C0(ctx); return 1; }
    if (address >= 0x805753C0u && address < 0x805793C0u && ((address - 0x805753C0u) & 3u) == 0u) { func_805753C0(ctx); return 1; }
    if (address >= 0x805793C0u && address < 0x8057D3C0u && ((address - 0x805793C0u) & 3u) == 0u) { func_805793C0(ctx); return 1; }
    if (address >= 0x8057D3C0u && address < 0x805813C0u && ((address - 0x8057D3C0u) & 3u) == 0u) { func_8057D3C0(ctx); return 1; }
    if (address >= 0x805813C0u && address < 0x805853C0u && ((address - 0x805813C0u) & 3u) == 0u) { func_805813C0(ctx); return 1; }
    if (address >= 0x805853C0u && address < 0x805893C0u && ((address - 0x805853C0u) & 3u) == 0u) { func_805853C0(ctx); return 1; }
    if (address >= 0x805893C0u && address < 0x8058D3C0u && ((address - 0x805893C0u) & 3u) == 0u) { func_805893C0(ctx); return 1; }
    if (address >= 0x8058D3C0u && address < 0x805913C0u && ((address - 0x8058D3C0u) & 3u) == 0u) { func_8058D3C0(ctx); return 1; }
    if (address >= 0x805913C0u && address < 0x805953C0u && ((address - 0x805913C0u) & 3u) == 0u) { func_805913C0(ctx); return 1; }
    if (address >= 0x805953C0u && address < 0x805993C0u && ((address - 0x805953C0u) & 3u) == 0u) { func_805953C0(ctx); return 1; }
    if (address >= 0x805993C0u && address < 0x8059D3C0u && ((address - 0x805993C0u) & 3u) == 0u) { func_805993C0(ctx); return 1; }
    if (address >= 0x8059D3C0u && address < 0x805A13C0u && ((address - 0x8059D3C0u) & 3u) == 0u) { func_8059D3C0(ctx); return 1; }
    if (address >= 0x805A13C0u && address < 0x805A53C0u && ((address - 0x805A13C0u) & 3u) == 0u) { func_805A13C0(ctx); return 1; }
    if (address >= 0x805A53C0u && address < 0x805A93C0u && ((address - 0x805A53C0u) & 3u) == 0u) { func_805A53C0(ctx); return 1; }
    if (address >= 0x805A93C0u && address < 0x805AD3C0u && ((address - 0x805A93C0u) & 3u) == 0u) { func_805A93C0(ctx); return 1; }
    if (address >= 0x805AD3C0u && address < 0x805B13C0u && ((address - 0x805AD3C0u) & 3u) == 0u) { func_805AD3C0(ctx); return 1; }
    if (address >= 0x805B13C0u && address < 0x805B53C0u && ((address - 0x805B13C0u) & 3u) == 0u) { func_805B13C0(ctx); return 1; }
    if (address >= 0x805B53C0u && address < 0x805B93C0u && ((address - 0x805B53C0u) & 3u) == 0u) { func_805B53C0(ctx); return 1; }
    if (address >= 0x805B93C0u && address < 0x805BD3C0u && ((address - 0x805B93C0u) & 3u) == 0u) { func_805B93C0(ctx); return 1; }
    if (address >= 0x805BD3C0u && address < 0x805C13C0u && ((address - 0x805BD3C0u) & 3u) == 0u) { func_805BD3C0(ctx); return 1; }
    if (address >= 0x805C13C0u && address < 0x805C53C0u && ((address - 0x805C13C0u) & 3u) == 0u) { func_805C13C0(ctx); return 1; }
    if (address >= 0x805C53C0u && address < 0x805C93C0u && ((address - 0x805C53C0u) & 3u) == 0u) { func_805C53C0(ctx); return 1; }
    if (address >= 0x805C93C0u && address < 0x805CD3C0u && ((address - 0x805C93C0u) & 3u) == 0u) { func_805C93C0(ctx); return 1; }
    if (address >= 0x805CD3C0u && address < 0x805D13C0u && ((address - 0x805CD3C0u) & 3u) == 0u) { func_805CD3C0(ctx); return 1; }
    if (address >= 0x805D13C0u && address < 0x805D53C0u && ((address - 0x805D13C0u) & 3u) == 0u) { func_805D13C0(ctx); return 1; }
    if (address >= 0x805D53C0u && address < 0x805D93C0u && ((address - 0x805D53C0u) & 3u) == 0u) { func_805D53C0(ctx); return 1; }
    if (address >= 0x805D93C0u && address < 0x805DD3C0u && ((address - 0x805D93C0u) & 3u) == 0u) { func_805D93C0(ctx); return 1; }
    if (address >= 0x805DD3C0u && address < 0x805E13C0u && ((address - 0x805DD3C0u) & 3u) == 0u) { func_805DD3C0(ctx); return 1; }
    if (address >= 0x805E13C0u && address < 0x805E53C0u && ((address - 0x805E13C0u) & 3u) == 0u) { func_805E13C0(ctx); return 1; }
    if (address >= 0x805E53C0u && address < 0x805E93C0u && ((address - 0x805E53C0u) & 3u) == 0u) { func_805E53C0(ctx); return 1; }
    if (address >= 0x805E93C0u && address < 0x805ED3C0u && ((address - 0x805E93C0u) & 3u) == 0u) { func_805E93C0(ctx); return 1; }
    if (address >= 0x805ED3C0u && address < 0x805F13C0u && ((address - 0x805ED3C0u) & 3u) == 0u) { func_805ED3C0(ctx); return 1; }
    if (address >= 0x805F13C0u && address < 0x805F53C0u && ((address - 0x805F13C0u) & 3u) == 0u) { func_805F13C0(ctx); return 1; }
    if (address >= 0x805F53C0u && address < 0x805F93C0u && ((address - 0x805F53C0u) & 3u) == 0u) { func_805F53C0(ctx); return 1; }
    if (address >= 0x805F93C0u && address < 0x805FD3C0u && ((address - 0x805F93C0u) & 3u) == 0u) { func_805F93C0(ctx); return 1; }
    if (address >= 0x805FD3C0u && address < 0x806013C0u && ((address - 0x805FD3C0u) & 3u) == 0u) { func_805FD3C0(ctx); return 1; }
    if (address >= 0x806013C0u && address < 0x806053C0u && ((address - 0x806013C0u) & 3u) == 0u) { func_806013C0(ctx); return 1; }
    if (address >= 0x806053C0u && address < 0x806093C0u && ((address - 0x806053C0u) & 3u) == 0u) { func_806053C0(ctx); return 1; }
    if (address >= 0x806093C0u && address < 0x8060D3C0u && ((address - 0x806093C0u) & 3u) == 0u) { func_806093C0(ctx); return 1; }
    if (address >= 0x8060D3C0u && address < 0x806113C0u && ((address - 0x8060D3C0u) & 3u) == 0u) { func_8060D3C0(ctx); return 1; }
    if (address >= 0x806113C0u && address < 0x806153C0u && ((address - 0x806113C0u) & 3u) == 0u) { func_806113C0(ctx); return 1; }
    if (address >= 0x806153C0u && address < 0x806193C0u && ((address - 0x806153C0u) & 3u) == 0u) { func_806153C0(ctx); return 1; }
    if (address >= 0x806193C0u && address < 0x8061D3C0u && ((address - 0x806193C0u) & 3u) == 0u) { func_806193C0(ctx); return 1; }
    if (address >= 0x8061D3C0u && address < 0x806213C0u && ((address - 0x8061D3C0u) & 3u) == 0u) { func_8061D3C0(ctx); return 1; }
    if (address >= 0x806213C0u && address < 0x806253C0u && ((address - 0x806213C0u) & 3u) == 0u) { func_806213C0(ctx); return 1; }
    if (address >= 0x806253C0u && address < 0x806293C0u && ((address - 0x806253C0u) & 3u) == 0u) { func_806253C0(ctx); return 1; }
    if (address >= 0x806293C0u && address < 0x8062D3C0u && ((address - 0x806293C0u) & 3u) == 0u) { func_806293C0(ctx); return 1; }
    if (address >= 0x8062D3C0u && address < 0x806313C0u && ((address - 0x8062D3C0u) & 3u) == 0u) { func_8062D3C0(ctx); return 1; }
    if (address >= 0x806313C0u && address < 0x806353C0u && ((address - 0x806313C0u) & 3u) == 0u) { func_806313C0(ctx); return 1; }
    if (address >= 0x806353C0u && address < 0x806393C0u && ((address - 0x806353C0u) & 3u) == 0u) { func_806353C0(ctx); return 1; }
    if (address >= 0x806393C0u && address < 0x8063D3C0u && ((address - 0x806393C0u) & 3u) == 0u) { func_806393C0(ctx); return 1; }
    if (address >= 0x8063D3C0u && address < 0x806413C0u && ((address - 0x8063D3C0u) & 3u) == 0u) { func_8063D3C0(ctx); return 1; }
    if (address >= 0x806413C0u && address < 0x806453C0u && ((address - 0x806413C0u) & 3u) == 0u) { func_806413C0(ctx); return 1; }
    if (address >= 0x806453C0u && address < 0x806493C0u && ((address - 0x806453C0u) & 3u) == 0u) { func_806453C0(ctx); return 1; }
    if (address >= 0x806493C0u && address < 0x8064D3C0u && ((address - 0x806493C0u) & 3u) == 0u) { func_806493C0(ctx); return 1; }
    if (address >= 0x8064D3C0u && address < 0x806513C0u && ((address - 0x8064D3C0u) & 3u) == 0u) { func_8064D3C0(ctx); return 1; }
    if (address >= 0x806513C0u && address < 0x806553C0u && ((address - 0x806513C0u) & 3u) == 0u) { func_806513C0(ctx); return 1; }
    if (address >= 0x806553C0u && address < 0x806593C0u && ((address - 0x806553C0u) & 3u) == 0u) { func_806553C0(ctx); return 1; }
    if (address >= 0x806593C0u && address < 0x8065D3C0u && ((address - 0x806593C0u) & 3u) == 0u) { func_806593C0(ctx); return 1; }
    if (address >= 0x8065D3C0u && address < 0x806613C0u && ((address - 0x8065D3C0u) & 3u) == 0u) { func_8065D3C0(ctx); return 1; }
    if (address >= 0x806613C0u && address < 0x806653C0u && ((address - 0x806613C0u) & 3u) == 0u) { func_806613C0(ctx); return 1; }
    if (address >= 0x806653C0u && address < 0x806693C0u && ((address - 0x806653C0u) & 3u) == 0u) { func_806653C0(ctx); return 1; }
    if (address >= 0x806693C0u && address < 0x8066D3C0u && ((address - 0x806693C0u) & 3u) == 0u) { func_806693C0(ctx); return 1; }
    if (address >= 0x8066D3C0u && address < 0x806713C0u && ((address - 0x8066D3C0u) & 3u) == 0u) { func_8066D3C0(ctx); return 1; }
    if (address >= 0x806713C0u && address < 0x806753C0u && ((address - 0x806713C0u) & 3u) == 0u) { func_806713C0(ctx); return 1; }
    if (address >= 0x806753C0u && address < 0x806793C0u && ((address - 0x806753C0u) & 3u) == 0u) { func_806753C0(ctx); return 1; }
    if (address >= 0x806793C0u && address < 0x8067D3C0u && ((address - 0x806793C0u) & 3u) == 0u) { func_806793C0(ctx); return 1; }
    if (address >= 0x8067D3C0u && address < 0x806813C0u && ((address - 0x8067D3C0u) & 3u) == 0u) { func_8067D3C0(ctx); return 1; }
    if (address >= 0x806813C0u && address < 0x806853C0u && ((address - 0x806813C0u) & 3u) == 0u) { func_806813C0(ctx); return 1; }
    if (address >= 0x806853C0u && address < 0x806893C0u && ((address - 0x806853C0u) & 3u) == 0u) { func_806853C0(ctx); return 1; }
    if (address >= 0x806893C0u && address < 0x8068D3C0u && ((address - 0x806893C0u) & 3u) == 0u) { func_806893C0(ctx); return 1; }
    if (address >= 0x8068D3C0u && address < 0x806913C0u && ((address - 0x8068D3C0u) & 3u) == 0u) { func_8068D3C0(ctx); return 1; }
    if (address >= 0x806913C0u && address < 0x806953C0u && ((address - 0x806913C0u) & 3u) == 0u) { func_806913C0(ctx); return 1; }
    if (address >= 0x806953C0u && address < 0x806993C0u && ((address - 0x806953C0u) & 3u) == 0u) { func_806953C0(ctx); return 1; }
    if (address >= 0x806993C0u && address < 0x8069D3C0u && ((address - 0x806993C0u) & 3u) == 0u) { func_806993C0(ctx); return 1; }
    if (address >= 0x8069D3C0u && address < 0x806A13C0u && ((address - 0x8069D3C0u) & 3u) == 0u) { func_8069D3C0(ctx); return 1; }
    if (address >= 0x806A13C0u && address < 0x806A53C0u && ((address - 0x806A13C0u) & 3u) == 0u) { func_806A13C0(ctx); return 1; }
    if (address >= 0x806A53C0u && address < 0x806A93C0u && ((address - 0x806A53C0u) & 3u) == 0u) { func_806A53C0(ctx); return 1; }
    if (address >= 0x806A93C0u && address < 0x806AD3C0u && ((address - 0x806A93C0u) & 3u) == 0u) { func_806A93C0(ctx); return 1; }
    if (address >= 0x806AD3C0u && address < 0x806B13C0u && ((address - 0x806AD3C0u) & 3u) == 0u) { func_806AD3C0(ctx); return 1; }
    if (address >= 0x806B13C0u && address < 0x806B53C0u && ((address - 0x806B13C0u) & 3u) == 0u) { func_806B13C0(ctx); return 1; }
    if (address >= 0x806B53C0u && address < 0x806B93C0u && ((address - 0x806B53C0u) & 3u) == 0u) { func_806B53C0(ctx); return 1; }
    if (address >= 0x806B93C0u && address < 0x806BD3C0u && ((address - 0x806B93C0u) & 3u) == 0u) { func_806B93C0(ctx); return 1; }
    if (address >= 0x806BD3C0u && address < 0x806C13C0u && ((address - 0x806BD3C0u) & 3u) == 0u) { func_806BD3C0(ctx); return 1; }
    if (address >= 0x806C13C0u && address < 0x806C53C0u && ((address - 0x806C13C0u) & 3u) == 0u) { func_806C13C0(ctx); return 1; }
    if (address >= 0x806C53C0u && address < 0x806C93C0u && ((address - 0x806C53C0u) & 3u) == 0u) { func_806C53C0(ctx); return 1; }
    if (address >= 0x806C93C0u && address < 0x806CD3C0u && ((address - 0x806C93C0u) & 3u) == 0u) { func_806C93C0(ctx); return 1; }
    if (address >= 0x806CD3C0u && address < 0x806D13C0u && ((address - 0x806CD3C0u) & 3u) == 0u) { func_806CD3C0(ctx); return 1; }
    if (address >= 0x806D13C0u && address < 0x806D53C0u && ((address - 0x806D13C0u) & 3u) == 0u) { func_806D13C0(ctx); return 1; }
    if (address >= 0x806D53C0u && address < 0x806D93C0u && ((address - 0x806D53C0u) & 3u) == 0u) { func_806D53C0(ctx); return 1; }
    if (address >= 0x806D93C0u && address < 0x806DD3C0u && ((address - 0x806D93C0u) & 3u) == 0u) { func_806D93C0(ctx); return 1; }
    if (address >= 0x806DD3C0u && address < 0x806E13C0u && ((address - 0x806DD3C0u) & 3u) == 0u) { func_806DD3C0(ctx); return 1; }
    if (address >= 0x806E13C0u && address < 0x806E53C0u && ((address - 0x806E13C0u) & 3u) == 0u) { func_806E13C0(ctx); return 1; }
    if (address >= 0x806E53C0u && address < 0x806E93C0u && ((address - 0x806E53C0u) & 3u) == 0u) { func_806E53C0(ctx); return 1; }
    if (address >= 0x806E93C0u && address < 0x806ED3C0u && ((address - 0x806E93C0u) & 3u) == 0u) { func_806E93C0(ctx); return 1; }
    if (address >= 0x806ED3C0u && address < 0x806F13C0u && ((address - 0x806ED3C0u) & 3u) == 0u) { func_806ED3C0(ctx); return 1; }
    if (address >= 0x806F13C0u && address < 0x806F53C0u && ((address - 0x806F13C0u) & 3u) == 0u) { func_806F13C0(ctx); return 1; }
    if (address >= 0x806F53C0u && address < 0x806F93C0u && ((address - 0x806F53C0u) & 3u) == 0u) { func_806F53C0(ctx); return 1; }
    if (address >= 0x806F93C0u && address < 0x806FD3C0u && ((address - 0x806F93C0u) & 3u) == 0u) { func_806F93C0(ctx); return 1; }
    if (address >= 0x806FD3C0u && address < 0x807013C0u && ((address - 0x806FD3C0u) & 3u) == 0u) { func_806FD3C0(ctx); return 1; }
    if (address >= 0x807013C0u && address < 0x807053C0u && ((address - 0x807013C0u) & 3u) == 0u) { func_807013C0(ctx); return 1; }
    if (address >= 0x807053C0u && address < 0x807093C0u && ((address - 0x807053C0u) & 3u) == 0u) { func_807053C0(ctx); return 1; }
    if (address >= 0x807093C0u && address < 0x8070D3C0u && ((address - 0x807093C0u) & 3u) == 0u) { func_807093C0(ctx); return 1; }
    if (address >= 0x8070D3C0u && address < 0x807113C0u && ((address - 0x8070D3C0u) & 3u) == 0u) { func_8070D3C0(ctx); return 1; }
    if (address >= 0x807113C0u && address < 0x80712600u && ((address - 0x807113C0u) & 3u) == 0u) { func_807113C0(ctx); return 1; }
    return 0;
}

static inline int dolrecomp_run_blocks(CPUState* ctx, u32 max_blocks) {
    u32 blocks = 0;
    while (max_blocks == 0u || blocks < max_blocks) {
        if (!dolrecomp_call(ctx, ctx->pc)) return 0;
        if (ctx->exception) return 0;
        blocks++;
    }
    return 1;
}

// end
