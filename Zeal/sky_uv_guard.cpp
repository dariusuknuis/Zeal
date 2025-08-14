// sky_uv_guard.cpp
#include "sky_uv_guard.h"

// Pull ZealService, chat print buffer, and the HookWrapper instance.
#include "zeal.h"
#include <Windows.h>

// ----- Static storage from header -----
thread_local bool SkyUVGuard::g_skip_uv = false;
SkyUVGuard::fn_t3dInitSky                    SkyUVGuard::o_t3dInitSky = nullptr;
SkyUVGuard::fn_s3dSetMaterialDefUVShiftPerMs SkyUVGuard::o_s3dSetUV   = nullptr;

// ----- Local helpers -----
static void Notify(const char* fmt, ...) {
    char buf[256];
    va_list vl; va_start(vl, fmt);
    _vsnprintf_s(buf, _TRUNCATE, fmt, vl);
    va_end(vl);
    if (auto zs = ZealService::get_instance()) {
        zs->print_buffer.emplace_back(buf);
    } else {
        OutputDebugStringA(buf);
        OutputDebugStringA("\n");
    }
}

// You can hardcode addresses for your client build here if you already have them.
// Set to 0 to force pattern/exports resolution.
static constexpr std::uint32_t kAddr_InitSky = 0;  // e.g. 0x12345678
static constexpr std::uint32_t kAddr_SetUV   = 0;  // e.g. 0x23456789

// Optional: module name most Zeal bits already use.
static const char* kGfxModule = "eqgfx_dx8.dll";

// Example pattern placeholders (IDA/byte pattern with '?' wildcards).
// Replace these with real signatures for your client if exports aren’t available.
static const char* kPat_InitSky = ""; // e.g. "55 8B EC 83 E4 F8 83 EC ?? 56 8B 75 ?? ..."
static const char* kPat_SetUV   = ""; // e.g. "55 8B EC 83 E4 F8 83 EC ?? D9 45 ?? D9 1D ?? ?? ?? ?? ..."

// If your pattern points to a prologue, offset is usually 0.
// If the pattern matches a CALL that targets the real function, set the offset to the rel32 target.
static int kPat_InitSkyOffset = 0;
static int kPat_SetUVOffset   = 0;

// Turn a CALL rel32’s address into its absolute target.
static std::uint32_t FollowRel32(std::uint32_t call_insn_address) {
    // call rel32 is 5 bytes; target = next_eip + *(int32_t*)(call+1)
    return static_cast<std::uint32_t>(call_insn_address + 5 + *reinterpret_cast<int32_t*>(call_insn_address + 1));
}

// ----- Resolution -----
std::uint32_t SkyUVGuard::ResolveAddr_InitSky() {
    if (kAddr_InitSky) return kAddr_InitSky;

    // 1) Try export (unlikely for internal funcs, but cheap check).
    if (HMODULE mod = GetModuleHandleA(kGfxModule)) {
        if (auto p = reinterpret_cast<std::uint32_t>(GetProcAddress(mod, "t3dInitSky"))) {
            return p;
        }
    }

    // 2) Try pattern.
    if (HMODULE mod = mem::find_module(kGfxModule)) {
        if (kPat_InitSky && *kPat_InitSky) {
            auto hit = static_cast<std::uint32_t>(mem::find_pattern(mod, kPat_InitSky));
            if (hit) {
                if (kPat_InitSkyOffset == 0) {
                    return hit;
                } else if (kPat_InitSkyOffset > 0) {
                    return FollowRel32(hit + kPat_InitSkyOffset);
                }
            }
        }
    }

    Notify("SkyUVGuard: failed to resolve t3dInitSky");
    return 0;
}

std::uint32_t SkyUVGuard::ResolveAddr_SetUV() {
    if (kAddr_SetUV) return kAddr_SetUV;

    // 1) Try export.
    if (HMODULE mod = GetModuleHandleA(kGfxModule)) {
        if (auto p = reinterpret_cast<std::uint32_t>(GetProcAddress(mod, "s3dSetMaterialDefUVShiftPerMs"))) {
            return p;
        }
    }

    // 2) Try pattern.
    if (HMODULE mod = mem::find_module(kGfxModule)) {
        if (kPat_SetUV && *kPat_SetUV) {
            auto hit = static_cast<std::uint32_t>(mem::find_pattern(mod, kPat_SetUV));
            if (hit) {
                if (kPat_SetUVOffset == 0) {
                    return hit;
                } else if (kPat_SetUVOffset > 0) {
                    return FollowRel32(hit + kPat_SetUVOffset);
                }
            }
        }
    }

    Notify("SkyUVGuard: failed to resolve s3dSetMaterialDefUVShiftPerMs");
    return 0;
}

// ----- Install & ctor -----
void SkyUVGuard::Install() {
    auto zs = ZealService::get_instance();
    if (!zs || !zs->hooks) {
        Notify("SkyUVGuard: Zeal hooks not ready; skipping install");
        return;
    }

    const auto addr_init = ResolveAddr_InitSky();
    const auto addr_set  = ResolveAddr_SetUV();

    if (!addr_init || !addr_set) {
        // We need both: we gate inside InitSky and decide whether to call SetUV.
        return;
    }

    // Install detours
    auto& hw = *zs->hooks;

    if (!hw.hook_map.count("Sky_InitSky")) {
        auto* h = hw.Add("Sky_InitSky", addr_init, &SkyUVGuard::hk_t3dInitSky, hook_type_detour);
        o_t3dInitSky = h->original(o_t3dInitSky);
    }

    if (!hw.hook_map.count("Sky_SetMatUVPerMs")) {
        auto* h = hw.Add("Sky_SetMatUVPerMs", addr_set, &SkyUVGuard::hk_s3dSetMaterialDefUVShiftPerMs, hook_type_detour);
        o_s3dSetUV = h->original(o_s3dSetUV);
    }

    Notify("SkyUVGuard: installed (InitSky=0x%08X, SetUV=0x%08X)", addr_init, addr_set);
}

SkyUVGuard::SkyUVGuard(ZealService* /*zeal*/) {
    // Don’t keep a pointer; just install using ZealService::get_instance().
    Install();
}

// ----- Hooks -----

// We only want UVShiftPerMs to be set initially by the normal material build paths,
// but *not* when t3dInitSky runs. So: enter guard before calling the original InitSky;
// any calls to s3dSetMaterialDefUVShiftPerMs during that window will be suppressed.
void __cdecl SkyUVGuard::hk_t3dInitSky(void* a0) {
    g_skip_uv = true;
    if (o_t3dInitSky) {
        o_t3dInitSky(a0);
    }
    g_skip_uv = false;
}

// Central gate: if we're inside InitSky, skip; otherwise, pass through untouched.
void __cdecl SkyUVGuard::hk_s3dSetMaterialDefUVShiftPerMs(void* matDef, float uPerMs, float vPerMs) {
    if (g_skip_uv) {
        // Suppressed during t3dInitSky.
        return;
    }
    if (o_s3dSetUV) {
        o_s3dSetUV(matDef, uPerMs, vPerMs);
    }
}
