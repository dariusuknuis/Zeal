#include "render_preset.h"
#include "zeal.h"
#include <intrin.h>
#pragma intrinsic(_ReturnAddress)

RenderPreset::fn_SetUserRender RenderPreset::o_SetUserRender = nullptr;

// CDisplay_SetUserRender (verify in your build)
static constexpr std::uintptr_t kCDisplay_SetUserRender = 0x004AFB6E;

// Return addresses (CALL+5) for the two known sites
static constexpr std::uintptr_t kRA_InitDDraw         = 0x004A558D; // early
static constexpr std::uintptr_t kRA_StartWorldDisplay = 0x004A870D; // late

RenderPreset::RenderPreset(ZealService* zeal) {
  if (!zeal || !zeal->hooks) return;

  auto* h = zeal->hooks->Add(
      "CDisplay_SetUserRender",
      static_cast<int>(kCDisplay_SetUserRender),
      &RenderPreset::hk_SetUserRender,
      hook_type_detour);

  o_SetUserRender = h->original(o_SetUserRender);

  if (!o_SetUserRender) {
    ZealService::get_instance()->print_buffer.emplace_back("RenderPreset: failed to resolve original.");
  } else {
    ZealService::get_instance()->print_buffer.emplace_back("RenderPreset: hook installed.");
  }
}

std::uint32_t __fastcall RenderPreset::hk_SetUserRender(void* thisptr, void* /*edx*/, int mode) {
  const auto ra = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
  int forced = ZealService::get_instance()->ini->getValue<int>("Zeal", "UserRenderPreset");

  // -1 => don't force anywhere
  if (forced >= 0) {
    if (forced == 1) {
      // Only safe late
      if (ra == kRA_StartWorldDisplay) mode = 1;
    } else if (forced >= 2) {
      // Wireframe/flat profile you observed appears only if set early
      if (ra == kRA_InitDDraw) mode = forced; // 2,3,4 all hit the same 'else' path
    } else { // forced == 0
      // If someone wants to force 0 (table), doing it early is fine (and redundant),
      // but applying late as well doesn't hurt. We'll just pass through (mode already 0 on both).
    }
  }

  return o_SetUserRender(thisptr, mode);
}
