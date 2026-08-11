#ifndef __PROMPT_LAYOUT_H__
#define __PROMPT_LAYOUT_H__

// Geometry for the Klipper action:prompt dialog, split out of PromptPanel so it
// can be rendered by the headless harness. PromptPanel owns the websocket, the
// button callbacks and the command plumbing; this unit owns nothing but LVGL
// objects and their sizing, which is the half that has to be looked at on a
// 480x272 panel to know whether a line of body text is readable.
//
// The split exists because the clipping it fixes is only visible in pixels, and
// linking PromptPanel into the harness would drag in libhv and the whole socket
// stack. One code path, two callers: the app and sim/pono_headless.cpp.

#include "lvgl.h"

namespace pono {

struct PromptLayout {
  lv_obj_t *cont   = nullptr;  // the dialog card
  lv_obj_t *header = nullptr;  // title line
  lv_obj_t *body   = nullptr;  // prompt_text lines + inline button groups
  lv_obj_t *footer = nullptr;  // prompt_footer_button row
};

// Create the card and its three regions on `parent`. Leaves it sized for a
// prompt with no content yet; call prompt_layout_reset before filling.
void prompt_layout_build(lv_obj_t *parent, PromptLayout *l);

// action:prompt_begin - drop the previous prompt's content and return the card
// to its starting size, so a short prompt after a tall one does not inherit the
// tall one's growth.
void prompt_layout_reset(PromptLayout *l);

// action:prompt_text - add one body line. Returns the label.
lv_obj_t *prompt_layout_add_text(PromptLayout *l, const char *text);

// action:prompt_button_group_start - a horizontal button row inside the body.
lv_obj_t *prompt_layout_add_button_row(PromptLayout *l);

// action:prompt_show - make the body fit before it is put on screen.
void prompt_layout_fit(PromptLayout *l);

}  // namespace pono

#endif  // __PROMPT_LAYOUT_H__
