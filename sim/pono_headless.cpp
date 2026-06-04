// SPDX-License-Identifier: GPL-3.0-only
// pono_headless.cpp - headless LVGL renderer for Pono Print UI iteration
//
// Renders a Pono screen to an off-screen framebuffer and dumps a 24-bit BMP -
// NO SDL, NO display hardware, NO X server. Links against only LVGL + the
// pono_theme/pono_home builders, so it compiles anywhere g++ + the in-repo
// LVGL submodule live (here: MinGW on Windows). This is the "on glass" loop:
// build a screen, render it, look at the pixels, before ever flashing.
//
// Usage:
//   pono-headless.exe <out.bmp> [advance_ms] [screen]
//     out.bmp     output path (default out.bmp)
//     advance_ms  ms of animation to advance before capture (default 700)
//     screen      which screen to build (default "home")

#include "lvgl.h"
#include "pono_theme.h"
#include "pono_home.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#define PW 480
#define PH 272

static lv_color_t g_fb[PW * PH]; // accumulated screen framebuffer

// LVGL here is built LV_TICK_CUSTOM=1 with SYS_TIME_EXPR = custom_tick_get().
// The real app defines that symbol; the headless harness supplies it as a
// counter we advance by hand to drive animations to a chosen frame.
static uint32_t g_tick_ms = 0;
extern "C" uint32_t custom_tick_get(void) { return g_tick_ms; }

// lv_extra.c initialises the extra/libs enabled in lv_conf (FS_STDIO, PNG).
// Those .c are excluded from the sim build (POSIX/3rd-party), so satisfy the
// linker with no-op stubs - the cockpit needs no filesystem nor PNG decode.
extern "C" void lv_fs_stdio_init(void) {}
extern "C" void lv_png_init(void) {}

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
  for (int y = area->y1; y <= area->y2; y++) {
    for (int x = area->x1; x <= area->x2; x++) {
      if (x >= 0 && x < PW && y >= 0 && y < PH) g_fb[y * PW + x] = *color_p;
      color_p++;
    }
  }
  lv_disp_flush_ready(drv);
}

static void put_le32(unsigned char *p, unsigned int v) {
  p[0] = v & 0xff; p[1] = (v >> 8) & 0xff; p[2] = (v >> 16) & 0xff; p[3] = (v >> 24) & 0xff;
}

static int write_bmp(const char *path) {
  const int rowsize = (PW * 3 + 3) & ~3;
  const int datasize = rowsize * PH;
  const int filesize = 54 + datasize;
  unsigned char hdr[54];
  memset(hdr, 0, sizeof hdr);
  hdr[0] = 'B'; hdr[1] = 'M';
  put_le32(hdr + 2, filesize);
  put_le32(hdr + 10, 54);
  put_le32(hdr + 14, 40);
  put_le32(hdr + 18, PW);
  put_le32(hdr + 22, PH);
  hdr[26] = 1; hdr[28] = 24;
  put_le32(hdr + 34, datasize);
  FILE *f = fopen(path, "wb");
  if (!f) return -1;
  fwrite(hdr, 1, 54, f);
  unsigned char *row = (unsigned char *)calloc(rowsize, 1);
  for (int y = PH - 1; y >= 0; y--) { // BMP rows are bottom-up
    for (int x = 0; x < PW; x++) {
      lv_color32_t c;
      c.full = lv_color_to32(g_fb[y * PW + x]);
      row[x * 3 + 0] = c.ch.blue;
      row[x * 3 + 1] = c.ch.green;
      row[x * 3 + 2] = c.ch.red;
    }
    fwrite(row, 1, rowsize, f);
  }
  free(row);
  fclose(f);
  return 0;
}

