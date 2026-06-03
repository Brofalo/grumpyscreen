// SPDX-License-Identifier: GPL-3.0-only
// pono_home.cpp - Pono Print home cockpit builder (see pono_home.h)
//
// Pure LVGL v8 + pono_theme tokens, designed natively for the printer's real
// 480x272 (no 800px scaling baggage). Coordinates are absolute within `parent`.
// "Crazy good using every trick" - within a 2-core ARMv7 software renderer
// (no GPU): depth via baked gradients (rendered once, zero idle cost), a
// glowing hero arc, mono heat-aware instruments, and exactly one tiny idle
// pulse. No perpetual full-area animation.

#include "pono_home.h"
#include "pono_theme.h"

#include <cstdio>

namespace pono {

// ---- small builders -------------------------------------------------------

static lv_obj_t *card(lv_obj_t *p, int x, int y, int w, int h,
                      lv_color_t bg, int radius, lv_opa_t opa = LV_OPA_COVER) {
  lv_obj_t *o = lv_obj_create(p);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, bg, 0);
  lv_obj_set_style_bg_opa(o, opa, 0);
  lv_obj_set_style_radius(o, radius, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

static lv_obj_t *lbl(lv_obj_t *p, const char *txt, const lv_font_t *font,
                     lv_color_t color, int x, int y) {
  lv_obj_t *l = lv_label_create(p);
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color, 0);
  lv_obj_set_pos(l, x, y);
  return l;
}

// Vertical two-stop gradient fill - cheap depth, rendered once.
static void vgrad(lv_obj_t *o, lv_color_t top, lv_color_t bottom) {
  lv_obj_set_style_bg_color(o, top, 0);
  lv_obj_set_style_bg_grad_color(o, bottom, 0);
  lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
}

// Soft colored glow around an object (static unless an anim drives its width).
static void soft_shadow(lv_obj_t *o, lv_color_t color, int width, lv_opa_t opa) {
  lv_obj_set_style_shadow_color(o, color, 0);
  lv_obj_set_style_shadow_width(o, width, 0);
  lv_obj_set_style_shadow_opa(o, opa, 0);
  lv_obj_set_style_shadow_spread(o, 0, 0);
}

// 1px hairline border (subtle card edge definition).
static void hairline(lv_obj_t *o, lv_color_t color, lv_opa_t opa) {
  lv_obj_set_style_border_color(o, color, 0);
  lv_obj_set_style_border_width(o, 1, 0);
  lv_obj_set_style_border_opa(o, opa, 0);
}

// Pulse a glow on a (small) object - the one "alive" idle animation.
static void glow_pulse(lv_obj_t *o, lv_color_t color, int lo, int hi, uint32_t period) {
  lv_obj_set_style_shadow_color(o, color, 0);
  lv_obj_set_style_shadow_opa(o, LV_OPA_60, 0);
  lv_obj_set_style_shadow_spread(o, 0, 0);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, o);
  lv_anim_set_exec_cb(&a, [](void *obj, int32_t v) {
    lv_obj_set_style_shadow_width((lv_obj_t *)obj, v, 0);
  });
  lv_anim_set_values(&a, lo, hi);
  lv_anim_set_time(&a, period);
  lv_anim_set_playback_time(&a, period);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_start(&a);
}

// ---- model ----------------------------------------------------------------

HomeModel demo_home_model() {
  HomeModel m{};
  m.printing = true;
  m.progress_pct = 47;
  m.layer = 84;
  m.layer_total = 180;
  m.job_name = "DA OMEGA CUBE";
  m.material = "PA-CF . 0.25 diamond";
  m.nozzle = 248;
  m.nozzle_set = 250;
  m.bed = 60;
  m.bed_set = 60;
  m.eta = "1:12 left";
  return m;
}

// ---- the cockpit -----------------------------------------------------------

