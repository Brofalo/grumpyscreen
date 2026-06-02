// SPDX-License-Identifier: GPL-3.0-only
// pono_home.cpp - Pono Print home cockpit builder (see pono_home.h)
//
// Layout mirrors docs/pono-home-mockup.svg frame 1 at the printer's real
// 480x272. Pure LVGL v8 + pono_theme tokens. Coordinates are absolute within
// `parent` (no layout engine) so the design matches the mockup 1:1.

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

// Pulse a cyan glow (box shadow) on an object - the "alive" OMEGA tile.
static void glow_pulse(lv_obj_t *o, lv_color_t color) {
  lv_obj_set_style_shadow_color(o, color, 0);
  lv_obj_set_style_shadow_opa(o, LV_OPA_60, 0);
  lv_obj_set_style_shadow_spread(o, 0, 0);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, o);
  lv_anim_set_exec_cb(&a, [](void *obj, int32_t v) {
    lv_obj_set_style_shadow_width((lv_obj_t *)obj, v, 0);
  });
  lv_anim_set_values(&a, 2, 18);
  lv_anim_set_time(&a, 1300);
  lv_anim_set_playback_time(&a, 1300);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
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
  lv_obj_set_style_bg_color(parent, color_surface_base, 0);
  lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

  // living ocean backdrop (gradient + drifting swells), behind everything
  lv_obj_t *bg = lv_obj_create(parent);
  lv_obj_remove_style_all(bg);
  lv_obj_set_pos(bg, 0, 0);
  lv_obj_set_size(bg, 480, 272);
  lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);
  ocean_tide_init(bg);

  // ---- left rail ----
  card(parent, 0, 0, 52, 272, color_surface_raised, 0);
  lv_obj_t *chip = card(parent, 8, 12, 36, 36, color_accent_primary, 9, 46); // ~18%
  lv_obj_t *home_ic = lbl(parent, LV_SYMBOL_HOME, ms, color_accent_primary, 0, 0);
  lv_obj_align_to(home_ic, chip, LV_ALIGN_CENTER, 0, 0);
  lbl(parent, LV_SYMBOL_LIST,     ms, color_text_secondary, 18, 74);
  lbl(parent, LV_SYMBOL_TINT,     ms, color_text_secondary, 18, 124);
  lbl(parent, LV_SYMBOL_SETTINGS, ms, color_text_secondary, 18, 226);

  // ---- top bar ----
  lbl(parent, "Pono Print", font_h2, color_text_primary, 66, 10);
  if (m.printing) {
    lv_obj_t *pill = card(parent, 372, 12, 92, 22, color_accent_secondary, 11, 40);
    (void)pill;
    card(parent, 382, 19, 8, 8, color_accent_secondary, 4); // status dot
    lbl(parent, "PRINTING", font_micro, color_accent_secondary, 398, 16);
  }

  // ---- job hero: progress ring ----
  lv_obj_t *arc = lv_arc_create(parent);
  lv_obj_set_size(arc, 96, 96);
  lv_obj_set_pos(arc, 60, 66);
  lv_arc_set_rotation(arc, 270);
  lv_arc_set_bg_angles(arc, 0, 360);
  lv_arc_set_range(arc, 0, 100);
  lv_arc_set_value(arc, m.progress_pct);
  lv_arc_set_mode(arc, LV_ARC_MODE_NORMAL);
  lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(arc, 10, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, 10, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc, color_surface_elevated, LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, color_accent_primary, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);

  char pct[8];
  snprintf(pct, sizeof pct, "%d%%", m.progress_pct);
  lv_obj_t *pl = lbl(parent, pct, font_num_large, color_text_primary, 0, 0);
  lv_obj_align_to(pl, arc, LV_ALIGN_CENTER, 0, -6);
  char ly[28];
  snprintf(ly, sizeof ly, "layer %d/%d", m.layer, m.layer_total);
  lv_obj_t *lyl = lbl(parent, ly, font_micro, color_text_secondary, 0, 0);
  lv_obj_align_to(lyl, arc, LV_ALIGN_CENTER, 0, 18);

  // job meta (to the right of the ring). Width-clamp the name + dot-truncate
  // so no job title ever bleeds into the temp strip.
  lv_obj_t *jn = lbl(parent, m.job_name, font_h2, color_text_primary, 168, 86);
  lv_label_set_long_mode(jn, LV_LABEL_LONG_DOT);
  lv_obj_set_width(jn, 190);
  lbl(parent, m.material, font_caption, color_text_secondary, 168, 112);
  card(parent, 168, 130, 160, 24, color_surface_elevated, 8);
  lbl(parent, m.eta, font_caption, color_accent_primary, 180, 134);
  lv_obj_t *ps = card(parent, 168, 160, 178, 30, color_state_error, 9);
  lv_obj_t *psl = lbl(parent, "PAUSE / STOP", font_caption, color_surface_base, 0, 0);
  lv_obj_align_to(psl, ps, LV_ALIGN_CENTER, 0, 0);

  // ---- temp strip (glanceable, tap to set) ----
  card(parent, 360, 52, 108, 46, color_surface_elevated, 10);
  lbl(parent, "NOZZLE", font_micro, color_text_secondary, 370, 57);
  char nzt[8]; snprintf(nzt, sizeof nzt, "%d", m.nozzle);
  lbl(parent, nzt, font_num_medium, color_text_primary, 370, 72);
  char nzs[8]; snprintf(nzs, sizeof nzs, "/%d", m.nozzle_set);
  lbl(parent, nzs, font_caption, color_text_secondary, 410, 78);
  lbl(parent, "tap", font_micro, color_accent_primary, 444, 57);

  card(parent, 360, 106, 108, 46, color_surface_elevated, 10);
  lbl(parent, "BED", font_micro, color_text_secondary, 370, 111);
  char bdt[8]; snprintf(bdt, sizeof bdt, "%d", m.bed);
  lbl(parent, bdt, font_num_medium, color_text_primary, 370, 126);
  char bds[8]; snprintf(bds, sizeof bds, "/%d", m.bed_set);
  lbl(parent, bds, font_caption, color_text_secondary, 402, 132);
  lbl(parent, "tap", font_micro, color_accent_primary, 444, 111);

  // ---- OMEGA tile (front and center, glowing) ----
  lv_obj_t *om = card(parent, 360, 160, 108, 48, color_surface_base, 12);
  lv_obj_set_style_border_color(om, color_accent_primary, 0);
  lv_obj_set_style_border_width(om, 2, 0);
  lv_obj_set_style_border_opa(om, LV_OPA_COVER, 0);
  lv_obj_t *omt = lbl(parent, "OMEGA", font_caption, color_accent_primary, 0, 0);
  lv_obj_align_to(omt, om, LV_ALIGN_TOP_MID, 0, 7);
  lv_obj_t *oms = lbl(parent, "1000% calibrate", font_micro, color_text_secondary, 0, 0);
  lv_obj_align_to(oms, om, LV_ALIGN_BOTTOM_MID, 0, -7);
  glow_pulse(om, color_accent_primary);

  // ---- quick actions ----
  struct QA { const char *icon; const char *name; };
  const QA qa[4] = {
    {LV_SYMBOL_HOME,     "Home"},
    {LV_SYMBOL_DOWNLOAD, "Filament"},
    {LV_SYMBOL_SETTINGS, "Tune"},
    {LV_SYMBOL_IMAGE,    "Camera"},
  };
  for (int i = 0; i < 4; i++) {
    int x = 66 + i * 72;
    lv_obj_t *qc = card(parent, x, 210, 64, 46, color_surface_elevated, 10);
    lv_obj_t *qi = lbl(parent, qa[i].icon, ms, color_text_primary, 0, 0);
    lv_obj_align_to(qi, qc, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_t *qn = lbl(parent, qa[i].name, font_micro, color_text_secondary, 0, 0);
    lv_obj_align_to(qn, qc, LV_ALIGN_BOTTOM_MID, 0, -6);
  }

  return arc;
}

} // namespace pono
