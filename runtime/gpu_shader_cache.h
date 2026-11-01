/* The disk cache of compiled shaders (DXBC files), shared by the renderer (gpu_d3d11.c) and the
 * ahead-of-time compiler (tools/shaderaot.c), so that both key, name and compile a program alike.
 *
 * A file is "<dir>/v<version>_<vs|ps|cs>_<program hash>_<vs outputs>_<jump tables' hash>.dxbc".
 * The ahead-of-time compiler dates its files 2001-01-01 until the game first uses them (the
 * renderer then dates them now): the start's prewarm makes only shaders the game has used.
 *
 * The Metal renderer (macOS) keeps MSL source under the same key, named apart so that the two
 * never mix: "<dir>/m<MSL version>_<vs|ps|cs>_<...the same...>.metal" (shader_cache_file_msl),
 * dated the same way. There is no fxc there: the D3D part below is left out. */
#pragma once
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifndef __APPLE__
#include <d3dcompiler.h>
#endif

#include "gpu_shader.h"

/* Bump when the translator changes what it generates: older files are then ignored. */
#define SHADER_CACHE_VERSION 10
/* MSL files' version: the translator's, and the MSL dialect's own revision (bump the last
 * digits for a change to MSL alone). */
#define SHADER_CACHE_MSL_VERSION (SHADER_CACHE_VERSION * 100 + 3)
#define SHADER_CACHE_UNPLAYED 0x01C07385C89DC000ull   /* FILETIME 2001-01-01 */
#define SHADER_CACHE_PLAYED   0x01C1925741648000ull   /* written after 2002-01-01: the game used it */
#define SHADER_CACHE_UNPLAYED_UNIX 978307200ll        /* the same two, as Unix times */
#define SHADER_CACHE_PLAYED_UNIX   1009843200ll

/* FNV-1a over a program's bytes (a vertex or pixel program: its header and code). */
static inline u64 shader_program_hash(const u8 *p, u32 size) {
    u64 h = 1469598103934665603ull;
    for (u32 i = 0; i < size; i++) h = (h ^ p[i]) * 1099511628211ull;
    return h;
}

/* The hash a program's cache files are named by: shader_program_hash's (and a compute
 * program's group size), mixed with a salt for the few programs whose translation takes the
 * render scale into account (shader_uses_pixels), and another for those with SSY/PBK stacks,
 * translated without them since (shader_uses_stacks), so that their files from before are not
 * used. Every other program keeps its files. */
#define SHADER_SCALE_SALT 0x7273636C30303031ull   /* "rscl0001" */
#define SHADER_STACK_SALT 0x7374636B30303031ull   /* "stck0001" */
static inline u64 shader_cache_hash(u64 hash, const u8 *program, u32 size, int stage) {
    if (shader_uses_pixels(program, size, stage)) hash = (hash ^ SHADER_SCALE_SALT) * 1099511628211ull;
    if (shader_uses_stacks(program, size)) hash = (hash ^ SHADER_STACK_SALT) * 1099511628211ull;
    return hash;
}

static inline void shader_cache_file(char *out, size_t n, const char *dir, int stage, u64 hash, u32 vs_outputs, u64 table_hash) {
    snprintf(out, n, "%s/v%u_%s_%016llx_%08x_%016llx.dxbc", dir, SHADER_CACHE_VERSION,
             stage == SHADER_VERTEX ? "vs" : stage == SHADER_PIXEL ? "ps" : "cs", (unsigned long long)hash, vs_outputs,
             (unsigned long long)table_hash);
}

static inline void shader_cache_file_msl(char *out, size_t n, const char *dir, int stage, u64 hash, u32 vs_outputs, u64 table_hash) {
    snprintf(out, n, "%s/m%u_%s_%016llx_%08x_%016llx.metal", dir, SHADER_CACHE_MSL_VERSION,
             stage == SHADER_VERTEX ? "vs" : stage == SHADER_PIXEL ? "ps" : "cs", (unsigned long long)hash, vs_outputs,
             (unsigned long long)table_hash);
}

#ifdef __APPLE__
#include <sys/stat.h>
#include <sys/time.h>

/* (macOS) A file's date: written after 2002 (the game used it), or not. */
static inline int shader_cache_played(const char *path) {
    struct stat st;
    return !stat(path, &st) && (long long)st.st_mtime >= SHADER_CACHE_PLAYED_UNIX;
}

/* A file the ahead-of-time translator wrote, now used by the game: dated now. */
static inline void shader_cache_touch(const char *path) {
    struct stat st;
    if (!stat(path, &st) && (long long)st.st_mtime < SHADER_CACHE_PLAYED_UNIX) utimes(path, NULL);
}

/* How the ahead-of-time translator dates what it writes: 2001-01-01, not yet used. */
static inline int shader_cache_unplayed(const char *path) {
    struct timeval t[2] = {{(time_t)SHADER_CACHE_UNPLAYED_UNIX, 0}, {(time_t)SHADER_CACHE_UNPLAYED_UNIX, 0}};
    return utimes(path, t);
}
#else
/* Opt-in diagnostic modes: 1 enforces IEEE rules; 2 also skips fxc optimization;
 * 3 optimizes dispatch-loop shaders as well; 4 uses FXC level 0 (distinct from
 * SKIP_OPTIMIZATION). Both levels 0 and 1 corrected green/blue outputs from a
 * recorded Iris Xe vertex shader, matching WARP and the shader debugger for
 * the same captured inputs.
 * The renderer keeps these modes in separate disk cache directories. */
