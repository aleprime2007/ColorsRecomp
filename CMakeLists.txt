cmake_minimum_required(VERSION 3.16)
# CXX is needed because the GXRuntime GX frontend is C++ (C API on top) and
# texdecode.cpp wraps gxcore's texture decoder.
project(ColorsRecomp C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# The generated chunk files are large switch-heavy TUs: each chunk dispatches
# with a ~4096-case switch over the PC. At -O0 GCC emits a linear compare chain
# (the dominant per-block cost); at -O2 it emits a jump table, which is several
# times faster. Optimize the recompiled code accordingly and disable strict
# aliasing (the emitter reinterprets guest memory through several pointer
# types). Override with -DCMAKE_BUILD_TYPE=... as needed.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

add_compile_options(-O2 -fno-strict-aliasing)

# ---------------------------------------------------------------------------
# Runtime: GXRuntime (CPU, guest memory, devices, retail GX frontend + gxcore).
# Aurora itself stays off; the host renders GX packets with its own software
# rasteriser.
# ---------------------------------------------------------------------------
set(GXRUNTIME_ENABLE_AURORA OFF CACHE BOOL "" FORCE)
# The "recomp" frontend/gxcore modules are Aurora-independent (pure C++ FIFO
# decode + draw packet emission), so the host can consume real GX commands
# headlessly. The Aurora renderer itself stays disabled (it needs Dawn/SDL3).
set(GXRUNTIME_ENABLE_AURORA_RECOMP ON CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(tools/GXRuntime GXRuntime)

# ---------------------------------------------------------------------------
# Recompiled sources produced by DolRecomp (recompiled/generated/).
# ---------------------------------------------------------------------------
set(COLORS_GENERATED_DIR "${CMAKE_SOURCE_DIR}/recompiled/generated")
if(NOT EXISTS "${COLORS_GENERATED_DIR}/generated.h")
    message(FATAL_ERROR "Recompiled output not found at ${COLORS_GENERATED_DIR}")
endif()

file(GLOB COLORS_CHUNKS CONFIGURE_DEPENDS "${COLORS_GENERATED_DIR}/chunks/*.c")
list(LENGTH COLORS_CHUNKS COLORS_CHUNK_COUNT)
message(STATUS "ColorsRecomp: ${COLORS_CHUNK_COUNT} recompiled chunk(s)")

# GXRuntime declares ppc_psq_load/ppc_psq_store as returning void (DolRecomp
# emits `if (!ppc_psq_...(...)) return;`). The generated tree is adapted by
# tools/fix_generated_gxruntime.py; catch an unpatched regeneration here so the
# failure is actionable instead of "invalid use of void expression".
file(GLOB COLORS_PSQ_PROBE "${COLORS_GENERATED_DIR}/chunks/chunk_0001_*.c")
if(COLORS_PSQ_PROBE)
    list(GET COLORS_PSQ_PROBE 0 COLORS_PSQ_PROBE_FILE)
    file(READ "${COLORS_PSQ_PROBE_FILE}" COLORS_PSQ_PROBE_TEXT)
    string(FIND "${COLORS_PSQ_PROBE_TEXT}" "if (!ppc_psq_" COLORS_PSQ_UNPATCHED)
    if(NOT COLORS_PSQ_UNPATCHED EQUAL -1)
        message(FATAL_ERROR
            "Recompiled output is not adapted to the GXRuntime generated-code ABI.\n"
            "Run: python3 tools/fix_generated_gxruntime.py")
    endif()
endif()

# Direct-mapped chunk dispatch (generated from generated.h). Regenerated when
# the recompiled header changes; the committed copy is used when Python is
# unavailable.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_custom_command(
        OUTPUT "${CMAKE_SOURCE_DIR}/src/chunk_table.c"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/gen_chunk_table.py"
                "${COLORS_GENERATED_DIR}/generated.h" "${CMAKE_SOURCE_DIR}/src/chunk_table.c"
        DEPENDS "${COLORS_GENERATED_DIR}/generated.h"
        COMMENT "Generating chunk dispatch table"
        VERBATIM)
endif()

# The recompiled chunks are DolRecomp output, so they call the instruction-shaped
# PPC helpers that GXRuntime's own <core/cpu.h> does not declare. src/compat/
# supplies them (see src/compat/ppc_abi.c) without touching GXRuntime.

# Offline GX FIFO decode probe (diagnostics; not part of the game binary).
add_executable(gxfifo_probe tools/gxfifo_probe.cpp)
target_link_libraries(gxfifo_probe PRIVATE GXRuntime::retail_gx_frontend)

# Optional SDL2 window for the software rasteriser's EFB. Without it the
# emulator stays headless (PPM dumps only).
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(SDL2 QUIET sdl2)
endif()

add_executable(ColorsRecomp
    src/main.c
    src/window.c
    src/chunk_table.c
    src/gx.c
    src/gx_frontend.cpp
    src/raster.c
    src/texdecode.cpp
    src/compat/ppc_abi.c
    src/log.c
    src/mem.c
    src/mmio.c
    src/dsp.c
    src/ai.c
    src/pi.c
    src/ios.c
    src/boot_env.c
    src/dbg.c
    src/interrupt.c
    src/dvd.c
    src/disc.c
    "${COLORS_GENERATED_DIR}/generated.c"
    ${COLORS_CHUNKS}
)

target_include_directories(ColorsRecomp BEFORE PRIVATE
    # Resolves "core/cpu.h" to the generated-code ABI shim, which pulls in
    # GXRuntime's real header via #include_next.
    "${CMAKE_SOURCE_DIR}/src/compat"
)
target_include_directories(ColorsRecomp PRIVATE
    "${COLORS_GENERATED_DIR}"
)

target_link_libraries(ColorsRecomp PRIVATE
    GXRuntime::runtime
    GXRuntime::retail_gx_frontend
    GXRuntime::gxcore)

if(SDL2_FOUND)
    target_include_directories(ColorsRecomp PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(ColorsRecomp PRIVATE ${SDL2_LIBRARIES})
    target_compile_definitions(ColorsRecomp PRIVATE COLORS_HAVE_SDL2=1)
    message(STATUS "ColorsRecomp: SDL2 window enabled (${SDL2_VERSION})")
else()
    message(STATUS "ColorsRecomp: SDL2 not found, headless only")
endif()
