// SPDX-License-Identifier: GPL-3.0-only
// pono_home.cpp - Pono Print home cockpit builder (see pono_home.h)
//
// Pure LVGL v8 + pono_theme tokens, designed natively for the printer's real
// 480x272 (no 800px scaling baggage). Laid out on ONE grid: 12px outer margin,
// a top bar, a two-column main zone (hero + readouts/actions), and a bottom
// nav bar. Every element shares the same margins and gutters - the old
// hand-placed magic numbers were why it read as scattered.
//
// "Crazy good using every trick" - within a 2-core ARMv7 software renderer
// (no GPU): depth via baked gradients (rendered once, zero idle cost), one
// hero arc, heat-aware instruments, and exactly one tiny idle pulse. No
// perpetual full-area animation.

#include "pono_home.h"
#include "pono_theme.h"
#include "pono_anim.h"   // Hawaii flag asset + comet spinner for the boot screen

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

// Public: start/stop the PRINTING-pill beat (declared in pono_home.h). Call only
// on the idle<->printing transition - it deletes any running pulse first, so a
// per-frame call would reset and stutter the animation. Keeps the always-on
// idle dashboard at zero animation cost (no pulse runs while not printing).
void set_state_pulse(lv_obj_t *dot, bool on) {
  if (!dot) return;
  lv_anim_del(dot, nullptr);
  if (on) {
    glow_pulse(dot, color_accent_secondary, 3, 11, 850);
  } else {
    lv_obj_set_style_shadow_width(dot, 3, 0);  // settle to a calm static dot
  }
}

// Heat-aware temperature readout: "Nozzle        248 / 250" (or "off" when idle).
// Fixed columns so a digit-count change on live update never shifts the layout.
static void temp_card(lv_obj_t *p, int x, int y, int w, int h, const char *name,
                      int val, int target,
                      lv_obj_t **o_card, lv_obj_t **o_val, lv_obj_t **o_set) {
  lv_obj_t *c = card(p, x, y, w, h, color_surface_elevated, 12);
  vgrad(c, color_surface_elevated, color_surface_raised);
  hairline(c, color_text_tertiary, opa_border_subtle);
  lv_obj_t *nm = lbl(c, name, font_caption, color_text_secondary, 0, 0);
  lv_obj_align(nm, LV_ALIGN_LEFT_MID, 14, 0);
  lv_color_t vc = (val >= 240) ? color_state_error
                : (val >= 45)  ? color_state_warning
                               : color_text_primary;
  char vb[12]; snprintf(vb, sizeof vb, "%d", val);
  lv_obj_t *v = lbl(c, vb, font_num_medium, vc, 0, 0);
  lv_obj_align(v, LV_ALIGN_LEFT_MID, 118, 0);
  char sb[16];
  if (target > 0) snprintf(sb, sizeof sb, "/ %d", target);
  else            snprintf(sb, sizeof sb, "off");
  lv_obj_t *s = lbl(c, sb, font_caption, color_text_secondary, 0, 0);
  lv_obj_align(s, LV_ALIGN_RIGHT_MID, -14, 0);
  if (o_card) *o_card = c;
  if (o_val)  *o_val = v;
  if (o_set)  *o_set = s;
}

// Bottom-bar navigation tile: centered icon over a caption.
static lv_obj_t *nav_tile(lv_obj_t *p, int x, int y, int w, int h,
                          const char *icon, const char *name, const lv_font_t *ms) {
  lv_obj_t *t = card(p, x, y, w, h, color_surface_elevated, 12);
  vgrad(t, color_surface_elevated, color_surface_raised);
  hairline(t, color_text_tertiary, opa_border_subtle);
  lv_obj_t *ic = lbl(t, icon, ms, color_accent_primary, 0, 0);
  lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, 9);
  lv_obj_t *nl = lbl(t, name, font_micro, color_text_secondary, 0, 0);
  lv_obj_align(nl, LV_ALIGN_BOTTOM_MID, 0, -7);
  return t;
}

// ---- model ----------------------------------------------------------------

HomeModel demo_home_model() {
  HomeModel m{};
  m.printing = true;
  m.progress_pct = 47;
  m.layer = 84;
  m.layer_total = 180;
  m.job_name = "omega_cube.gcode";
  m.material = "PA-CF . 0.25 diamond";
  m.nozzle = 248;
  m.nozzle_set = 250;
  m.bed = 60;
  m.bed_set = 60;
  m.eta = "1:12 left";
  return m;
}

HomeModel demo_home_paused_model() {
  HomeModel m = demo_home_model();
  m.paused = true;
  return m;
}

HomeModel demo_home_idle_model() {
  HomeModel m{};
  m.printing = false;
  m.progress_pct = 0;
  m.layer = 0;
  m.layer_total = 0;
  m.job_name = "";
  m.material = "PA-CF . 0.25 diamond";
  m.nozzle = 32;
  m.nozzle_set = 0;
  m.bed = 32;
  m.bed_set = 0;
  m.eta = "";
  return m;
}

// Forward decls: sub-screen chrome is defined in the native section below,
// but build_tune (above it) references screen_header.
static lv_obj_t *screen_header(lv_obj_t *parent, const char *title);
static lv_obj_t *tap_btn(lv_obj_t *p, int x, int y, int w, int h,
                         const char *txt, const lv_font_t *f, lv_color_t tc);

// ---- the cockpit -----------------------------------------------------------