int main(int argc, char **argv) {
  const char *out = argc > 1 ? argv[1] : "out.bmp";
  int advance_ms = argc > 2 ? atoi(argv[2]) : 700;
  std::string screen = argc > 3 ? argv[3] : "home";

  lv_init();

  static lv_disp_draw_buf_t dbuf;
  static lv_color_t buf1[PW * PH];
  lv_disp_draw_buf_init(&dbuf, buf1, NULL, PW * PH);

  static lv_disp_drv_t ddrv;
  lv_disp_drv_init(&ddrv);
  ddrv.hor_res = PW;
  ddrv.ver_res = PH;
  ddrv.flush_cb = flush_cb;
  ddrv.draw_buf = &dbuf;
  lv_disp_t *disp = lv_disp_drv_register(&ddrv);

  pono::theme_init(disp);

  if (screen == "boot") {
    // Mirror init_panel: ocean tide + small joke pill + tiny status line, so
    // the boot joke size and the tide flex can be eyeballed before flashing.
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_pad_all(scr, 0, 0);
    pono::ocean_tide_init(scr);

    lv_obj_t *joke = lv_label_create(scr);
    lv_obj_set_width(joke, lv_pct(78));
    lv_obj_set_height(joke, LV_SIZE_CONTENT);
    lv_label_set_long_mode(joke, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(joke, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(joke, pono::color_text_primary, 0);
    lv_obj_set_style_text_font(joke, pono::font_caption, 0);
    lv_obj_set_style_bg_color(joke, pono::color_surface_raised, 0);
    lv_obj_set_style_bg_opa(joke, LV_OPA_80, 0);
    lv_obj_set_style_pad_all(joke, 8, 0);
    lv_obj_set_style_radius(joke, 8, 0);
    lv_label_set_text(joke,
        "First layer is like a good poke bowl: get the base right or the whole thing falls apart.");
    lv_obj_align(joke, LV_ALIGN_CENTER, 0, -10);

    lv_obj_t *st = lv_label_create(scr);
    lv_obj_set_style_text_color(st, pono::color_text_secondary, 0);
    lv_obj_set_style_text_font(st, pono::font_micro, 0);
    lv_label_set_text(st, "Waiting for Klipper to start...");
    lv_obj_align_to(st, joke, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    lv_obj_t *bt = lv_obj_create(scr);
    lv_obj_remove_style_all(bt);
    lv_obj_set_size(bt, 220, 6);
    lv_obj_align_to(bt, st, LV_ALIGN_OUT_BOTTOM_MID, 0, 16);
    lv_obj_set_style_bg_color(bt, pono::color_surface_raised, 0);
    lv_obj_set_style_bg_opa(bt, LV_OPA_70, 0);
    lv_obj_set_style_radius(bt, 3, 0);
    lv_obj_t *sg = lv_obj_create(bt);
    lv_obj_remove_style_all(sg);
    lv_obj_set_size(sg, 64, 6);
    lv_obj_set_x(sg, 96);
    lv_obj_set_style_bg_color(sg, pono::color_accent_primary, 0);
    lv_obj_set_style_bg_opa(sg, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(sg, 3, 0);

    // Dedication pill, pinned to the bottom (mirrors init_panel).
    lv_obj_t *ded = lv_label_create(scr);
    lv_obj_set_width(ded, LV_SIZE_CONTENT);
    lv_obj_set_height(ded, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(ded, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(ded, pono::color_accent_primary, 0);
    lv_obj_set_style_text_font(ded, pono::font_caption, 0);
    lv_obj_set_style_bg_color(ded, pono::color_surface_raised, 0);
    lv_obj_set_style_bg_opa(ded, LV_OPA_80, 0);
    lv_obj_set_style_pad_all(ded, 8, 0);
    lv_obj_set_style_radius(ded, 8, 0);
    lv_label_set_text(ded, "Dedicated to Elio and Io\nmy little cousins");
    lv_obj_align(ded, LV_ALIGN_BOTTOM_MID, 0, -10);
  } else if (screen == "move") {
    pono::build_move(lv_scr_act());
  } else if (screen == "filament") {
    pono::build_filament(lv_scr_act());
  } else if (screen == "temps" || screen == "temp") {
    pono::build_temps(lv_scr_act());
  } else if (screen == "fans") {
    pono::build_fans(lv_scr_act());
  } else if (screen == "files") {
    pono::build_files(lv_scr_act());
  } else if (screen == "tune") {
    pono::build_tune(lv_scr_act());
  } else if (screen == "settings" || screen == "expert") {
    pono::build_settings(lv_scr_act());
  } else if (screen == "more") {
    pono::build_more(lv_scr_act());
  } else if (screen == "idle") {
    pono::build_home(lv_scr_act(), pono::demo_home_idle_model());
  } else {
    pono::build_home(lv_scr_act(), pono::demo_home_model());
  }

  // advance time so animations settle (ocean drift, glow pulse)
  for (int t = 0; t < advance_ms; t += 16) {
    g_tick_ms += 16;
    lv_timer_handler();
  }

  // hide the perf-monitor overlay (sys layer) so it does not blemish captures
  lv_obj_t *sys = lv_disp_get_layer_sys(disp);
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(sys); i++) {
    lv_obj_add_flag(lv_obj_get_child(sys, i), LV_OBJ_FLAG_HIDDEN);
  }
  lv_refr_now(disp);

  if (write_bmp(out) != 0) {
    fprintf(stderr, "failed to write %s\n", out);
    return 1;
  }
  fprintf(stderr, "wrote %s (%s, %dms)\n", out, screen.c_str(), advance_ms);
  return 0;
}
