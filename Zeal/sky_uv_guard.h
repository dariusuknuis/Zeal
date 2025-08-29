#pragma once
#include <cstdint>
#include <vector>
#include "hook_wrapper.h"
#include "memory.h"

class SkyUVGuard {
 public:
  SkyUVGuard(class ZealService* zeal);
  ~SkyUVGuard() = default;

 private:
  // Hook only SetUV and gate by caller.
  static void __cdecl hk_s3dSetMaterialDefUVShiftPerMs(void* pMatDef,
                                                       void* pCtx,
                                                       std::uint32_t uPerMs_bits,
                                                       std::uint32_t vPerMs_bits);

  using fn_s3dSetMaterialDefUVShiftPerMs =
      void(__cdecl*)(void*, void*, std::uint32_t, std::uint32_t);

  static fn_s3dSetMaterialDefUVShiftPerMs o_s3dSetUV;

  // All return addresses (one per callsite in t3dInitSky that calls SetUV).
  static std::vector<std::uintptr_t> s_ret_from_t3dInitSky_calls;
};
