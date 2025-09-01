#pragma once
#include <cstdint>

class RenderPreset {
public:
  RenderPreset(class ZealService* zeal);
  ~RenderPreset() = default;

private:
  // __thiscall original: uint32_t __thiscall fn(void* this, int mode);
  using fn_SetUserRender = std::uint32_t(__thiscall*)(void*, int);

  // Our detour uses __fastcall so ECX(this) becomes 'thisptr' and
  // the hidden EDX is the 2nd param placeholder.
  static std::uint32_t __fastcall hk_SetUserRender(void* thisptr, void* /*edx*/, int mode);

  static fn_SetUserRender o_SetUserRender;
};
