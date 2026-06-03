// SPDX-License-Identifier: GPL-3.0-only
// pono_theme.h - Pono Print design token registry
//
// Phase A.1 of the Pono Print touch UI per docs/design/pono-print-phase-a-technical-brief.md
// and docs/design/pono-print-ui-design.md §1. All 30 tokens from the spec
// declared here, defined in pono_theme.cpp.
//
// Usage:
//   #include "pono_theme.h"
//   lv_obj_set_style_bg_color(my_obj, pono::color_accent_primary, 0);
//   lv_obj_set_style_text_font(label, pono::font_h2, 0);
//
// To install the theme (call once after lv_init() in main.cpp):
//   pono::theme_init(disp);
//
// Drop-in for Brofalo/grumpyscreen fork. Tokens never change at runtime in
// Phase A; Phase G adds runtime-switchable variants via the same API.

#pragma once
#include "lvgl.h"

namespace pono {

// ---- Color tokens (spec §1.1) ----
extern const lv_color_t color_surface_base;     // #0a0e17
extern const lv_color_t color_surface_raised;   // #0f1520
extern const lv_color_t color_surface_elevated; // #141c2b
extern const lv_color_t color_accent_primary;   // #00e5ff (cyan)
extern const lv_color_t color_accent_secondary; // #00ff88 (green)
extern const lv_color_t color_state_error;      // #ff3b5c (red)
extern const lv_color_t color_state_warning;    // #ffb020 (amber)
extern const lv_color_t color_state_intel;      // #b388ff (purple)
extern const lv_color_t color_text_primary;     // #e8edf5
extern const lv_color_t color_text_secondary;   // #8892a8
extern const lv_color_t color_text_tertiary;    // #4a5568

// ---- Border alpha tokens (overlay opacities) ----
extern const lv_opa_t opa_border_subtle;  // 6% (15/255)
extern const lv_opa_t opa_border_medium;  // 12% (31/255)
extern const lv_opa_t opa_border_strong;  // 20% (51/255)

// ---- Typography tokens (spec §1.2) ----
// Externally defined; .c files compiled from TTF via tools/regen_pono_fonts.sh
extern const lv_font_t *font_display;     // 48 Inter Bold
extern const lv_font_t *font_h1;          // 28 Inter Bold
extern const lv_font_t *font_h2;          // 20 Inter Bold
extern const lv_font_t *font_body;        // 14 Inter Regular
extern const lv_font_t *font_caption;     // 12 Inter Medium
extern const lv_font_t *font_micro;       // 10 Inter Medium
extern const lv_font_t *font_num_large;   // 32 JBM Bold
extern const lv_font_t *font_num_medium;  // 20 JBM Bold
extern const lv_font_t *font_num_small;   // 14 JBM Regular

// ---- Spacing tokens (spec §1.3) ----
constexpr lv_coord_t space_xs = 4;
constexpr lv_coord_t space_sm = 8;
constexpr lv_coord_t space_md = 12;
constexpr lv_coord_t space_lg = 16;
constexpr lv_coord_t space_xl = 24;

// ---- Radii tokens (spec §1.4) ----
constexpr lv_coord_t radius_sm = 4;
constexpr lv_coord_t radius_md = 8;
constexpr lv_coord_t radius_lg = 12;

// ---- Frame architecture constants (spec §2.1) ----
constexpr lv_coord_t frame_width   = 480;
constexpr lv_coord_t frame_height  = 272;
constexpr lv_coord_t rail_width    = 48;
constexpr lv_coord_t topbar_height = 28;
constexpr lv_coord_t content_x     = rail_width;
constexpr lv_coord_t content_y     = topbar_height;
constexpr lv_coord_t content_w     = frame_width - rail_width;
constexpr lv_coord_t content_h     = frame_height - topbar_height;

// ---- Motion duration tokens (spec §3.1) ----
constexpr uint32_t motion_fast       = 120;   // button press
constexpr uint32_t motion_base       = 200;   // default fade
constexpr uint32_t motion_slow       = 350;   // screen transition
constexpr uint32_t motion_deliberate = 600;   // boot intro, accent reveal
constexpr uint32_t motion_pulse      = 2000;  // idle pulse cycle

// ---- Init ----
//
// Registers the Pono theme on the given display. Call once after lv_init().
// Idempotent: calling twice replaces the theme on the second call.
//
// Internally: builds an lv_theme_t with an apply_cb that sets per-widget
// styles using the tokens above. Inherits from the LVGL default theme so
// untouched widget classes still get sensible defaults.
void theme_init(lv_disp_t *disp);

// ---- LV_PALETTE_* to Pono token mapping (Phase A.4 pass 6) ----
//
// Bridges user-configured int palette indices (LV_PALETTE_RED through
// LV_PALETTE_GREY, plus LV_PALETTE_NONE = 0xff sentinel) to the Pono
// Westworld Dark state + accent token cluster. Used by main_panel temp
// sensor color config where the user-supplied int is cast to lv_palette_t.
//
// Out-of-range input returns color_text_tertiary as a safe default; this
// covers LV_PALETTE_NONE + any corrupted config value.
//
// Mapping rationale per row documented in
// docs/plans/2026-05-22-pono-print-a4-panel-refactor-plan.md Decision 1.
lv_color_t color_for_palette(lv_palette_t p);

// ---- Hawaii ocean caustic backdrop (boot delighter) ----
//
// ocean_tide_init() gives `parent` a prerendered, gently scrolling ocean: a
// depth gradient with soft phase-modulated sine "caustics" baked ONCE into an
// off-screen canvas, then scrolled horizontally. Scrolling a baked canvas is a
// flat blit (no per-pixel work per frame), so it holds 60fps on the 2-core
// software renderer. The canvas is the backmost child of `parent`; foreground
// content stays on top. Idempotent: the texture is baked + the canvas created
// only on the first call, later calls just restart the scroll (reconnect).
//
// Intended for the boot/init screen, which is visible only while waiting for
// Klipper. Call ocean_tide_stop() once the wait ends so the scroll costs no
// CPU behind the live dashboard. ocean_tide_stop() is idempotent and safe
// whether or not the scroll is running.
void ocean_tide_init(lv_obj_t *parent);
void ocean_tide_stop(lv_obj_t *parent);
// Drop the cached canvas pointer after `parent` (and the canvas) is deleted.
// Defensive: the boot panel is a singleton so this never runs in production,
// but it keeps the cached pointer from dangling if that ever changes.
void ocean_tide_teardown();

// ---- Panel open animation ----
//
// panel_open() moves a panel to the foreground and fades it in from
// transparent (a short ease-out), so overlay panels "open" smoothly instead
// of snapping in. Self-contained and safe to call repeatedly: a re-open
// restarts the fade cleanly. There is intentionally no animated hide here --
// the foreground/background z-order close stays instant (the riskiest part to
// get right blind); 60 fps + the open fade already carry the feel.
void panel_open(lv_obj_t *obj);

} // namespace pono
