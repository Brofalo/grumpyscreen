// SPDX-License-Identifier: GPL-3.0-only
// pono_anim.cpp - canned animation helpers. See pono_anim.h.
#include "pono_anim.h"
#include "pono_theme.h"

namespace {
// Singleton "working" overlay, lazily built on lv_layer_top.
lv_obj_t *g_busy = nullptr;
lv_obj_t *g_busy_spinner = nullptr;
lv_obj_t *g_busy_label = nullptr;
}  // namespace

namespace pono {

lv_obj_t *spinner_create(lv_obj_t *parent, uint16_t period_ms) {
  lv_obj_t *a = lv_animimg_create(parent);
  lv_animimg_set_src(a, (const void **)pono_spinner_frames, pono_spinner_frame_count);
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

}  // namespace pono
