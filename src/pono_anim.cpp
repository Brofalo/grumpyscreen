// SPDX-License-Identifier: GPL-3.0-only
// pono_anim.cpp - canned animation helpers. See pono_anim.h.
#include "pono_anim.h"
#include "pono_theme.h"
#include <cstdio>  // sscanf: parse "OMEGA X/N" for the banner progress strip

namespace {
// Singleton "working" overlay, lazily built on lv_layer_top.
lv_obj_t *g_busy = nullptr;
lv_obj_t *g_busy_spinner = nullptr;
lv_obj_t *g_busy_label = nullptr;
}  // namespace

namespace pono {

lv_obj_t *spinner_create(lv_obj_t *parent, uint16_t period_ms) {
  lv_obj_t *a = lv_animimg_create(parent);
  // lv_animimg stores the frame count in an int8_t (pic_count); clamp so a
  // future frame-count bump past 127 can't wrap it negative and read OOB in the
  // index modulo. At the current 24 frames this is a no-op.
  uint8_t n = pono_spinner_frame_count > 127 ? 127 : pono_spinner_frame_count;
  lv_animimg_set_src(a, (const void **)pono_spinner_frames, n);
  lv_animimg_set_duration(a, period_ms);
  lv_animimg_set_repeat_count(a, LV_ANIM_REPEAT_INFINITE);
  lv_animimg_start(a);
  // lv_animimg is an lv_img; size to the frame so callers can align it.
  lv_obj_set_size(a, pono_spinner_frames[0]->header.w, pono_spinner_frames[0]->header.h);
  return a;
}

void busy_show(const char *text) {
  if (g_busy == nullptr) {
    g_busy = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_busy);
    lv_obj_set_size(g_busy, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(g_busy, color_surface_base, 0);
    lv_obj_set_style_bg_opa(g_busy, LV_OPA_90, 0);  // scrim swallows taps behind it
    lv_obj_clear_flag(g_busy, LV_OBJ_FLAG_SCROLLABLE);
    g_busy_spinner = spinner_create(g_busy, 1000);
    lv_obj_align(g_busy_spinner, LV_ALIGN_CENTER, 0, -16);
    g_busy_label = lv_label_create(g_busy);
    lv_obj_set_style_text_color(g_busy_label, color_text_primary, 0);
    lv_obj_set_style_text_font(g_busy_label, font_body, 0);
    lv_obj_set_style_text_align(g_busy_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_busy_label, LV_ALIGN_CENTER, 0, 50);
  }
  lv_label_set_text(g_busy_label, text != nullptr ? text : "Working");
  lv_animimg_start(g_busy_spinner);  // re-arm (busy_hide stopped it)
  lv_obj_clear_flag(g_busy, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(g_busy);
}

void busy_hide() {
  if (g_busy == nullptr) return;
  lv_anim_del(g_busy_spinner, nullptr);  // stop the loop -> zero cost while idle
  lv_obj_add_flag(g_busy, LV_OBJ_FLAG_HIDDEN);
}

namespace {
// Singleton OMEGA status banner, lazily built on lv_layer_top.
lv_obj_t *g_omega = nullptr;
lv_obj_t *g_omega_label = nullptr;
lv_obj_t *g_omega_bar = nullptr;  // campaign progress strip along the banner's bottom edge
}  // namespace

void omega_status_show(const char *text) {
  if (g_omega == nullptr) {
    g_omega = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_omega);
    lv_obj_set_size(g_omega, LV_PCT(100), 30);
    lv_obj_set_pos(g_omega, 0, 0);  // top strip over the wordmark row
    lv_obj_set_style_bg_color(g_omega, color_state_warning, 0);  // OMEGA amber
    lv_obj_set_style_bg_opa(g_omega, LV_OPA_COVER, 0);
    lv_obj_clear_flag(g_omega, LV_OBJ_FLAG_SCROLLABLE);
    g_omega_label = lv_label_create(g_omega);
    lv_obj_set_style_text_color(g_omega_label, color_surface_base, 0);  // dark on amber
    lv_obj_set_style_text_font(g_omega_label, font_body, 0);
    lv_obj_set_style_text_align(g_omega_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(g_omega_label, LV_LABEL_LONG_DOT);  // ellipsize a long step
    lv_obj_set_width(g_omega_label, 456);
    lv_obj_align(g_omega_label, LV_ALIGN_CENTER, 0, -2);  // clear the bar strip below
    g_omega_bar = lv_obj_create(g_omega);
    lv_obj_remove_style_all(g_omega_bar);
    lv_obj_set_style_bg_color(g_omega_bar, color_surface_base, 0);  // dark on amber, like the text
    lv_obj_set_style_bg_opa(g_omega_bar, LV_OPA_COVER, 0);
    lv_obj_set_pos(g_omega_bar, 0, 26);
    lv_obj_set_size(g_omega_bar, 0, 4);
    lv_obj_add_flag(g_omega_bar, LV_OBJ_FLAG_HIDDEN);
  }
  lv_label_set_text(g_omega_label, text != nullptr ? text : "OMEGA");
  // "OMEGA X/N: ..." carries the campaign position; render it as a width.
  int done = 0, total = 0;
  if (text != nullptr && std::sscanf(text, "OMEGA %d/%d", &done, &total) == 2 && total > 0) {
    if (done < 0) done = 0;
    if (done > total) done = total;
    lv_obj_set_size(g_omega_bar, (lv_coord_t)((480 * done) / total), 4);
    lv_obj_clear_flag(g_omega_bar, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(g_omega_bar, LV_OBJ_FLAG_HIDDEN);  // no count, no fake bar
  }
  lv_obj_clear_flag(g_omega, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(g_omega);
}

void omega_status_hide() {
  if (g_omega == nullptr) return;
  lv_obj_add_flag(g_omega, LV_OBJ_FLAG_HIDDEN);
}

// ---- Boot beach + random wash-up waves ----
namespace {
lv_obj_t  *s_beach = nullptr;       // static sand wedge
lv_obj_t  *s_wave = nullptr;        // foam flipbook (one wash at a time)
lv_timer_t *s_wave_timer = nullptr; // schedules the next random wash
uint32_t   s_wave_seed = 0x9e3779b9u;

uint32_t wave_rand() {
  s_wave_seed = s_wave_seed * 1103515245u + 12345u;  // tiny LCG; no libc rand dep
  return (s_wave_seed >> 16) & 0x7fffu;
}

// Runs on the LVGL thread (lv_timer_handler holds lv_lock): pick a random
// wash-up variant, play it once, then reschedule after a random gap. Sequence
// of pre-canned washes -> reads as a naturally random tide lapping the sand.
void wave_tick(lv_timer_t *t) {
  if (s_wave == nullptr || pono_wave_variant_count == 0) return;
  uint8_t v = (uint8_t)(wave_rand() % pono_wave_variant_count);
  lv_animimg_set_src(s_wave, (const void **)pono_wave_variants[v], pono_wave_variant_frames[v]);
  lv_animimg_set_duration(s_wave, 850 + (wave_rand() % 600));   // ~0.85-1.45s wash
  lv_animimg_set_repeat_count(s_wave, 1);                       // one wash, not a loop
  lv_animimg_start(s_wave);
  lv_timer_set_period(t, 1500 + (wave_rand() % 2800));          // random gap to the next
}
}  // namespace

void beach_init(lv_obj_t *parent) {
  if (parent == nullptr) return;
  const int y = 272 - (int)pono_beach_h;  // bottom-left corner of the 480x272 panel
  // z-order falls out of creation order: ocean (move_background) < beach <
  // wave < the spinner/joke/labels the boot panel creates after this. So no
  // foregrounding here, or a re-arm would shove the sand over the status text.
  if (s_beach == nullptr) {
    s_beach = lv_img_create(parent);
    lv_img_set_src(s_beach, &pono_beach);
    lv_obj_set_pos(s_beach, 0, y);
    lv_obj_clear_flag(s_beach, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_beach, LV_OBJ_FLAG_SCROLLABLE);
  }
  if (s_wave == nullptr) {
    s_wave = lv_animimg_create(parent);
    lv_obj_set_pos(s_wave, 0, y);
    lv_obj_set_size(s_wave, pono_beach_w, pono_beach_h);
    lv_animimg_set_src(s_wave, (const void **)pono_wave_variants[0], pono_wave_variant_frames[0]);
    lv_animimg_set_duration(s_wave, 1000);
    lv_animimg_set_repeat_count(s_wave, 1);
    lv_obj_clear_flag(s_wave, LV_OBJ_FLAG_CLICKABLE);
  }
  if (s_wave_timer == nullptr) {
    s_wave_timer = lv_timer_create(wave_tick, 400, nullptr);  // first wash shortly after boot
  } else {
    lv_timer_resume(s_wave_timer);
  }
}

void beach_stop() {
  if (s_wave_timer) lv_timer_pause(s_wave_timer);
  if (s_wave) lv_anim_del(s_wave, nullptr);  // halt any in-flight wash -> zero cost
}

void beach_teardown() {
  if (s_wave_timer) { lv_timer_del(s_wave_timer); s_wave_timer = nullptr; }
  s_beach = nullptr; s_wave = nullptr;  // children freed with the boot panel's container
}

}  // namespace pono
