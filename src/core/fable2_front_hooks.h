// fable_2 - hook points in the main menu's press dispatch
// (FrontMenu_PressBitDispatch, overridden in src/diagnostics/fable2_modmenu.h).
// Kept apart so a feature can claim a hook without including the menu probes.
#pragma once

#include <cstdint>

#include <rex/ppc/context.h>

namespace fable2::fronthooks {

// Called first for every raw main-menu press (r5 = pressed-button mask:
// Up 1, Down 2, Back 32, A 4096). Return true to swallow the press so the
// stock menu never sees it.
inline bool (*g_press_hook)(PPCContext& ctx, uint8_t* base) = nullptr;

}  // namespace fable2::fronthooks
