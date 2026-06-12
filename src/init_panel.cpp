#include "init_panel.h"
#include "utils.h"
#include "state.h"
#include "config.h"
#include "logger.h"
#include "pono_home.h"   // build_boot / boot_set_progress (the shared boot layout)
#include "pono_anim.h"   // pono::busy_hide() (clear a stranded blocking overlay on disconnect)

#include <algorithm>
#include <string>
#include <vector>

InitPanel::InitPanel(MainPanel &mp, std::mutex& l)
  : cont(lv_obj_create(lv_scr_act()))
  , main_panel(mp)
  , lv_lock(l)
{
  // Full-screen boot container: the Hawaii flag with a real progress bar
  // under it (build_boot); shown only while we wait for Klipper, then hidden
  // when the live cockpit takes over. The joke cycle and comet spinner were
  // retired 2026-06-11 (the boot screen is the flag); the joke book still
  // serves the login banner via pono-print-boot-joke.
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_all(cont, 0, 0);
  lv_obj_set_style_border_width(cont, 0, 0);
  lv_obj_set_style_radius(cont, 0, 0);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

  pono::build_boot(cont, &boot_);

  pono::boot_set_progress(&boot_, 4, "Waiting for Klipper to start...");
}

InitPanel::~InitPanel() {
  if (cont != NULL) {
    lv_obj_del(cont);   // frees the flag/status/bar children too
    cont = NULL;
  }
}

// Advance the boot progress bar + status from a websocket-thread callback. Takes
// lv_lock itself (the connect callbacks below do not hold it).
void InitPanel::set_stage(int pct, const char *msg) {
  std::lock_guard<std::mutex> lock(lv_lock);
  pono::boot_set_progress(&boot_, pct, msg);
}

void InitPanel::connected(KWebSocketClient &ws) {
  LOG_DEBUG("init panel connected");
  State *state = State::get_instance();
  state->reset();

  set_stage(22, "Connecting to Moonraker...");

  ws.send_jsonrpc("printer.objects.list", [this, &ws](json& d) {
    State *state = State::get_instance();
	  state->set_data("printer_objs", d, "/result");

    this->set_stage(55, "Loading printer state...");

	  ws.send_jsonrpc("server.files.roots",
			[](json& j) { State::get_instance()->set_data("roots", j, "/result"); });

	  ws.send_jsonrpc("printer.info",
			[](json& j) { State::get_instance()->set_data("printer_info", j, "/result"); });

    this->main_panel.subscribe();

    // spoolman
    ws.send_jsonrpc("server.info", [this](json &j) {
      LOG_DEBUG("server_info {}", j.dump());
      State::get_instance()->set_data("server_info", j, "/result");

      auto &components = j["/result/components"_json_pointer];
      if (!components.is_null()) {
        const auto &has_spoolman = components.template get<std::vector<std::string>>();
        if (std::find(has_spoolman.begin(), has_spoolman.end(), "spoolman") != has_spoolman.end()) {
          this->main_panel.enable_spoolman();
        }
      }
    });

    auto display_sensors = state->get_display_sensors();
    this->main_panel.create_sensors(display_sensors);

    auto display_fans = state->get_display_fans();
    this->main_panel.create_fans(display_fans);

    auto display_leds = state->get_display_leds();
    this->main_panel.create_leds(display_leds);

    // subscribe to all objects except gcode_macro
    auto objs = d["/result/objects"_json_pointer];
    if (!objs.is_null()) {
      json sub_objs;
      for (auto &obj : objs) {
        if (!obj.is_string()) continue;  // skip a non-string entry rather than throw out of the connect callback
        std::string obj_name = obj.template get<std::string>();
        if (obj_name.rfind("gcode_macro ", 0 ) != 0) {
          sub_objs[obj_name] = nullptr;
        }
      }

      this->set_stage(78, "Subscribing to printer...");

      json subs = {{ "objects", sub_objs }};
      LOG_DEBUG("subscribing to {}", subs.dump());
      ws.send_jsonrpc("printer.objects.subscribe", subs, [this](json &data) {
        State::get_instance()->set_data("printer_state", data, "/result/status");
        this->main_panel.init(data);
        LOG_DEBUG("done init");
        std::lock_guard<std::mutex> lock(this->lv_lock);
        pono::boot_set_progress(&boot_, 100, "Ready");
        lv_obj_add_flag(this->cont, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_background(this->cont);
        this->main_panel.show_home();  // bring the native cockpit forward
      });
    }
  });
}

void InitPanel::disconnected(KWebSocketClient &ws) {
  LOG_DEBUG("init panel disconnected");
  std::lock_guard<std::mutex> lock(lv_lock);
  // disconnected() runs on the websocket thread; every LVGL write here must hold
  // lv_lock against the render loop (guppyscreen.cpp loop).
  pono::boot_set_progress(&boot_, 4, "Waiting for Klipper to start...");
  // A blocking-action or calibration overlay waits on a gcode RPC / display
  // update that will never arrive now the link is down. Reset all overlay
  // tracking so it can't strand on the top layer, and so the cal overlay
  // re-shows after reconnect if the cal is still running.
  main_panel.reset_overlay_state();
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(cont);
}

// CONTRACT: writes the LVGL status label without self-locking. The caller must
// hold GuppyScreen::lv_lock. disconnected() runs on the ws thread and takes the
// lock before calling this (the alpha.147 fix was reordering that very lock).
void InitPanel::set_message(const char *message) {
  if (boot_.status) lv_label_set_text(boot_.status, message);
}