lv_obj_t *build_home(lv_obj_t *parent, const HomeModel &m, HomeHandles *out) {
  const lv_font_t *ms = &lv_font_montserrat_14; // built-in: carries LV_SYMBOL_*
  const bool pr = m.printing;

  // ---- one grid for the whole screen ----
  const int PAD = 12, GUT = 10, W = 480, H = 272;
  const int CX0 = PAD, CX1 = W - PAD;             // content x: 12 .. 468 (456 wide)
  const int TOP_Y = 10, TOP_H = 26;               // top bar
  const int MAIN_Y = TOP_Y + TOP_H + 8;           // 44
  const int BAR_H = 54, BAR_Y = H - PAD - BAR_H;  // 206
  const int MAIN_H = BAR_Y - GUT - MAIN_Y;        // 152

  lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(parent, 0, 0);

  // baked vertical depth wash (rendered once, zero idle cost)
  lv_obj_set_style_bg_color(parent, lv_color_hex(0x0b1220), 0);
  lv_obj_set_style_bg_grad_color(parent, lv_color_hex(0x070b12), 0);
  lv_obj_set_style_bg_grad_dir(parent, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

  // ===== TOP BAR: wordmark + one state chip + a hairline divider =====
  lbl(parent, "Pono Print", font_h2, color_text_primary, CX0, TOP_Y + 2);
  {
    lv_color_t sc = m.paused ? color_state_warning
                  : pr       ? color_accent_secondary : color_text_secondary;
    lv_obj_t *pill = card(parent, CX1 - 104, TOP_Y, 104, TOP_H - 2, color_surface_elevated, 12);
    hairline(pill, sc, opa_border_strong);
    lv_obj_t *dot = card(pill, 0, 0, 8, 8, sc, 4);
    lv_obj_align(dot, LV_ALIGN_LEFT_MID, 11, 0);
    lv_obj_t *prl = lbl(pill, m.paused ? "PAUSED" : (pr ? "PRINTING" : "READY"), font_micro, sc, 0, 0);
    lv_obj_align(prl, LV_ALIGN_LEFT_MID, 25, 0);
    if (pr && !m.paused) glow_pulse(dot, color_accent_secondary, 3, 11, 850);
    if (out) { out->state_pill = pill; out->state_dot = dot; }
  }
  card(parent, CX0, TOP_Y + TOP_H + 1, CX1 - CX0, 1, color_text_tertiary, 0, opa_border_subtle);

  // ===== MAIN: left hero (ring) + right column (temps + actions) =====
  const int HERO_W = 176, HERO_X = CX0;
  const int RCOL_X = CX0 + HERO_W + GUT, RCOL_W = CX1 - RCOL_X;  // 198, 270

  // ---- hero card ----
  lv_obj_t *hero = card(parent, HERO_X, MAIN_Y, HERO_W, MAIN_H, color_surface_raised, 16);
  vgrad(hero, color_surface_elevated, color_surface_raised);
  hairline(hero, color_text_tertiary, opa_border_subtle);

  const int ringD = 104, ringX = HERO_X + (HERO_W - ringD) / 2, ringY = MAIN_Y + 14;
  lv_obj_t *arc = lv_arc_create(parent);
  lv_obj_set_size(arc, ringD, ringD);
  lv_obj_set_pos(arc, ringX, ringY);
  lv_arc_set_rotation(arc, 270);
  lv_arc_set_bg_angles(arc, 0, 360);
  lv_arc_set_range(arc, 0, 100);
  lv_arc_set_value(arc, pr ? m.progress_pct : 0);
  lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(arc, 8, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, 8, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc, color_surface_base, LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, color_accent_primary, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);

  char pctbuf[8];
  if (pr) snprintf(pctbuf, sizeof pctbuf, "%d%%", m.progress_pct);
  lv_obj_t *pl = lbl(parent, pr ? pctbuf : "Ready",
                     pr ? font_num_large : font_h2, color_text_primary, 0, 0);
  lv_obj_align_to(pl, arc, LV_ALIGN_CENTER, 0, 0);

  char lybuf[28];
  if (pr) snprintf(lybuf, sizeof lybuf, "layer %d / %d", m.layer, m.layer_total);
  lv_obj_t *lyl = lbl(parent, pr ? lybuf : m.material, font_micro, color_text_secondary, 0, 0);
  lv_obj_align_to(lyl, arc, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);

  lv_obj_t *eta_l = lbl(parent, pr ? m.eta : "", font_caption, color_accent_primary, 0, 0);
  lv_obj_align_to(eta_l, lyl, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);

  // ---- right column: two temp readouts ----
  const int tH = 42;
  lv_obj_t *nzc = nullptr, *nz_num = nullptr, *nz_set = nullptr;
  lv_obj_t *bdc = nullptr, *bd_num = nullptr, *bd_set = nullptr;
  temp_card(parent, RCOL_X, MAIN_Y, RCOL_W, tH, "Nozzle",
            m.nozzle, m.nozzle_set, &nzc, &nz_num, &nz_set);
  temp_card(parent, RCOL_X, MAIN_Y + tH + 8, RCOL_W, tH, "Bed",
            m.bed, m.bed_set, &bdc, &bd_num, &bd_set);

  // ---- action row: printing/paused -> [Pause|Resume] + Cancel + Tune; idle -> Print + Tune ----
  const int ay = MAIN_Y + 2 * (tH + 8);          // 144
  const int aH = MAIN_Y + MAIN_H - ay;           // 52
  lv_obj_t *prim = nullptr, *cxl = nullptr;
  int tnx, tnw;
  if (pr) {
    const int pw = 104, cw = 66, g = 6;
    // primary: Resume (cyan, go) when paused, else Pause (amber, caution)
    prim = card(parent, RCOL_X, ay, pw, aH, m.paused ? color_accent_primary : color_state_warning, 12);
    if (m.paused) vgrad(prim, lv_color_hex(0x33eaff), lv_color_hex(0x00b3cc));
    else          vgrad(prim, lv_color_hex(0xffb84d), lv_color_hex(0xe08600));
    soft_shadow(prim, m.paused ? color_accent_primary : color_state_warning, 12, LV_OPA_40);
    lv_obj_t *priml = lbl(prim, m.paused ? (LV_SYMBOL_PLAY "  Resume") : (LV_SYMBOL_PAUSE "  Pause"),
                          ms, color_surface_base, 0, 0);
    lv_obj_center(priml);
    // cancel/abort (red); the app gates it behind a confirm dialog
    cxl = card(parent, RCOL_X + pw + g, ay, cw, aH, color_state_error, 12);
    vgrad(cxl, lv_color_hex(0xff5d77), lv_color_hex(0xe11d48));
    soft_shadow(cxl, color_state_error, 12, LV_OPA_40);
    lv_obj_center(lbl(cxl, LV_SYMBOL_STOP, ms, color_text_primary, 0, 0));
    tnx = RCOL_X + pw + g + cw + g;   // 182
    tnw = CX1 - tnx;                  // 88, to the content right edge
  } else {
    const int primW = 168;
    prim = card(parent, RCOL_X, ay, primW, aH, color_accent_primary, 12);
    vgrad(prim, lv_color_hex(0x33eaff), lv_color_hex(0x00b3cc));
    soft_shadow(prim, color_accent_primary, 12, LV_OPA_40);
    lv_obj_center(lbl(prim, LV_SYMBOL_PLAY "  Print", ms, color_surface_base, 0, 0));
    tnx = RCOL_X + primW + GUT - 2;
    tnw = RCOL_W - primW - GUT + 2;
  }

  lv_obj_t *tn = card(parent, tnx, ay, tnw, aH, color_surface_elevated, 12);
  vgrad(tn, color_surface_elevated, color_surface_raised);
  hairline(tn, color_text_tertiary, opa_border_medium);
  lv_obj_t *tni = lbl(tn, LV_SYMBOL_SETTINGS, ms, color_accent_primary, 0, 0);
  lv_obj_align(tni, LV_ALIGN_TOP_MID, 0, 8);
  lv_obj_t *tnl = lbl(tn, "Tune", font_micro, color_text_secondary, 0, 0);
  lv_obj_align(tnl, LV_ALIGN_BOTTOM_MID, 0, -7);

  // ===== BOTTOM NAV BAR: five equal tiles =====
  const char *bi[5] = {LV_SYMBOL_GPS, LV_SYMBOL_DOWNLOAD, LV_SYMBOL_DIRECTORY,
                       LV_SYMBOL_LOOP, LV_SYMBOL_LIST};
  const char *bn[5] = {"Move", "Filament", "Files", "Fans", "More"};
  const int tw = 84, tg = 8;
  const int bx0 = CX0 + ((CX1 - CX0) - (5 * tw + 4 * tg)) / 2;  // centered (== 14)
  for (int i = 0; i < 5; i++) {
    int x = bx0 + i * (tw + tg);
    lv_obj_t *t = nav_tile(parent, x, BAR_Y, tw, BAR_H, bi[i], bn[i], ms);
    if (out) { if (i < 4) out->qa[i] = t; else out->tile_more = t; }
  }

  if (out) {
    out->arc = arc; out->pct = pl; out->layer = lyl;
    out->job = nullptr; out->material = nullptr; out->eta = eta_l;
    out->nozzle = nz_num; out->bed = bd_num;
    out->nozzle_set = nz_set; out->bed_set = bd_set;
    out->tile_nozzle = nzc; out->tile_bed = bdc;
    out->tile_tune = tn; out->tile_omega = nullptr; out->btn_pausestop = prim; out->btn_cancel = cxl;
  }
  return arc;
}

// ---- Tune screen -----------------------------------------------------------
// Flexibility lives one tap in: the two tiers up top, granular calibrations in
// the middle, and a live speed slider at the base (the smooth 60fps moment -
// dragging repaints only the slider, so it stays buttery on this SoC).
void build_tune(lv_obj_t *parent, TuneHandles *h) {
  const lv_font_t *ms = &lv_font_montserrat_14;
  lv_obj_t *back = screen_header(parent, "Tune");
  if (h) h->back = back;

  // ---- two tiers ----
  lv_obj_t *st = card(parent, 12, 54, 224, 50, color_surface_elevated, 12);
  vgrad(st, color_surface_elevated, color_surface_raised);
  hairline(st, color_text_tertiary, opa_border_medium);
  lv_obj_t *stt = lbl(st, "Standard", font_body, color_text_primary, 0, 0);
  lv_obj_align(stt, LV_ALIGN_TOP_LEFT, 14, 8);
  lv_obj_t *sts = lbl(st, "machine auto-cals", font_micro, color_text_secondary, 0, 0);
  lv_obj_align(sts, LV_ALIGN_BOTTOM_LEFT, 14, -8);
  if (h) h->standard = st;

  lv_obj_t *om = card(parent, 244, 54, 224, 50, color_surface_base, 12);
  vgrad(om, color_surface_elevated, color_surface_base);
  lv_obj_set_style_border_color(om, color_accent_primary, 0);
  lv_obj_set_style_border_width(om, 2, 0);
  lv_obj_set_style_border_opa(om, LV_OPA_COVER, 0);
  soft_shadow(om, color_accent_primary, 10, LV_OPA_40);
  lv_obj_t *omt = lbl(om, "Full Calibration", font_body, color_accent_primary, 0, 0);
  lv_obj_align(omt, LV_ALIGN_TOP_LEFT, 14, 8);
  lv_obj_t *oms = lbl(om, "complete tuning suite", font_micro, color_text_secondary, 0, 0);
  lv_obj_align(oms, LV_ALIGN_BOTTOM_LEFT, 14, -8);
  if (h) h->omega = om;

  // ---- individual calibrations ----
  lbl(parent, "INDIVIDUAL", font_micro, color_text_secondary, 12, 112);
  const char *cals[5] = {"Bed Mesh", "Pressure Adv", "Flow", "Input Shaper", "Z-Offset"};
  for (int i = 0; i < 5; i++) {
    int col = i % 3, row = i / 3;
    int x = 12 + col * 154;
    int y = 128 + row * 44;
    lv_obj_t *t = card(parent, x, y, 146, 38, color_surface_elevated, 10);
    vgrad(t, color_surface_elevated, color_surface_raised);
    hairline(t, color_text_tertiary, opa_border_subtle);
    lv_obj_t *nl = lbl(t, cals[i], font_caption, color_text_primary, 0, 0);
    lv_obj_align(nl, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_t *ch = lbl(t, LV_SYMBOL_RIGHT, ms, color_text_tertiary, 0, 0);
    lv_obj_align(ch, LV_ALIGN_RIGHT_MID, -8, 0);
    if (h) h->cals[i] = t;
  }

  // ---- live speed slider ----
  lbl(parent, "SPEED", font_micro, color_text_secondary, 12, 224);
  lv_obj_t *spv = lbl(parent, "100%", font_num_small, color_accent_primary, 0, 0);
  lv_obj_align(spv, LV_ALIGN_TOP_RIGHT, -14, 222);
  if (h) h->speed_val = spv;
  lv_obj_t *sl = lv_slider_create(parent);
  lv_obj_set_pos(sl, 12, 244);
  lv_obj_set_size(sl, 456, 10);
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
  if (h) h->speed = sl;
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

// Tappable value field on the right of a row (tap -> keypad in the app).
// Returns the pill so the app can wire the tap and update the value text
// (the value label is the pill's first child).
static lv_obj_t *value_pill(lv_obj_t *row, const char *val) {
  lv_obj_t *p = lv_obj_create(row);
  lv_obj_remove_style_all(p);
  lv_obj_set_size(p, 80, 26);
  lv_obj_align(p, LV_ALIGN_RIGHT_MID, -8, 0);
  lv_obj_set_style_bg_color(p, color_surface_base, 0);
  lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(p, 6, 0);
  hairline(p, color_accent_primary, opa_border_medium);
  lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(p, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t *v = lbl(p, val, font_caption, color_accent_primary, 0, 0);
  lv_obj_center(v);
  return p;
}

// Set the value text on a pill returned by value_pill (label is child 0).
void pill_set(lv_obj_t *pill, const char *txt) {
  if (!pill) return;
  lv_obj_t *v = lv_obj_get_child(pill, 0);
  if (v) { lv_label_set_text(v, txt); lv_obj_center(v); }
}

// A row of three tappable preset chips (quick values beside the keypad).
static void chip_row(lv_obj_t *list, const char *a, const char *b, const char *c, lv_obj_t *out[3]) {
  lv_obj_t *r = lv_obj_create(list);
  lv_obj_remove_style_all(r);
  lv_obj_set_size(r, lv_pct(100), 30);
  lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  const char *labs[3] = {a, b, c};
  for (int i = 0; i < 3; i++) {
    lv_obj_t *ch = lv_obj_create(r);
    lv_obj_remove_style_all(ch);
    lv_obj_set_size(ch, 140, 30);
    vgrad(ch, color_surface_elevated, color_surface_raised);
    lv_obj_set_style_radius(ch, 7, 0);
    hairline(ch, color_text_tertiary, opa_border_subtle);
    lv_obj_clear_flag(ch, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *l = lbl(ch, labs[i], font_micro, color_accent_primary, 0, 0);
    lv_obj_center(l);
    out[i] = ch;
  }
}

void build_settings(lv_obj_t *parent, SettingsHandles *h) {
  lv_obj_t *back = screen_header(parent, "Expert Tune");
  if (h) h->back = back;

  lv_obj_t *list = lv_obj_create(parent);
  lv_obj_remove_style_all(list);
  lv_obj_set_pos(list, 12, 54);
  lv_obj_set_size(list, 456, 210);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(list, 7, 0);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);
  lv_obj_set_style_bg_color(list, color_accent_primary, LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(list, LV_OPA_40, LV_PART_SCROLLBAR);
  lv_obj_set_style_width(list, 3, LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(list, 2, LV_PART_SCROLLBAR);

  // Speed factor (M220) + quick presets
  { lv_obj_t *p = value_pill(setting_row(list, "Speed factor"), "100%"); if (h) h->speed = p; }
  { lv_obj_t *c[3]; chip_row(list, "50%", "100%", "150%", c); if (h) { h->speed_p[0] = c[0]; h->speed_p[1] = c[1]; h->speed_p[2] = c[2]; } }

  // Flow factor (M221) + quick presets
  { lv_obj_t *p = value_pill(setting_row(list, "Flow factor"), "100%"); if (h) h->flow = p; }
  { lv_obj_t *c[3]; chip_row(list, "95%", "100%", "105%", c); if (h) { h->flow_p[0] = c[0]; h->flow_p[1] = c[1]; h->flow_p[2] = c[2]; } }

  // Z-offset (live babystep via SET_GCODE_OFFSET): [-] value [+], value also keypad-tappable
  {
    lv_obj_t *r = setting_row(list, "Z-offset");
    lv_obj_t *pls = card(r, 0, 0, 34, 28, color_surface_elevated, 7);
    vgrad(pls, color_surface_elevated, color_surface_raised);
    hairline(pls, color_text_tertiary, opa_border_subtle);
    lv_obj_align(pls, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_center(lbl(pls, LV_SYMBOL_PLUS, &lv_font_montserrat_14, color_accent_primary, 0, 0));
    lv_obj_t *pv = value_pill(r, "0.000");
    lv_obj_set_width(pv, 68);
    lv_obj_align(pv, LV_ALIGN_RIGHT_MID, -48, 0);
    lv_obj_t *mns = card(r, 0, 0, 34, 28, color_surface_elevated, 7);
    vgrad(mns, color_surface_elevated, color_surface_raised);
    hairline(mns, color_text_tertiary, opa_border_subtle);
    lv_obj_align(mns, LV_ALIGN_RIGHT_MID, -122, 0);
    lv_obj_center(lbl(mns, LV_SYMBOL_MINUS, &lv_font_montserrat_14, color_accent_primary, 0, 0));
    if (h) { h->zoff = pv; h->zoff_plus = pls; h->zoff_minus = mns; }
  }

  // Pressure advance (SET_PRESSURE_ADVANCE)
  { lv_obj_t *p = value_pill(setting_row(list, "Pressure advance"), "0.040"); if (h) h->pa = p; }

  // Part fan (M106) + quick presets
  { lv_obj_t *p = value_pill(setting_row(list, "Part fan"), "0%"); if (h) h->fan = p; }
  { lv_obj_t *c[3]; chip_row(list, "Off", "50%", "Full", c); if (h) { h->fan_p[0] = c[0]; h->fan_p[1] = c[1]; h->fan_p[2] = c[2]; } }
}

// ============================================================================
// Native sub-screens (replace the legacy guppyscreen panels). Each is a full
// 480x272 screen built on the same grid + tokens as the cockpit, with a back
// chip top-left. The app shows/hides them over the cockpit and wires actions.
// ============================================================================

// Shared chrome: depth backdrop + top bar (back chip + title + divider).
// Returns the back chip so the caller wires "return to cockpit".
static lv_obj_t *screen_header(lv_obj_t *parent, const char *title) {
  const lv_font_t *ms = &lv_font_montserrat_14;
  lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(parent, 0, 0);
  lv_obj_set_style_bg_color(parent, lv_color_hex(0x0b1220), 0);
  lv_obj_set_style_bg_grad_color(parent, lv_color_hex(0x070b12), 0);
  lv_obj_set_style_bg_grad_dir(parent, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
  lv_obj_t *back = card(parent, 12, 10, 42, 28, color_surface_elevated, 10);
  vgrad(back, color_surface_elevated, color_surface_raised);
  hairline(back, color_text_tertiary, opa_border_medium);
  lv_obj_t *bi = lbl(back, LV_SYMBOL_LEFT, ms, color_accent_primary, 0, 0);
  lv_obj_center(bi);
  lbl(parent, title, font_h2, color_text_primary, 64, 13);
  card(parent, 12, 44, 456, 1, color_text_tertiary, 0, opa_border_subtle);
  return back;
}

// Generic tappable card-button with a centered label.
static lv_obj_t *tap_btn(lv_obj_t *p, int x, int y, int w, int h,
                         const char *txt, const lv_font_t *f, lv_color_t tc) {
  lv_obj_t *b = card(p, x, y, w, h, color_surface_elevated, 10);
  vgrad(b, color_surface_elevated, color_surface_raised);
  hairline(b, color_text_tertiary, opa_border_medium);
  lv_obj_t *l = lbl(b, txt, f, tc, 0, 0);
  lv_obj_center(l);
  return b;
}

void build_move(lv_obj_t *parent, MoveHandles *h) {
  const lv_font_t *ms = &lv_font_montserrat_14;
  lv_obj_t *back = screen_header(parent, "Move");
  if (h) h->back = back;

  const int bs = 50;
  // ---- XY jog cross (left) ----
  const int cx = 78, cy = 112;
  lv_obj_t *yp = tap_btn(parent, cx, cy - bs - 8, bs, bs, LV_SYMBOL_UP, ms, color_text_primary);
  lv_obj_t *ym = tap_btn(parent, cx, cy + bs + 8, bs, bs, LV_SYMBOL_DOWN, ms, color_text_primary);
  lv_obj_t *xm = tap_btn(parent, cx - bs - 8, cy, bs, bs, LV_SYMBOL_LEFT, ms, color_text_primary);
  lv_obj_t *xp = tap_btn(parent, cx + bs + 8, cy, bs, bs, LV_SYMBOL_RIGHT, ms, color_text_primary);
  lv_obj_t *hxy = tap_btn(parent, cx, cy, bs, bs, LV_SYMBOL_HOME, ms, color_accent_primary);
  if (h) { h->xplus = xp; h->xminus = xm; h->yplus = yp; h->yminus = ym; h->home_xy = hxy; }

  // ---- Z jog ----
  const int zx = 212;
  lv_obj_t *zp = tap_btn(parent, zx, cy - bs - 8, bs, bs, LV_SYMBOL_UP, ms, color_text_primary);
  lv_obj_t *zm = tap_btn(parent, zx, cy + bs + 8, bs, bs, LV_SYMBOL_DOWN, ms, color_text_primary);
  lbl(parent, "Z", font_num_small, color_text_secondary, zx + 20, cy + 16);
  if (h) { h->zplus = zp; h->zminus = zm; }

  // ---- step selector ----
  lbl(parent, "step (mm)", font_micro, color_text_tertiary, 282, 40);
  const char *steps[4] = {"0.1", "1", "10", "100"};
  for (int i = 0; i < 4; i++) {
    int sw = 44, sx = 280 + i * (sw + 4);
    bool on = (i == 1);
    lv_obj_t *sb = card(parent, sx, 54, sw, 32, on ? color_accent_primary : color_surface_elevated, 8);
    if (!on) { vgrad(sb, color_surface_elevated, color_surface_raised); hairline(sb, color_text_tertiary, opa_border_subtle); }
    lv_obj_t *sl = lbl(sb, steps[i], font_caption, on ? color_surface_base : color_text_secondary, 0, 0);
    lv_obj_center(sl);
    if (h) h->step[i] = sb;
  }

  // ---- home all + motors off ----
  lv_obj_t *ha = card(parent, 280, 96, 188, 46, color_accent_primary, 10);
  vgrad(ha, lv_color_hex(0x33eaff), lv_color_hex(0x00b3cc));
  lv_obj_t *hal = lbl(ha, LV_SYMBOL_HOME "  Home All", ms, color_surface_base, 0, 0);
  lv_obj_center(hal);
  lv_obj_t *mo = tap_btn(parent, 280, 150, 188, 46, "Motors Off", font_body, color_text_secondary);
  if (h) { h->home_all = ha; h->motors_off = mo; }

  // ---- position readout ----
  lv_obj_t *pc = card(parent, 280, 204, 188, 44, color_surface_base, 10);
  hairline(pc, color_text_tertiary, opa_border_subtle);
  lv_obj_t *pl = lbl(pc, "X --  Y --  Z --", font_caption, color_text_secondary, 0, 0);
  lv_obj_center(pl);
  if (h) h->pos = pl;
}

void build_filament(lv_obj_t *parent, FilamentHandles *h) {
  const lv_font_t *ms = &lv_font_montserrat_14;
  lv_obj_t *back = screen_header(parent, "Filament");
  if (h) h->back = back;

  // Live nozzle temp rides the header line (right side) - frees a full row so
  // material select, load length, and the action pairs all keep 44pt targets.
  lv_obj_t *tc = lv_obj_create(parent);
  lv_obj_remove_style_all(tc);
  lv_obj_set_pos(tc, 250, 6);
  lv_obj_set_size(tc, 218, 34);
  lv_obj_clear_flag(tc, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *tn = lbl(tc, "nozzle", font_micro, color_text_tertiary, 0, 0);
  lv_obj_align(tn, LV_ALIGN_LEFT_MID, 60, 0);
  lv_obj_t *tv = lbl(tc, "-- / --", font_num_small, color_text_primary, 0, 0);
  lv_obj_align(tv, LV_ALIGN_RIGHT_MID, -14, 0);
  if (h) h->temp = tv;

  // Material select (doubles as preheat): the picked tier is the temp Load /
  // Unload run at. seg_highlight marks the active material; Off cools down.
  const char *pn[3] = {"PLA", "PETG", "PA-CF"};
  for (int i = 0; i < 3; i++) {
    lv_obj_t *pb = tap_btn(parent, 12 + i * 116, 52, 108, 44, pn[i], font_caption, color_text_secondary);
    if (h) h->preset[i] = pb;
  }
  lv_obj_t *cd = tap_btn(parent, 360, 52, 108, 44, "Off", font_caption, color_text_secondary);
  if (h) h->cooldown = cd;

  // Load length: slider + live mm readout (used by Load; Extrude/Retract keep
  // their fixed 25mm purge).
  lbl(parent, "LOAD LENGTH", font_micro, color_text_secondary, 12, 106);
  lv_obj_t *lval = lbl(parent, "200 mm", font_num_small, color_accent_primary, 0, 0);
  lv_obj_align(lval, LV_ALIGN_TOP_RIGHT, -14, 104);
  lv_obj_t *sl = lv_slider_create(parent);
  lv_obj_set_pos(sl, 12, 128);
  lv_obj_set_size(sl, 456, 10);
  lv_slider_set_range(sl, 50, 300);
  lv_slider_set_value(sl, 200, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(sl, color_surface_elevated, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(sl, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(sl, 5, LV_PART_MAIN);
  lv_obj_set_style_bg_color(sl, color_accent_primary, LV_PART_INDICATOR);
  lv_obj_set_style_radius(sl, 5, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(sl, color_accent_primary, LV_PART_KNOB);
  lv_obj_set_style_shadow_color(sl, color_accent_primary, LV_PART_KNOB);
  lv_obj_set_style_shadow_width(sl, 12, LV_PART_KNOB);
  lv_obj_set_style_shadow_opa(sl, LV_OPA_50, LV_PART_KNOB);
  lv_obj_set_ext_click_area(sl, 16);  // thin track, fat finger
  if (h) { h->len_slider = sl; h->len_val = lval; }

  // load / unload (primary)
  lv_obj_t *ld = card(parent, 12, 152, 224, 52, color_accent_primary, 10);
  vgrad(ld, lv_color_hex(0x33eaff), lv_color_hex(0x00b3cc));
  lv_obj_t *ldl = lbl(ld, LV_SYMBOL_DOWN "  Load", ms, color_surface_base, 0, 0);
  lv_obj_center(ldl);
  lv_obj_t *ul = tap_btn(parent, 244, 152, 224, 52, LV_SYMBOL_UP "  Unload", ms, color_text_primary);
  if (h) { h->load = ld; h->unload = ul; }

  // extrude / retract
  lv_obj_t *ex = tap_btn(parent, 12, 212, 224, 44, "Extrude 25", font_body, color_text_primary);
  lv_obj_t *rt = tap_btn(parent, 244, 212, 224, 44, "Retract 25", font_body, color_text_primary);
  if (h) { h->extrude = ex; h->retract = rt; }
}

// one temperature column (nozzle or bed): current, target, 3 presets, Off.
// out = [cur, tgt, b0, b1, b2, off].
static void temp_section(lv_obj_t *parent, int x, int w, const char *name,
                         const char *p0, const char *p1, const char *p2,
                         lv_obj_t *out[8]) {
  lv_obj_t *c = card(parent, x, 52, w, 196, color_surface_raised, 14);
  vgrad(c, color_surface_elevated, color_surface_raised);
  hairline(c, color_text_tertiary, opa_border_subtle);
  lv_obj_t *nm = lbl(c, name, font_caption, color_text_secondary, 0, 0);
  lv_obj_align(nm, LV_ALIGN_TOP_MID, 0, 10);
  lv_obj_t *cv = lbl(c, "--", font_num_large, color_text_primary, 0, 0);
  lv_obj_align(cv, LV_ALIGN_TOP_MID, 0, 28);
  lv_obj_add_flag(cv, LV_OBJ_FLAG_CLICKABLE);   // tap the number to type an exact target
  lv_obj_set_ext_click_area(cv, 18);            // fat-finger touch target
  // manual -/+ steppers flanking the live target value (the "set temp" control)
  lv_obj_t *mn = card(c, 10, 68, 46, 34, color_surface_elevated, 8);
  vgrad(mn, color_surface_elevated, color_surface_raised);
  hairline(mn, color_text_tertiary, opa_border_subtle);
  lv_obj_t *mnl = lbl(mn, LV_SYMBOL_MINUS, &lv_font_montserrat_14, color_accent_primary, 0, 0);
  lv_obj_center(mnl);
  lv_obj_t *ps_btn = card(c, w - 10 - 46, 68, 46, 34, color_surface_elevated, 8);
  vgrad(ps_btn, color_surface_elevated, color_surface_raised);
  hairline(ps_btn, color_text_tertiary, opa_border_subtle);
  lv_obj_t *psl = lbl(ps_btn, LV_SYMBOL_PLUS, &lv_font_montserrat_14, color_accent_primary, 0, 0);
  lv_obj_center(psl);
  lv_obj_t *tv = lbl(c, "off", font_caption, color_accent_primary, 0, 0);
  lv_obj_align(tv, LV_ALIGN_TOP_MID, 0, 76);
  out[0] = cv; out[1] = tv; out[6] = mn; out[7] = ps_btn;
  const char *ps[3] = {p0, p1, p2};
  int pw = (w - 24 - 2 * 6) / 3;
  for (int i = 0; i < 3; i++) {
    lv_obj_t *pb = card(c, 12 + i * (pw + 6), 110, pw, 34, color_surface_elevated, 8);
    vgrad(pb, color_surface_elevated, color_surface_raised);
    hairline(pb, color_text_tertiary, opa_border_subtle);
    lv_obj_t *lab = lbl(pb, ps[i], font_micro, color_accent_primary, 0, 0);
    lv_obj_center(lab);
    out[2 + i] = pb;
  }
  lv_obj_t *ob = card(c, 12, 152, w - 24, 32, color_surface_base, 8);
  hairline(ob, color_state_error, opa_border_medium);
  lv_obj_t *ol = lbl(ob, "Off", font_caption, color_state_error, 0, 0);
  lv_obj_center(ol);
  out[5] = ob;
}

void build_temps(lv_obj_t *parent, TempsHandles *h) {
  lv_obj_t *back = screen_header(parent, "Temperature");
  if (h) h->back = back;
  lv_obj_t *nz[8], *bd[8];
  temp_section(parent, 12, 224, "NOZZLE", "PLA 220", "PETG 240", "PA 260", nz);
  temp_section(parent, 244, 224, "BED", "PLA 60", "PETG 80", "PA 75", bd);
  if (h) {
    h->nz_cur = nz[0]; h->nz_tgt = nz[1];
    h->nz_preset[0] = nz[2]; h->nz_preset[1] = nz[3]; h->nz_preset[2] = nz[4]; h->nz_off = nz[5];
    h->nz_minus = nz[6]; h->nz_plus = nz[7];
    h->bd_cur = bd[0]; h->bd_tgt = bd[1];
    h->bd_preset[0] = bd[2]; h->bd_preset[1] = bd[3]; h->bd_preset[2] = bd[4]; h->bd_off = bd[5];
    h->bd_minus = bd[6]; h->bd_plus = bd[7];
  }
}

// ---- Bed mesh heatmap -------------------------------------------------------
// 5-stop colormap blue->cyan->green->yellow->red across t in [0,1].
static lv_color_t heat_color(float t) {
  if (t < 0.f) t = 0.f;
  if (t > 1.f) t = 1.f;
  float r, g, b;
  if (t < 0.25f)      { float u = t / 0.25f;           r = 0;     g = u;     b = 1; }
  else if (t < 0.5f)  { float u = (t - 0.25f) / 0.25f; r = 0;     g = 1;     b = 1 - u; }
  else if (t < 0.75f) { float u = (t - 0.5f) / 0.25f;  r = u;     g = 1;     b = 0; }
  else                { float u = (t - 0.75f) / 0.25f; r = 1;     g = 1 - u; b = 0; }
  return lv_color_make((uint8_t)(r * 255), (uint8_t)(g * 255), (uint8_t)(b * 255));
}

void mesh_render(lv_obj_t *grid, const float *z, int rows, int cols, float zmin, float zmax) {
  if (!grid || !z || rows < 1 || cols < 1) return;
  lv_obj_clean(grid);
  int gw = lv_obj_get_width(grid);  if (gw <= 0) gw = 192;
  int gh = lv_obj_get_height(grid); if (gh <= 0) gh = 192;
  float range = zmax - zmin; if (range < 1e-6f) range = 1e-6f;
  int cw = gw / cols, ch = gh / rows;
  for (int r = 0; r < rows; r++)
    for (int c = 0; c < cols; c++) {
      float t = (z[r * cols + c] - zmin) / range;
      lv_obj_t *cell = lv_obj_create(grid);
      lv_obj_remove_style_all(cell);
      lv_obj_set_size(cell, cw + 1, ch + 1);
      lv_obj_set_pos(cell, c * cw, (rows - 1 - r) * ch);  // front row at the bottom
      lv_obj_set_style_bg_color(cell, heat_color(t), 0);
      lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
      lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    }
}

void build_mesh(lv_obj_t *parent, MeshHandles *h) {
  lv_obj_t *back = screen_header(parent, "Bed Mesh");
  if (h) h->back = back;

  lv_obj_t *grid = lv_obj_create(parent);
  lv_obj_remove_style_all(grid);
  lv_obj_set_pos(grid, 14, 60);
  lv_obj_set_size(grid, 192, 192);
  lv_obj_set_style_bg_color(grid, color_surface_base, 0);
  lv_obj_set_style_bg_opa(grid, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(grid, 6, 0);
  lv_obj_set_style_clip_corner(grid, true, 0);
  hairline(grid, color_text_tertiary, opa_border_medium);
  lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
  if (h) h->grid = grid;

  lv_obj_t *pf = lbl(parent, "Profile: default", font_caption, color_text_primary, 220, 66);
  if (h) h->profile = pf;
  lv_obj_t *rg = lbl(parent, "Range: --", font_micro, color_text_secondary, 220, 92);
  if (h) h->range = rg;

  lv_obj_t *bar = lv_obj_create(parent);  // legend: red (high) top -> blue (low) bottom
  lv_obj_remove_style_all(bar);
  lv_obj_set_pos(bar, 220, 132);
  lv_obj_set_size(bar, 22, 104);
  lv_obj_set_style_bg_color(bar, lv_color_make(255, 0, 0), 0);
  lv_obj_set_style_bg_grad_color(bar, lv_color_make(0, 80, 255), 0);
  lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(bar, 4, 0);
  lbl(parent, "high", font_micro, color_text_secondary, 250, 132);
  lbl(parent, "low", font_micro, color_text_secondary, 250, 222);

  // demo surface so the sim and first boot show a heatmap before a real probe
  static const float demo[49] = {
    0.05f, 0.03f, 0.00f,-0.02f, 0.00f, 0.03f, 0.06f,
    0.03f, 0.01f,-0.02f,-0.04f,-0.02f, 0.01f, 0.04f,
    0.00f,-0.02f,-0.05f,-0.07f,-0.05f,-0.02f, 0.01f,
   -0.02f,-0.04f,-0.07f,-0.09f,-0.07f,-0.03f, 0.00f,
    0.00f,-0.02f,-0.05f,-0.07f,-0.04f,-0.01f, 0.02f,
    0.03f, 0.01f,-0.02f,-0.03f,-0.01f, 0.02f, 0.05f,
    0.06f, 0.04f, 0.01f, 0.00f, 0.02f, 0.05f, 0.08f };
  mesh_render(grid, demo, 7, 7, -0.09f, 0.08f);
}

void build_more(lv_obj_t *parent, MoreHandles *h) {
  lv_obj_t *back = screen_header(parent, "More");
  if (h) h->back = back;

  lv_obj_t *list = lv_obj_create(parent);
  lv_obj_remove_style_all(list);
  lv_obj_set_pos(list, 12, 56);
  lv_obj_set_size(list, 456, 206);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(list, 8, 0);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);
  lv_obj_set_style_bg_color(list, color_accent_primary, LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(list, LV_OPA_40, LV_PART_SCROLLBAR);
  lv_obj_set_style_width(list, 3, LV_PART_SCROLLBAR);

  auto row = [&](const char *icon, const char *title, const char *sub) -> lv_obj_t * {
    lv_obj_t *r = lv_obj_create(list);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, lv_pct(100), 58);
    vgrad(r, color_surface_elevated, color_surface_raised);
    lv_obj_set_style_radius(r, 12, 0);
    hairline(r, color_text_tertiary, opa_border_subtle);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *ic = lbl(r, icon, &lv_font_montserrat_14, color_accent_primary, 0, 0);
    lv_obj_align(ic, LV_ALIGN_LEFT_MID, 16, 0);
    lv_obj_t *tl = lbl(r, title, font_body, color_text_primary, 0, 0);
    lv_obj_align(tl, LV_ALIGN_LEFT_MID, 48, -9);
    lv_obj_t *sl = lbl(r, sub, font_micro, color_text_secondary, 0, 0);
    lv_obj_align(sl, LV_ALIGN_LEFT_MID, 48, 10);
    lv_obj_t *ch = lbl(r, LV_SYMBOL_RIGHT, &lv_font_montserrat_14, color_text_tertiary, 0, 0);
    lv_obj_align(ch, LV_ALIGN_RIGHT_MID, -14, 0);
    return r;
  };

  lv_obj_t *w  = row(LV_SYMBOL_WIFI, "Wi-Fi & Network", "Scan and connect");
  lv_obj_t *m  = row(LV_SYMBOL_IMAGE, "Bed Mesh", "Live probed surface heatmap");
  lv_obj_t *e  = row(LV_SYMBOL_SETTINGS, "Expert Tune", "Live print tuning");
  lv_obj_t *li = row(LV_SYMBOL_CHARGE, "Lights", "Case + hotend LEDs");
  lv_obj_t *sy = row(LV_SYMBOL_LIST, "System", "Version, network, uptime");
  lv_obj_t *pw = row(LV_SYMBOL_POWER, "Power", "Restart, reboot, shutdown");
  if (h) { h->wifi = w; h->mesh = m; h->expert = e; h->led = li; h->system = sy; h->power = pw; }
}

void build_system(lv_obj_t *parent, SystemHandles *h) {
  lv_obj_t *back = screen_header(parent, "System");
  if (h) h->back = back;
  lv_obj_t *list = lv_obj_create(parent);
  lv_obj_remove_style_all(list);
  lv_obj_set_pos(list, 12, 58);
  lv_obj_set_size(list, 456, 200);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(list, 7, 0);
  lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLLABLE);
  auto inforow = [&](const char *name) -> lv_obj_t * {
    lv_obj_t *r = lv_obj_create(list);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, lv_pct(100), 34);
    vgrad(r, color_surface_elevated, color_surface_raised);
    lv_obj_set_style_radius(r, 8, 0);
    hairline(r, color_text_tertiary, opa_border_subtle);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *n = lbl(r, name, font_caption, color_text_secondary, 0, 0);
    lv_obj_align(n, LV_ALIGN_LEFT_MID, 14, 0);
    lv_obj_t *v = lbl(r, "--", font_caption, color_text_primary, 0, 0);
    lv_obj_align(v, LV_ALIGN_RIGHT_MID, -14, 0);
    return v;
  };
  lv_obj_t *fw = inforow("Firmware");
  lv_obj_t *hn = inforow("Hostname");
  lv_obj_t *ip = inforow("IP address");
  lv_obj_t *up = inforow("Uptime");
  lv_obj_t *mc = inforow("CPU temp");
  if (h) { h->version = fw; h->host = hn; h->ip = ip; h->uptime = up; h->mcu = mc; }
}

void build_power(lv_obj_t *parent, PowerHandles *h) {
  lv_obj_t *back = screen_header(parent, "Power");
  if (h) h->back = back;
  lv_obj_t *rk = tap_btn(parent, 12, 64, 224, 86, "Restart Klipper", font_body, color_text_primary);
  lv_obj_t *rf = tap_btn(parent, 244, 64, 224, 86, "Restart Firmware", font_body, color_text_primary);
  lv_obj_t *rb = card(parent, 12, 160, 224, 86, color_state_warning, 12);
  vgrad(rb, lv_color_hex(0xffc04a), lv_color_hex(0xff9e1b));
  lv_obj_center(lbl(rb, "Reboot", font_body, color_surface_base, 0, 0));
  lv_obj_t *sd = card(parent, 244, 160, 224, 86, color_state_error, 12);
  vgrad(sd, lv_color_hex(0xff5a5a), lv_color_hex(0xd83232));
  lv_obj_center(lbl(sd, "Shutdown", font_body, color_surface_base, 0, 0));
  if (h) { h->restart_klipper = rk; h->restart_fw = rf; h->reboot = rb; h->shutdown = sd; }
}

void seg_highlight(lv_obj_t *const *btns, int n, int active) {
  for (int i = 0; i < n; i++) {
    if (!btns[i]) continue;
    bool on = (i == active);
    lv_obj_set_style_bg_color(btns[i], on ? lv_color_hex(0x33eaff) : color_surface_elevated, 0);
    lv_obj_set_style_bg_grad_color(btns[i], on ? lv_color_hex(0x00b3cc) : color_surface_raised, 0);
    lv_obj_t *l = lv_obj_get_child(btns[i], 0);
    if (l) lv_obj_set_style_text_color(l, on ? color_surface_base : color_text_secondary, 0);
  }
}

void build_confirm(lv_obj_t *parent, ConfirmHandles *h) {
  // Scrim first -> lower z than the card; both live on lv_layer_top, hidden
  // until the app shows them before a destructive action.
  lv_obj_t *scrim = lv_obj_create(parent);
  lv_obj_remove_style_all(scrim);
  lv_obj_set_size(scrim, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(scrim, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(scrim, LV_OPA_50, 0);
  lv_obj_add_flag(scrim, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(scrim, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *cd = card(parent, 70, 71, 340, 130, color_surface_raised, 14);  // centered on 480x272
  vgrad(cd, color_surface_elevated, color_surface_raised);
  lv_obj_set_style_border_color(cd, color_accent_primary, 0);
  lv_obj_set_style_border_width(cd, 1, 0);
  lv_obj_set_style_border_opa(cd, LV_OPA_70, 0);
  lv_obj_set_style_shadow_color(cd, lv_color_black(), 0);
  lv_obj_set_style_shadow_width(cd, 24, 0);
  lv_obj_set_style_shadow_opa(cd, LV_OPA_50, 0);
  lv_obj_add_flag(cd, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *msg = lbl(cd, "Are you sure?", font_body, color_text_primary, 0, 0);
  lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(msg, 308);
  lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(msg, LV_ALIGN_TOP_MID, 0, 22);

  lv_obj_t *cancel = tap_btn(cd, 16, 76, 150, 40, "Cancel", font_body, color_text_primary);
  lv_obj_t *confirm = card(cd, 174, 76, 150, 40, color_state_error, 10);
  vgrad(confirm, lv_color_hex(0xff5a5f), lv_color_hex(0xd92d36));
  lv_obj_center(lbl(confirm, "Confirm", font_body, color_surface_base, 0, 0));

  if (h) { h->scrim = scrim; h->card = cd; h->msg = msg; h->cancel = cancel; h->confirm = confirm; }
}

void build_lights(lv_obj_t *parent, LightsHandles *h) {
  lv_obj_t *back = screen_header(parent, "Lights");
  if (h) h->back = back;
  auto section = [&](int y, const char *name, lv_obj_t **off, lv_obj_t **mid, lv_obj_t **full) {
    lv_obj_t *c = card(parent, 12, y, 456, 88, color_surface_raised, 14);
    vgrad(c, color_surface_elevated, color_surface_raised);
    hairline(c, color_text_tertiary, opa_border_subtle);
    lv_obj_t *nm = lbl(c, name, font_body, color_text_primary, 0, 0);
    lv_obj_align(nm, LV_ALIGN_TOP_LEFT, 16, 12);
    const int bw = 134, bh = 38, gap = 8, x0 = 16, by = 40;
    *off = tap_btn(c, x0, by, bw, bh, "Off", font_caption, color_text_secondary);
    *mid = tap_btn(c, x0 + bw + gap, by, bw, bh, "50%", font_caption, color_text_primary);
    *full = tap_btn(c, x0 + 2 * (bw + gap), by, bw, bh, "Full", font_caption, color_text_primary);
  };
  lv_obj_t *co, *cm, *cf, *ho, *hm, *hf;
  section(56, "Case", &co, &cm, &cf);
  section(150, "Hotend", &ho, &hm, &hf);
  // Default highlight to Full (boot state). The app moves it on tap and re-
  // applies the tracked level when the screen is reopened.
  lv_obj_t *cb[3] = {co, cm, cf}; seg_highlight(cb, 3, 2);
  lv_obj_t *hb[3] = {ho, hm, hf}; seg_highlight(hb, 3, 2);
  if (h) { h->case_off = co; h->case_50 = cm; h->case_full = cf;
           h->hot_off = ho; h->hot_50 = hm; h->hot_full = hf; }
}

void build_fans(lv_obj_t *parent, FansHandles *h) {
  lv_obj_t *back = screen_header(parent, "Fans");
  if (h) h->back = back;

  // One row per fan. The three user-settable fans get a live slider (drag to 0
  // = off); the two Klipper-managed fans show an AUTO pill + live %.
  static const char *names[5]    = {"Part cooling", "Model fan", "Box fan", "Mainboard", "Hotend"};
  static const bool  settable[5] = {true, true, true, false, false};
  const int X = 12, W = 456, RH = 38, Y0 = 60, GAP = 4;
  for (int i = 0; i < 5; i++) {
    int y = Y0 + i * (RH + GAP);
    lv_obj_t *c = card(parent, X, y, W, RH, color_surface_raised, 10);
    hairline(c, color_text_tertiary, opa_border_subtle);
    lv_obj_t *nm = lbl(c, names[i], font_caption, color_text_secondary, 0, 0);
    lv_obj_align(nm, LV_ALIGN_LEFT_MID, 14, 0);
    // Fans read 0 at boot (Klipper zeroes every fan on restart, which is when
    // build_fans runs) and the consume() loop only updates a fan on a non-null
    // speed delta -- an idle fan never sends one, so seed 0% not "--%".
    lv_obj_t *pv = lbl(c, "0%", font_body, color_accent_primary, 0, 0);
    lv_obj_align(pv, LV_ALIGN_RIGHT_MID, -14, 0);
    if (h) h->val[i] = pv;
    if (settable[i]) {
      lv_obj_t *sl = lv_slider_create(c);
      lv_obj_set_size(sl, 188, 8);
      lv_obj_align(sl, LV_ALIGN_CENTER, 30, 0);
      lv_slider_set_range(sl, 0, 100);
      lv_slider_set_value(sl, 0, LV_ANIM_OFF);
      lv_obj_set_style_bg_color(sl, color_surface_base, LV_PART_MAIN);
      lv_obj_set_style_radius(sl, 4, LV_PART_MAIN);
      lv_obj_set_style_bg_color(sl, color_accent_primary, LV_PART_INDICATOR);
      lv_obj_set_style_radius(sl, 4, LV_PART_INDICATOR);
      lv_obj_set_style_bg_color(sl, color_accent_primary, LV_PART_KNOB);
      if (h) h->slider[i] = sl;
    } else {
      lv_obj_t *pill = card(c, 0, 0, 50, 22, color_surface_elevated, 11);
      hairline(pill, color_accent_secondary, opa_border_strong);
      lv_obj_align(pill, LV_ALIGN_CENTER, 30, 0);
      lv_obj_center(lbl(pill, "AUTO", font_micro, color_accent_secondary, 0, 0));
    }
  }
}

// Append one file row to the Files list (public: the app populates real files).
void files_add_row(lv_obj_t *list, const char *name, const char *meta) {
  lv_obj_t *r = lv_obj_create(list);
  lv_obj_remove_style_all(r);
  lv_obj_set_size(r, lv_pct(100), 54);
  vgrad(r, color_surface_elevated, color_surface_raised);
  lv_obj_set_style_radius(r, 10, 0);
  hairline(r, color_text_tertiary, opa_border_subtle);
  lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
  // child 0: thumbnail image (hidden until the app loads one from metadata).
  // pivot 0,0 + pos so a zoom-to-fit lands the visual exactly in a 46px slot.
  lv_obj_t *th = lv_img_create(r);
  lv_obj_add_flag(th, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_radius(th, 6, 0);
  lv_obj_set_style_clip_corner(th, true, 0);
  lv_img_set_pivot(th, 0, 0);
  lv_obj_set_pos(th, 8, 4);
  // child 1: fallback file glyph (shown until a thumbnail replaces it)
  lv_obj_t *ic = lbl(r, LV_SYMBOL_FILE, &lv_font_montserrat_14, color_accent_primary, 0, 0);
  lv_obj_align(ic, LV_ALIGN_LEFT_MID, 18, 0);
  // child 2: name, child 3: meta
  lv_obj_t *nm = lbl(r, name, font_caption, color_text_primary, 0, 0);
  lv_obj_align(nm, LV_ALIGN_LEFT_MID, 62, -8);
  lv_obj_t *mt = lbl(r, meta, font_micro, color_text_secondary, 0, 0);
  lv_obj_align(mt, LV_ALIGN_LEFT_MID, 62, 10);
  // child 4: play
  lv_obj_t *pi = lbl(r, LV_SYMBOL_PLAY, &lv_font_montserrat_14, color_accent_secondary, 0, 0);
  lv_obj_align(pi, LV_ALIGN_RIGHT_MID, -14, 0);
}

// Apply async metadata to a file row: swap the glyph for the loaded thumbnail
// (child 0 img, child 1 glyph) and refresh the meta line (child 3).
void files_apply_meta(lv_obj_t *row, const char *thumb_path, int zoom, const char *meta) {
  if (!row) return;
  lv_obj_t *th = lv_obj_get_child(row, 0);
  lv_obj_t *ic = lv_obj_get_child(row, 1);
  lv_obj_t *mt = lv_obj_get_child(row, 3);
  if (th && thumb_path && thumb_path[0]) {
    lv_img_set_src(th, thumb_path);
    lv_img_set_pivot(th, 0, 0);
    if (zoom > 0) lv_img_set_zoom(th, zoom);
    lv_obj_set_pos(th, 8, 4);
    lv_obj_clear_flag(th, LV_OBJ_FLAG_HIDDEN);
    if (ic) lv_obj_add_flag(ic, LV_OBJ_FLAG_HIDDEN);
  }
  if (mt && meta && meta[0]) lv_label_set_text(mt, meta);
}

void build_files(lv_obj_t *parent, FilesHandles *h) {
  lv_obj_t *back = screen_header(parent, "Files");
  if (h) h->back = back;
  lv_obj_t *list = lv_obj_create(parent);
  lv_obj_remove_style_all(list);
  lv_obj_set_pos(list, 12, 52);
  lv_obj_set_size(list, 456, 206);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(list, 8, 0);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);
  lv_obj_set_style_bg_color(list, color_accent_primary, LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(list, LV_OPA_40, LV_PART_SCROLLBAR);
  lv_obj_set_style_width(list, 3, LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(list, 2, LV_PART_SCROLLBAR);
  if (h) h->list = list;
  // placeholder rows; the app clears + repopulates from Moonraker.
  files_add_row(list, "omega_cube.gcode", "18m  .  PA-CF");
  files_add_row(list, "benchy_0.25.gcode", "1h 12m  .  PA-CF");
  files_add_row(list, "bracket_v3.gcode", "42m  .  PLA");
  files_add_row(list, "phone_stand.gcode", "2h 04m  .  PETG");
}

// ---- boot / connecting screen ----------------------------------------------
// Static Hawaii flag hero + a cycling island joke + a real progress bar the
// app drives from the live connect stages. No ocean animation: a static hero
// cannot jank, and the honest progress is the motion. The "Pono Print"
// wordmark came off 2026-06-10 (Jack: "I just want the HI flag") - the flag
// IS the identity mark.
void build_boot(lv_obj_t *parent, BootHandles *h) {
  lv_obj_set_style_bg_color(parent, color_surface_base, 0);
  lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

  // Hawaii flag hero, framed, top-centre (dropped a little lower now that it
  // carries the top half alone).
  const int fw = pono_flag_w, fh = pono_flag_h;          // 192 x 96
  const int fx = (480 - fw) / 2, fy = 28;
  lv_obj_t *frame = card(parent, fx - 2, fy - 2, fw + 4, fh + 4, color_surface_elevated, 6);
  hairline(frame, color_text_tertiary, opa_border_medium);
  lv_obj_t *flag = lv_img_create(parent);
  lv_img_set_src(flag, &pono_flag);
  lv_obj_set_pos(flag, fx, fy);
  if (h) h->flag = flag;

  // Cycling island joke (the app rotates the text via the joke timer).
  lv_obj_t *joke = lv_label_create(parent);
  lv_obj_set_width(joke, lv_pct(84));
  lv_obj_set_height(joke, LV_SIZE_CONTENT);
  lv_label_set_long_mode(joke, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(joke, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(joke, color_text_secondary, 0);
  lv_obj_set_style_text_font(joke, font_caption, 0);
  lv_label_set_text(joke, "Warming up the trade winds...");
  lv_obj_align(joke, LV_ALIGN_TOP_MID, 0, 152);
  if (h) h->joke = joke;

  // Loading widget: a styled box (brand-cyan edge) holding the comet spinner on
  // the left and a real status line over a progress bar. The app feeds both
  // from the live connect stages, so the bar actually means something.
  const int bx = 60, by = 192, bw = 360, bh = 56;
  lv_obj_t *box = card(parent, bx, by, bw, bh, color_surface_raised, 12);
  vgrad(box, color_surface_elevated, color_surface_raised);
  lv_obj_set_style_border_color(box, color_accent_primary, 0);
  lv_obj_set_style_border_width(box, 1, 0);
  lv_obj_set_style_border_opa(box, LV_OPA_50, 0);

  // Comet spinner: the "still working" circle Jack wants kept. The shared asset
  // is 88px (the busy overlay's hero size); zoom this instance to ~44px so it
  // fits the widget. align_to the box: the 88px object box centres the ~44px
  // visual at box-left + 30.
  lv_obj_t *spin = spinner_create(parent, 1000);
  lv_img_set_zoom(spin, 128);                            // 88px -> 44px visual
  lv_obj_align_to(spin, box, LV_ALIGN_LEFT_MID, -14, 0);
  if (h) h->spinner = spin;

  lv_obj_t *st = lbl(box, "Waiting for Klipper to start...", font_caption, color_text_primary, 0, 0);
  lv_obj_align(st, LV_ALIGN_TOP_LEFT, 56, 10);
  if (h) h->status = st;

  lv_obj_t *bar = lv_bar_create(box);
  lv_obj_set_size(bar, bw - 56 - 20, 8);
  lv_obj_align(bar, LV_ALIGN_BOTTOM_LEFT, 56, -12);
  lv_bar_set_range(bar, 0, 100);
  lv_bar_set_value(bar, 4, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(bar, color_surface_base, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(bar, 4, LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, color_accent_primary, LV_PART_INDICATOR);
  lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR);
  if (h) h->bar = bar;

  // Dedication, pinned bottom (kept from the old boot screen).
  lv_obj_t *ded = lbl(parent, "For Elio and Io", font_micro, color_accent_primary, 0, 0);
  lv_obj_align(ded, LV_ALIGN_BOTTOM_MID, 0, -8);
}

void boot_set_progress(BootHandles *h, int pct, const char *stage) {
  if (!h) return;
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  if (h->bar) lv_bar_set_value(h->bar, pct, LV_ANIM_ON);
  if (h->status && stage) lv_label_set_text(h->status, stage);
}

} // namespace pono
