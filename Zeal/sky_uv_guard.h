#pragma once
#include <cstdint>
#include "hook_wrapper.h"
#include "memory.h"

class SkyUVGuard {
 public:
  SkyUVGuard(class ZealService *zeal);
  ~SkyUVGuard() = default;
  
 private:
  void Install();
  // Hooks
  static void __cdecl hk_t3dInitSky(void* a0 /* adjust if your real proto differs */);
  static void __cdecl hk_s3dSetMaterialDefUVShiftPerMs(void* matDef, float uPerMs, float vPerMs);

  // Original types
  using fn_t3dInitSky = void(__cdecl*)(void*);
  using fn_s3dSetMaterialDefUVShiftPerMs = void(__cdecl*)(void*, float, float);

  // Resolve targets
  static std::uint32_t ResolveAddr_InitSky();
  static std::uint32_t ResolveAddr_SetUV();

  // Static storage (defined in .cpp to avoid link issues)
  static thread_local bool g_skip_uv;
  static fn_t3dInitSky                    o_t3dInitSky;
  static fn_s3dSetMaterialDefUVShiftPerMs o_s3dSetUV;

};
