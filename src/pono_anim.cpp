// SPDX-License-Identifier: GPL-3.0-only
// pono_anim.cpp - canned animation helpers. See pono_anim.h.
#include "pono_anim.h"

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

}  // namespace pono
