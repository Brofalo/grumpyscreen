// SPDX-License-Identifier: GPL-3.0-only
// main_sim.cpp - Desktop SDL2 simulator entry point for Pono Print UI
//
// Phase A.5 per docs/design/pono-print-phase-a-technical-brief.md.
//
// Builds a Linux/macOS/Windows desktop binary that opens a 480x272 SDL
// window and runs the Pono Print UI without needing to flash the printer.
// Use this for screen iteration during Phase B-G implementation.
//
// Build (from sim/ directory):
//   make -f Makefile.sim
//
// Run:
//   ./grumpyscreen-sim                 # opens default Home panel
//   ./grumpyscreen-sim --screen=queue  # opens Queue panel (Phase G3)
//   ./grumpyscreen-sim --screen=files  # opens Files panel
//
// Screenshot regression (from CI):
//   xvfb-run -s "-screen 0 480x272x24" ./grumpyscreen-sim --capture out.png --exit
//
// Drop-in for Brofalo/grumpyscreen fork.

#include "lvgl.h"
#include "lv_drivers/sdl/sdl.h"
#include "../src/pono_theme.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

namespace {

constexpr lv_coord_t kWidth  = 480;
constexpr lv_coord_t kHeight = 272;

void parse_args(int argc, char **argv,
                std::string *screen, std::string *capture_out, bool *exit_after) {
    *screen = "home";
    *capture_out = "";
    *exit_after = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a.rfind("--screen=", 0) == 0) {
            *screen = a.substr(9);
        } else if (a.rfind("--capture=", 0) == 0) {
            *capture_out = a.substr(10);
        } else if (a == "--exit") {
            *exit_after = true;
        }
    }
}

// Build a placeholder screen showing the active theme is wired up.
// Replace with real panel calls once Phase B-G land.
lv_obj_t *build_placeholder_screen(const std::string &name) {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, pono::color_surface_base, 0);

    // Side rail (48 px) for visual reference
    lv_obj_t *rail = lv_obj_create(scr);
    lv_obj_set_size(rail, pono::rail_width, pono::frame_height);
    lv_obj_set_pos(rail, 0, 0);
    lv_obj_set_style_bg_color(rail, pono::color_surface_raised, 0);
    lv_obj_set_style_border_width(rail, 0, 0);
    lv_obj_set_style_radius(rail, 0, 0);

    // Top bar (28 px)
    lv_obj_t *topbar = lv_obj_create(scr);
    lv_obj_set_size(topbar, pono::content_w, pono::topbar_height);
    lv_obj_set_pos(topbar, pono::content_x, 0);
    lv_obj_set_style_bg_color(topbar, pono::color_surface_base, 0);
    lv_obj_set_style_border_width(topbar, 0, 0);
    lv_obj_set_style_radius(topbar, 0, 0);

    // Screen title (h2 token)
    lv_obj_t *title = lv_label_create(topbar);
    lv_label_set_text_fmt(title, "%s", name.c_str());
    lv_obj_set_style_text_color(title, pono::color_text_primary, 0);
    lv_obj_set_style_text_font(title, pono::font_h2, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, pono::space_md, 0);

    // Placeholder content
    lv_obj_t *body = lv_label_create(scr);
    lv_label_set_text(body,
        "Pono Print simulator (Phase A scaffold)\n"
        "Drop in real panel build calls here once Phase B-G land.\n"
        "Cyan = accent_primary; this proves the theme is registered.");
    lv_obj_set_style_text_color(body, pono::color_text_secondary, 0);
    lv_obj_set_style_text_font(body, pono::font_body, 0);
    lv_obj_set_width(body, pono::content_w - pono::space_lg * 2);
    lv_obj_align(body, LV_ALIGN_LEFT_MID,
        pono::content_x + pono::space_lg,
        pono::topbar_height / 2);

    // Accent CTA showing the button theme
    lv_obj_t *btn = lv_btn_create(scr);
    lv_obj_set_size(btn, 120, 32);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -pono::space_md);
    lv_obj_t *btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, "OK");
    lv_obj_center(btn_lbl);

    return scr;
}

} // namespace

int main(int argc, char **argv) {
    std::string screen, capture_out;
    bool exit_after = false;
    parse_args(argc, argv, &screen, &capture_out, &exit_after);

    lv_init();
    sdl_init();

    static lv_color_t draw_buf_1[kWidth * 100];
    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, draw_buf_1, nullptr, kWidth * 100);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.flush_cb = sdl_display_flush;
    disp_drv.hor_res  = kWidth;
    disp_drv.ver_res  = kHeight;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_t *disp = lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = sdl_mouse_read;
    lv_indev_drv_register(&indev_drv);

    pono::theme_init(disp);
    build_placeholder_screen(screen);

    // Render one frame so the first capture is valid.
    lv_timer_handler();

    if (!capture_out.empty()) {
        std::fprintf(stderr,
            "sim: capture requested at %s "
            "(SDL framebuffer dump TODO; see Phase A.5 brief)\n",
            capture_out.c_str());
    }
    if (exit_after) {
        std::fprintf(stderr, "sim: --exit set, terminating after first render\n");
        return 0;
    }

    while (true) {
        lv_timer_handler();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return 0;
}
