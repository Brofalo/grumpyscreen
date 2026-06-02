#include "init_panel.h"
#include "utils.h"
#include "state.h"
#include "config.h"
#include "logger.h"
#include "pono_theme.h"  // Phase A.4: surface_raised token

#include <algorithm>
#include <cstdio>

InitPanel::InitPanel(MainPanel &mp, std::mutex& l)
  : cont(lv_obj_create(lv_scr_act()))
  , label(lv_label_create(cont))
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

  // Readable message pill, centered over the ocean and kept in the
  // foreground so the drifting swells pass behind the text.
  lv_obj_set_size(label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_max_width(label, lv_pct(82), 0);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(label, pono::color_text_primary, 0);
  lv_obj_set_style_bg_color(label, pono::color_surface_raised, 0);
  lv_obj_set_style_bg_opa(label, LV_OPA_80, 0);
  lv_obj_set_style_pad_all(label, pono::space_md, 0);
  lv_obj_set_style_radius(label, pono::radius_md, 0);
  lv_label_set_text(label, LV_SYMBOL_WARNING " Waiting for Klipper to start...");
  lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
  lv_obj_move_foreground(label);
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
  set_message(LV_SYMBOL_WARNING " Waiting for Klipper to start...");
  std::lock_guard<std::mutex> lock(lv_lock);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(cont);
}

void InitPanel::set_message(const char *message) {
	lv_label_set_text(label, message);
}