lv_obj_t *build_home(lv_obj_t *parent, const HomeModel &m) {
  const lv_font_t *ms = &lv_font_montserrat_14; // built-in: carries LV_SYMBOL_*

  lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(parent, 0, 0);

  // Static ocean-depth gradient backdrop. No animation -> zero idle cost on the
  // always-on dashboard (the boot screen gets the live tide; here a baked
  // navy -> teal-black vertical wash carries the depth for free).
  lv_obj_set_style_bg_color(parent, lv_color_hex(0x0c1422), 0);
  lv_obj_set_style_bg_grad_color(parent, lv_color_hex(0x05090f), 0);
  lv_obj_set_style_bg_grad_dir(parent, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

  // ---- left rail ----
  lv_obj_t *rail = card(parent, 0, 0, 52, 272, color_surface_raised, 0);
  vgrad(rail, color_surface_elevated, color_surface_raised);
  lv_obj_t *chip = card(parent, 8, 12, 36, 36, color_accent_primary, 10, 55);
  soft_shadow(chip, color_accent_primary, 14, LV_OPA_40);
  lv_obj_t *home_ic = lbl(parent, LV_SYMBOL_HOME, ms, color_accent_primary, 0, 0);
  lv_obj_align_to(home_ic, chip, LV_ALIGN_CENTER, 0, 0);
  lbl(parent, LV_SYMBOL_LIST,     ms, color_text_secondary, 18, 80);
  lbl(parent, LV_SYMBOL_TINT,     ms, color_text_secondary, 18, 128);
  lbl(parent, LV_SYMBOL_SETTINGS, ms, color_text_secondary, 18, 228);

  // ---- top bar ----
  lbl(parent, "Pono Print", font_h2, color_text_primary, 66, 8);
  if (m.printing) {
    lv_obj_t *pill = card(parent, 374, 10, 94, 22, color_surface_elevated, 11);
    hairline(pill, color_accent_secondary, opa_border_strong);
    lv_obj_t *dot = card(parent, 384, 17, 8, 8, color_accent_secondary, 4);
    soft_shadow(dot, color_accent_secondary, 8, LV_OPA_COVER);
    lbl(parent, "PRINTING", font_micro, color_accent_secondary, 400, 14);
    glow_pulse(dot, color_accent_secondary, 3, 12, 850);  // tiny: the "alive" beat
  }

  // ---- job hero: progress ring with a soft cyan halo ----
  lv_obj_t *halo = card(parent, 60, 62, 100, 100, color_surface_base, 50, LV_OPA_TRANSP);
  soft_shadow(halo, color_accent_primary, 24, LV_OPA_30);
  lv_obj_t *arc = lv_arc_create(parent);
  lv_obj_set_size(arc, 100, 100);
  lv_obj_set_pos(arc, 60, 62);
  lv_arc_set_rotation(arc, 270);
  lv_arc_set_bg_angles(arc, 0, 360);
  lv_arc_set_range(arc, 0, 100);
  lv_arc_set_value(arc, m.progress_pct);
  lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(arc, 6, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, 11, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc, color_surface_elevated, LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, color_accent_primary, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);

  char pct[8];
  snprintf(pct, sizeof pct, "%d%%", m.progress_pct);
  lv_obj_t *pl = lbl(parent, pct, font_num_large, color_text_primary, 0, 0);
  lv_obj_align_to(pl, arc, LV_ALIGN_CENTER, 0, -7);
  char ly[28];
  snprintf(ly, sizeof ly, "layer %d / %d", m.layer, m.layer_total);
  lv_obj_t *lyl = lbl(parent, ly, font_micro, color_text_secondary, 0, 0);
  lv_obj_align_to(lyl, arc, LV_ALIGN_CENTER, 0, 18);

  // ---- job meta (right of ring) ----
  lv_obj_t *jn = lbl(parent, m.job_name, font_h2, color_text_primary, 172, 68);
  lv_label_set_long_mode(jn, LV_LABEL_LONG_DOT);
  lv_obj_set_width(jn, 190);
  lbl(parent, m.material, font_caption, color_text_secondary, 172, 93);
  lv_obj_t *etap = card(parent, 172, 114, 150, 24, color_surface_elevated, 8);
  hairline(etap, color_accent_primary, opa_border_medium);
  lbl(parent, m.eta, font_caption, color_accent_primary, 184, 118);

  // ---- pause / stop (gradient fill + icon) ----
  lv_obj_t *ps = card(parent, 172, 150, 180, 36, color_state_error, 10);
  vgrad(ps, lv_color_hex(0xff5d77), lv_color_hex(0xe11d48));
  soft_shadow(ps, color_state_error, 12, LV_OPA_40);
  lv_obj_t *psl = lbl(parent, LV_SYMBOL_PAUSE "   PAUSE / STOP", ms, color_text_primary, 0, 0);
  lv_obj_align_to(psl, ps, LV_ALIGN_CENTER, 0, 0);

  // ---- temp instruments (mono numbers, amber when hot) ----
  lv_obj_t *nzc = card(parent, 360, 48, 110, 48, color_surface_elevated, 10);
  vgrad(nzc, color_surface_elevated, color_surface_raised);
  hairline(nzc, color_text_tertiary, opa_border_subtle);
  lbl(parent, "NOZZLE", font_micro, color_text_secondary, 370, 54);
  lv_color_t nz_col = (m.nozzle >= 45) ? color_state_warning : color_text_primary;
  char nzt[8]; snprintf(nzt, sizeof nzt, "%d", m.nozzle);
  lbl(parent, nzt, font_num_medium, nz_col, 370, 68);
  char nzs[10]; snprintf(nzs, sizeof nzs, "/%d", m.nozzle_set);
  lbl(parent, nzs, font_num_small, color_text_secondary, 410, 75);
  lbl(parent, "tap", font_micro, color_accent_primary, 446, 54);

  lv_obj_t *bdc = card(parent, 360, 102, 110, 48, color_surface_elevated, 10);
  vgrad(bdc, color_surface_elevated, color_surface_raised);
  hairline(bdc, color_text_tertiary, opa_border_subtle);
  lbl(parent, "BED", font_micro, color_text_secondary, 370, 108);
  lv_color_t bd_col = (m.bed >= 40) ? color_state_warning : color_text_primary;
  char bdt[8]; snprintf(bdt, sizeof bdt, "%d", m.bed);
  lbl(parent, bdt, font_num_medium, bd_col, 370, 122);
  char bds[10]; snprintf(bds, sizeof bds, "/%d", m.bed_set);
  lbl(parent, bds, font_num_small, color_text_secondary, 402, 129);
  lbl(parent, "tap", font_micro, color_accent_primary, 446, 108);

  // ---- tune tiers: TUNE (standard) + OMEGA (enhanced) ----
  // Two tap targets so OMEGA reads as the enhanced option, not the only one.
  // TUNE = the everyday on-device calibrate; OMEGA = the 1000% suite (glows).
  lv_obj_t *tn = card(parent, 360, 156, 53, 50, color_surface_elevated, 12);
  vgrad(tn, color_surface_elevated, color_surface_raised);
  hairline(tn, color_text_tertiary, opa_border_medium);
  lv_obj_t *tnt = lbl(parent, "TUNE", font_caption, color_text_primary, 0, 0);
  lv_obj_align_to(tnt, tn, LV_ALIGN_TOP_MID, 0, 9);
  lv_obj_t *tns = lbl(parent, "standard", font_micro, color_text_secondary, 0, 0);
  lv_obj_align_to(tns, tn, LV_ALIGN_BOTTOM_MID, 0, -9);

  lv_obj_t *om = card(parent, 417, 156, 53, 50, color_surface_base, 12);
  vgrad(om, color_surface_elevated, color_surface_base);
  lv_obj_set_style_border_color(om, color_accent_primary, 0);
  lv_obj_set_style_border_width(om, 2, 0);
  lv_obj_set_style_border_opa(om, LV_OPA_COVER, 0);
  soft_shadow(om, color_accent_primary, 10, LV_OPA_40);  // static glow marks the enhanced tier
  lv_obj_t *omt = lbl(parent, "OMEGA", font_caption, color_accent_primary, 0, 0);
  lv_obj_align_to(omt, om, LV_ALIGN_TOP_MID, 0, 9);
  lv_obj_t *oms = lbl(parent, "1000%", font_micro, color_text_secondary, 0, 0);
  lv_obj_align_to(oms, om, LV_ALIGN_BOTTOM_MID, 0, -9);

  // ---- quick actions (spread across the base) ----
  struct QA { const char *icon; const char *name; };
  const QA qa[4] = {
    {LV_SYMBOL_GPS,       "Move"},
    {LV_SYMBOL_DOWNLOAD,  "Filament"},
    {LV_SYMBOL_DIRECTORY, "Files"},
    {LV_SYMBOL_IMAGE,     "Camera"},
  };
  for (int i = 0; i < 4; i++) {
    int x = 72 + i * 100;
    lv_obj_t *qc = card(parent, x, 214, 84, 44, color_surface_elevated, 10);
    vgrad(qc, color_surface_elevated, color_surface_raised);
    hairline(qc, color_text_tertiary, opa_border_subtle);
    lv_obj_t *qi = lbl(parent, qa[i].icon, ms, color_text_primary, 0, 0);
    lv_obj_align_to(qi, qc, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_t *qn = lbl(parent, qa[i].name, font_micro, color_text_secondary, 0, 0);
    lv_obj_align_to(qn, qc, LV_ALIGN_BOTTOM_MID, 0, -6);
  }

  return arc;
}

// ---- Tune screen -----------------------------------------------------------
// Flexibility lives one tap in: the two tiers up top, granular calibrations in
// the middle, and a live speed slider at the base (the smooth 60fps moment -
// dragging repaints only the slider, so it stays buttery on this SoC).
void build_tune(lv_obj_t *parent) {
  const lv_font_t *ms = &lv_font_montserrat_14;

  lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(parent, 0, 0);
  lv_obj_set_style_bg_color(parent, lv_color_hex(0x0c1422), 0);
  lv_obj_set_style_bg_grad_color(parent, lv_color_hex(0x05090f), 0);
  lv_obj_set_style_bg_grad_dir(parent, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

  // left rail with a back chip
  lv_obj_t *rail = card(parent, 0, 0, 52, 272, color_surface_raised, 0);
  vgrad(rail, color_surface_elevated, color_surface_raised);
  lv_obj_t *back = card(parent, 8, 12, 36, 36, color_surface_elevated, 10);
  hairline(back, color_text_tertiary, opa_border_medium);
  lv_obj_t *bi = lbl(parent, LV_SYMBOL_LEFT, ms, color_accent_primary, 0, 0);
  lv_obj_align_to(bi, back, LV_ALIGN_CENTER, 0, 0);

  lbl(parent, "Tune", font_h1, color_text_primary, 64, 6);

  // ---- two tiers ----
  lv_obj_t *st = card(parent, 64, 44, 192, 46, color_surface_elevated, 12);
  vgrad(st, color_surface_elevated, color_surface_raised);
  hairline(st, color_text_tertiary, opa_border_medium);
  lbl(parent, "Standard", font_body, color_text_primary, 78, 52);
  lbl(parent, "full auto-calibrate", font_micro, color_text_secondary, 78, 71);

  lv_obj_t *om = card(parent, 266, 44, 192, 46, color_surface_base, 12);
  vgrad(om, color_surface_elevated, color_surface_base);
  lv_obj_set_style_border_color(om, color_accent_primary, 0);
  lv_obj_set_style_border_width(om, 2, 0);
  lv_obj_set_style_border_opa(om, LV_OPA_COVER, 0);
  soft_shadow(om, color_accent_primary, 10, LV_OPA_40);
  lbl(parent, "OMEGA  1000%", font_body, color_accent_primary, 280, 52);
  lbl(parent, "enhanced suite", font_micro, color_text_secondary, 280, 71);

  // ---- individual calibrations (the granular knobs) ----
  lbl(parent, "INDIVIDUAL", font_micro, color_text_secondary, 64, 98);
  const char *cals[5] = {"Bed Mesh", "Pressure Adv", "Flow", "Input Shaper", "Z-Offset"};
  for (int i = 0; i < 5; i++) {
    int col = i % 3, row = i / 3;
    int x = 64 + col * 134;
    int y = 114 + row * 46;
    lv_obj_t *t = card(parent, x, y, 126, 40, color_surface_elevated, 10);
    vgrad(t, color_surface_elevated, color_surface_raised);
    hairline(t, color_text_tertiary, opa_border_subtle);
    lv_obj_t *nl = lbl(parent, cals[i], font_caption, color_text_primary, 0, 0);
    lv_obj_align_to(nl, t, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_t *ch = lbl(parent, LV_SYMBOL_RIGHT, ms, color_text_tertiary, 0, 0);
    lv_obj_align_to(ch, t, LV_ALIGN_RIGHT_MID, -8, 0);
  }

  // ---- live speed slider (the "smooth where it matters" moment) ----
  lbl(parent, "SPEED", font_micro, color_text_secondary, 64, 212);
  lv_obj_t *spv = lbl(parent, "100%", font_num_small, color_accent_primary, 0, 0);
  lv_obj_set_pos(spv, 424, 210);
  lv_obj_t *sl = lv_slider_create(parent);
  lv_obj_set_pos(sl, 64, 232);
  lv_obj_set_size(sl, 394, 10);
  lv_slider_set_range(sl, 50, 200);
  lv_slider_set_value(sl, 100, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(sl, color_surface_elevated, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(sl, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(sl, 5, LV_PART_MAIN);
  lv_obj_set_style_bg_color(sl, color_accent_primary, LV_PART_INDICATOR);
  lv_obj_set_style_radius(sl, 5, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(sl, color_accent_primary, LV_PART_KNOB);
  lv_obj_set_style_shadow_color(sl, color_accent_primary, LV_PART_KNOB);
  lv_obj_set_style_shadow_width(sl, 10, LV_PART_KNOB);
  lv_obj_set_style_shadow_opa(sl, LV_OPA_50, LV_PART_KNOB);
}

// ---- Expert Tune screen ----------------------------------------------------
// One full-width settings row; returns the card so the caller drops the editor.
static lv_obj_t *setting_row(lv_obj_t *list, const char *name) {
  lv_obj_t *r = lv_obj_create(list);
  lv_obj_remove_style_all(r);
  lv_obj_set_size(r, lv_pct(100), 36);
  vgrad(r, color_surface_elevated, color_surface_raised);
  lv_obj_set_style_radius(r, 8, 0);
  hairline(r, color_text_tertiary, opa_border_subtle);
  lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *n = lbl(r, name, font_caption, color_text_primary, 0, 0);
  lv_obj_align(n, LV_ALIGN_LEFT_MID, 12, 0);
  return r;
}

// Tappable value field on the right of a row (tap -> editor/keypad in the app).
static void value_pill(lv_obj_t *row, const char *val) {
  lv_obj_t *p = lv_obj_create(row);
  lv_obj_remove_style_all(p);
  lv_obj_set_size(p, 80, 26);
  lv_obj_align(p, LV_ALIGN_RIGHT_MID, -8, 0);
  lv_obj_set_style_bg_color(p, color_surface_base, 0);
  lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(p, 6, 0);
  hairline(p, color_accent_primary, opa_border_medium);
  lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *v = lbl(p, val, font_caption, color_accent_primary, 0, 0);
  lv_obj_center(v);
}

void build_settings(lv_obj_t *parent) {
  const lv_font_t *ms = &lv_font_montserrat_14;

  lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(parent, 0, 0);
  lv_obj_set_style_bg_color(parent, lv_color_hex(0x0c1422), 0);
  lv_obj_set_style_bg_grad_color(parent, lv_color_hex(0x05090f), 0);
  lv_obj_set_style_bg_grad_dir(parent, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

  lv_obj_t *rail = card(parent, 0, 0, 52, 272, color_surface_raised, 0);
  vgrad(rail, color_surface_elevated, color_surface_raised);
  lv_obj_t *back = card(parent, 8, 12, 36, 36, color_surface_elevated, 10);
  hairline(back, color_text_tertiary, opa_border_medium);
  lv_obj_t *bi = lbl(parent, LV_SYMBOL_LEFT, ms, color_accent_primary, 0, 0);
  lv_obj_align_to(bi, back, LV_ALIGN_CENTER, 0, 0);

  lbl(parent, "Expert Tune", font_h1, color_text_primary, 64, 6);

  // category chips (active = Quality)
  const char *cats[5] = {"Quality", "Speed", "Walls", "Infill", "Filament"};
  for (int i = 0; i < 5; i++) {
    int x = 64 + i * 79;
    bool active = (i == 0);
    lv_obj_t *c = card(parent, x, 44, 74, 26, active ? color_accent_primary : color_surface_elevated, 13);
    if (!active) { vgrad(c, color_surface_elevated, color_surface_raised); hairline(c, color_text_tertiary, opa_border_subtle); }
    lv_obj_t *cl = lbl(parent, cats[i], font_micro, active ? color_surface_base : color_text_secondary, 0, 0);
    lv_obj_align_to(cl, c, LV_ALIGN_CENTER, 0, 0);
  }

  // scrollable parameter list (the full-tuneability surface)
  lv_obj_t *list = lv_obj_create(parent);
  lv_obj_remove_style_all(list);
  lv_obj_set_pos(list, 64, 78);
  lv_obj_set_size(list, 400, 188);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(list, 7, 0);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);
  lv_obj_set_style_bg_color(list, color_accent_primary, LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(list, LV_OPA_40, LV_PART_SCROLLBAR);
  lv_obj_set_style_width(list, 3, LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(list, 2, LV_PART_SCROLLBAR);

  value_pill(setting_row(list, "Layer height"), "0.16 mm");
  value_pill(setting_row(list, "Wall loops"), "3");

  // infill: inline slider + live value
  {
    lv_obj_t *r = setting_row(list, "Infill density");
    lv_obj_t *iv = lbl(r, "15%", font_num_small, color_accent_primary, 0, 0);
    lv_obj_align(iv, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_t *sl = lv_slider_create(r);
    lv_obj_align(sl, LV_ALIGN_RIGHT_MID, -56, 0);
    lv_obj_set_size(sl, 96, 8);
    lv_slider_set_range(sl, 0, 100);
    lv_slider_set_value(sl, 15, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sl, color_surface_base, LV_PART_MAIN);
    lv_obj_set_style_radius(sl, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, color_accent_primary, LV_PART_INDICATOR);
    lv_obj_set_style_radius(sl, 4, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, color_accent_primary, LV_PART_KNOB);
  }

  value_pill(setting_row(list, "Top/bottom layers"), "4");

  // ironing: a real toggle
  {
    lv_obj_t *r = setting_row(list, "Ironing");
    lv_obj_t *sw = lv_switch_create(r);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_size(sw, 46, 24);
    lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, color_surface_base, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, color_accent_primary, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, color_text_primary, LV_PART_KNOB);
  }

  value_pill(setting_row(list, "Seam position"), "Aligned");
  value_pill(setting_row(list, "Flow ratio"), "0.97");
  value_pill(setting_row(list, "Pressure advance"), "0.040");
}

} // namespace pono
