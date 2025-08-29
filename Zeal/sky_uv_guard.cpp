#include "sky_uv_guard.h"
#include "zeal.h"
#include <Windows.h>
#include <Psapi.h>
#include <intrin.h>
#pragma intrinsic(_ReturnAddress)

SkyUVGuard::fn_s3dSetMaterialDefUVShiftPerMs SkyUVGuard::o_s3dSetUV = nullptr;
std::vector<std::uintptr_t> SkyUVGuard::s_ret_from_t3dInitSky_calls;

static const char* kGfxModule = "eqgfx_dx8.dll";

// From your dump: t3dInitSky at 0x10060990  -> offset 0x60990 from module base.
static constexpr std::uintptr_t kInitSkyFallbackOffset = 0x60990;
// How far to scan forward for calls (clamped to module image end).
static constexpr std::uintptr_t kScanMax = 0x20000; // 128 KB

SkyUVGuard::SkyUVGuard(ZealService* zeal) {
  if (!zeal || !zeal->hooks) return;

  HMODULE mod = GetModuleHandleA(kGfxModule);
  if (!mod) return;
  const auto base = reinterpret_cast<std::uintptr_t>(mod);

  // Resolve SetUV
  std::uintptr_t pSetUV =
      reinterpret_cast<std::uintptr_t>(GetProcAddress(mod, "s3dSetMaterialDefUVShiftPerMs"));
  if (!pSetUV) {
    // Known direct CALL in t3dInitSky at 0x10060A27 -> follow rel32
    const auto callsite = static_cast<int>(base + 0x60A27);
    pSetUV = static_cast<std::uintptr_t>(mem::instruction_to_absolute_address(callsite));
  }
  if (!pSetUV) return;

  // Resolve t3dInitSky start (export if present, else fallback offset)
  std::uintptr_t pInitSky =
      reinterpret_cast<std::uintptr_t>(GetProcAddress(mod, "t3dInitSky"));
  if (!pInitSky) pInitSky = base + kInitSkyFallbackOffset;

  // Determine scan range [scanBegin, scanEnd)
  MODULEINFO mi{};
  GetModuleInformation(GetCurrentProcess(), mod, &mi, sizeof(mi));
  const auto imgStart = base;
  const auto imgEnd   = base + static_cast<std::uintptr_t>(mi.SizeOfImage);

  const auto scanBegin = pInitSky;
  const auto scanEnd   = (pInitSky + kScanMax < imgEnd) ? (pInitSky + kScanMax) : imgEnd;

  // Collect every direct callsite in t3dInitSky that targets SetUV (0xE8 rel32).
  s_ret_from_t3dInitSky_calls.clear();
  for (std::uintptr_t ip = scanBegin; ip + 5 <= scanEnd; ++ip) {
    const auto op = *reinterpret_cast<const unsigned char*>(ip);
    if (op != 0xE8) continue; // CALL rel32

    const auto rel   = *reinterpret_cast<const int*>(ip + 1);
    const auto tgt   = ip + 5 + static_cast<std::uintptr_t>(rel);
    if (tgt == pSetUV) {
      s_ret_from_t3dInitSky_calls.push_back(ip + 5); // return address for this callsite
      // ip += 4; // optional tiny skip; not necessary
    }
  }

  // Fallback: if we somehow didn’t find any (shouldn’t happen on your build),
  // keep the single known return address so it still works.
  if (s_ret_from_t3dInitSky_calls.empty()) {
    s_ret_from_t3dInitSky_calls.push_back(base + 0x60A2C);
  }

  // Install hook (integer address per HookWrapper)
  auto* h = zeal->hooks->Add("Sky_SetMatUVPerMs",
                             static_cast<int>(pSetUV),
                             &SkyUVGuard::hk_s3dSetMaterialDefUVShiftPerMs,
                             hook_type_detour);
  o_s3dSetUV = h->original(o_s3dSetUV);
}

void __cdecl SkyUVGuard::hk_s3dSetMaterialDefUVShiftPerMs(void* pMatDef,
                                                          void* pCtx,
                                                          std::uint32_t uPerMs_bits,
                                                          std::uint32_t vPerMs_bits) {
  const auto ret = reinterpret_cast<std::uintptr_t>(_ReturnAddress());

  // Block if this call originated at any callsite inside t3dInitSky.
  for (auto ra : s_ret_from_t3dInitSky_calls) {
    if (ret == ra) return;
  }

  // Otherwise, pass through unchanged.
  if (o_s3dSetUV) o_s3dSetUV(pMatDef, pCtx, uPerMs_bits, vPerMs_bits);
}