static inline int shader_compiler_diagnostic_mode(void) {
    const char *value = getenv("SSBU_SHADER_COMPILER_MODE");
    return value && !strcmp(value, "strict") ? 1 : value && !strcmp(value, "strict-noopt") ? 2
         : value && !strcmp(value, "optimized") ? 3 : value && !strcmp(value, "level0") ? 4
         : value && !strcmp(value, "vertex-color") ? 5
         : value && !strcmp(value, "vertex-batch") ? 6
         : value && !strcmp(value, "crash-pair") ? 7 : 0;
}

static inline int shader_compiler_crash_target(const char *name, int stage) {
    return shader_compiler_diagnostic_mode() == 7 && name
        && ((stage == SHADER_VERTEX && !strcmp(name, "vs_4f9f3073b6b2da99"))
         || (stage == SHADER_PIXEL && !strcmp(name, "ps_b2f9c0dd20940e71")));
}

/* A bounded in-game confirmation of the VS isolated in RenderDoc event 10066.
 * Other programs retain their original compiler flags and cached bytecode. */
static inline int shader_compiler_level0_target(const char *name, int stage) {
    int mode = shader_compiler_diagnostic_mode();
    if (shader_compiler_crash_target(name, stage)) return 1;
    if (mode == 7) mode = 6; /* Preserve the previously validated color set. */
    if (stage != SHADER_VERTEX || !name) return 0;
    if (mode == 5) return !strcmp(name, "vs_6bb96a73f78a50a5");
    if (mode != 6) return 0;
    /* Diagnostic set found by Intel/WARP post-VS comparison in the recorded
     * victory frame. This is deliberately not a general Intel shader policy. */
    static const char *const targets[] = {
        "vs_5d2a0989c7b00e99", "vs_d0b1966299af9679", "vs_1e64daead57b160a",
        "vs_c2a298a9af00bd26", "vs_d892317bbff6eb17", "vs_b8e4abb56fc32b5e",
        "vs_c12ee566d3e06858", "vs_6bb96a73f78a50a5", "vs_fe96930a8ac0ad3b",
        "vs_2b674bc09538701d", "vs_03cfc33dd059231d", "vs_fe0f887b818965e5",
        "vs_580345c6fd1514dc", "vs_739c76b6e573ebd4", "vs_1669b7d183575986",
        "vs_38e6664f5324068f", "vs_02fe5c225f10059a"
    };
    for (unsigned i = 0; i < sizeof targets / sizeof targets[0]; i++)
        if (!strcmp(name, targets[i])) return 1;
    return 0;
}

static inline UINT shader_compiler_flags(const char *hlsl) {
    int mode = shader_compiler_diagnostic_mode();
    UINT flags = mode == 4 ? D3DCOMPILE_OPTIMIZATION_LEVEL0
        : mode == 2 || (mode != 3 && strstr(hlsl, "npc_")) ? D3DCOMPILE_SKIP_OPTIMIZATION : D3DCOMPILE_OPTIMIZATION_LEVEL1;
    return flags | (mode == 1 || mode == 2 ? D3DCOMPILE_IEEE_STRICTNESS : 0);
}

/* fxc as the renderer runs it: dispatch-loop programs ("npc_") without fxc's optimiser (they
 * compile much faster, and the driver optimises the bytecode anyway); any other program again
 * without it when the optimiser fails (it has internal errors on some). */
static inline HRESULT shader_cache_compile(const char *hlsl, const char *name, int stage, ID3DBlob **code, ID3DBlob **errors) {
    const char *target = stage == SHADER_VERTEX ? "vs_5_0" : stage == SHADER_PIXEL ? "ps_5_0" : "cs_5_0";
    UINT flags = shader_compiler_level0_target(name, stage)
        ? D3DCOMPILE_OPTIMIZATION_LEVEL0 : shader_compiler_flags(hlsl);
    *code = *errors = NULL;
    HRESULT hr = D3DCompile(hlsl, strlen(hlsl), name, NULL, NULL, "main", target, flags, 0, code, errors);
    if (FAILED(hr) && !(flags & D3DCOMPILE_SKIP_OPTIMIZATION) && !shader_compiler_crash_target(name, stage)) {
        if (shader_compiler_diagnostic_mode() >= 3)
            fprintf(stderr, "[gfx] optimized compilation failed for %s; falling back to unoptimized bytecode\n", name);
        if (*errors) (*errors)->lpVtbl->Release(*errors);
        *errors = NULL;
        hr = D3DCompile(hlsl, strlen(hlsl), name, NULL, NULL, "main", target,
                       D3DCOMPILE_SKIP_OPTIMIZATION | (flags & D3DCOMPILE_IEEE_STRICTNESS), 0, code, errors);
    }
    return hr;
}
#endif
