#include "init_panel.h"
#include "utils.h"
#include "state.h"
#include "config.h"
#include "logger.h"
#include "pono_theme.h"  // Phase A.4: surface_raised token

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>

namespace {
// The boot joke is written to /run/pono-print-joke at boot by the
// pono-print-boot-joke init script. Read the single line; empty if absent
// (e.g. on a dev box or before the init script runs).
std::string read_boot_joke() {
  std::ifstream f("/run/pono-print-joke");
  if (!f.is_open()) return std::string();
  std::string line;
  std::getline(f, line);
  return line;
}
} // namespace

InitPanel::InitPanel(MainPanel &mp, std::mutex& l)
  : cont(lv_obj_create(lv_scr_act()))
  , label(lv_label_create(cont))
  , joke_label(lv_label_create(cont))
  , main_panel(mp)
  , lv_lock(l)
{
  // Full-screen Hawaii ocean boot backdrop. It is visible only while waiting
  // for Klipper to come up; connected() calls ocean_tide_stop() so the tide
  // animation costs nothing once the live dashboard takes over.
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_all(cont, 0, 0);
  lv_obj_set_style_border_width(cont, 0, 0);
  lv_obj_set_style_radius(cont, 0, 0);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  pono::ocean_tide_init(cont);

  // Hawaii boot joke: the delight. Deliberately small (a caption, not a
  // banner) so it never shouts over the tide; the readable pill keeps it
  // legible while the swells drift behind it. It persists for the whole wait
  // because set_message() only touches the status line below.
  std::string joke = read_boot_joke();
  lv_obj_set_width(joke_label, lv_pct(78));   // explicit width = reliable wrap
  lv_obj_set_height(joke_label, LV_SIZE_CONTENT);
  lv_label_set_long_mode(joke_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(joke_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(joke_label, pono::color_text_primary, 0);
  lv_obj_set_style_text_font(joke_label, pono::font_caption, 0);
  lv_obj_set_style_bg_color(joke_label, pono::color_surface_raised, 0);
  lv_obj_set_style_bg_opa(joke_label, LV_OPA_80, 0);
  lv_obj_set_style_pad_all(joke_label, pono::space_sm, 0);
  lv_obj_set_style_radius(joke_label, pono::radius_md, 0);
  if (!joke.empty()) {
    lv_label_set_text(joke_label, joke.c_str());
  } else {
    lv_obj_add_flag(joke_label, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_align(joke_label, LV_ALIGN_CENTER, 0, -10);

  // Connection status line: small + secondary, tucked under the joke. This is
  // what set_message() updates as we wait for / reconnect to Klipper.
  lv_obj_set_width(label, lv_pct(82));
  lv_obj_set_height(label, LV_SIZE_CONTENT);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(label, pono::color_text_secondary, 0);
  lv_obj_set_style_text_font(label, pono::font_micro, 0);
  lv_label_set_text(label, "Waiting for Klipper to start...");
  if (!joke.empty()) {
    lv_obj_align_to(label, joke_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
  } else {
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
  }

  lv_obj_move_foreground(joke_label);
  lv_obj_move_foreground(label);
  pono::panel_open(joke_label);  // silky fade-in
}

InitPanel::~InitPanel() {
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

void InitPanel::connected(KWebSocketClient &ws) {
  LOG_DEBUG("init panel connected");
  State *state = State::get_instance();
  state->reset();

  ws.send_jsonrpc("printer.objects.list", [this, &ws](json& d) {
    State *state = State::get_instance();
	  state->set_data("printer_objs", d, "/result");

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
        std::string obj_name = obj.template get<std::string>();
        if (obj_name.rfind("gcode_macro ", 0 ) != 0) {
          sub_objs[obj_name] = nullptr;
        }
      }

      json subs = {{ "objects", sub_objs }};
      LOG_DEBUG("subscribing to {}", subs.dump());
      ws.send_jsonrpc("printer.objects.subscribe", subs, [this](json &data) {
        State::get_instance()->set_data("printer_state", data, "/result/status");
        this->main_panel.init(data);
        LOG_DEBUG("done init");
        std::lock_guard<std::mutex> lock(this->lv_lock);
        lv_obj_add_flag(this->cont, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_background(this->cont);
        pono::ocean_tide_stop(this->cont);  // dashboard is up; stop the boot tide
      });
    }
  });
}

void InitPanel::disconnected(KWebSocketClient &ws) {
  LOG_DEBUG("init panel disconnected");
  set_message("Waiting for Klipper to start...");
  std::lock_guard<std::mutex> lock(lv_lock);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(cont);
}

void InitPanel::set_message(const char *message) {
	lv_label_set_text(label, message);
}
