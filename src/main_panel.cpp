#include "main_panel.h"
#include "state.h"
#include "lvgl/lvgl.h"
#include "logger.h"
#include "pono_theme.h"  // Phase A.4: surface + accent tokens for tab UI

#include <string>

LV_IMG_DECLARE(filament_img);
LV_IMG_DECLARE(light_img);
LV_IMG_DECLARE(move);
LV_IMG_DECLARE(print);
LV_IMG_DECLARE(extruder);
LV_IMG_DECLARE(bed);
LV_IMG_DECLARE(fan);
LV_IMG_DECLARE(heater);
LV_IMG_DECLARE(emergency);

LV_FONT_DECLARE(materialdesign_font_40);

#define INFO_SYMBOL    u8"\U000F02FD"
#define SETTING_SYMBOL u8"\U000F1064"
#define HOME_SYMBOL    u8"\U000F02DC"
#define CONSOLE_SYMBOL u8"\U000F018D"

MainPanel::MainPanel(KWebSocketClient &websocket,
		     std::mutex &lock,
		     SpoolmanPanel &sm)
  : NotifyConsumer(lock)
  , ws(websocket)
  , homing_panel(ws, lock)
  , fan_panel(ws, lock)
  , led_panel(ws, lock)    
  , tabview(lv_tabview_create(lv_scr_act(), LV_DIR_LEFT, 60))
  , main_tab(lv_tabview_add_tab(tabview, HOME_SYMBOL))
  , console_tab(lv_tabview_add_tab(tabview, CONSOLE_SYMBOL))
  , console_panel(ws, lock, console_tab)
  , setting_tab(lv_tabview_add_tab(tabview, SETTING_SYMBOL))
  , setting_panel(websocket, lock, setting_tab)
  , sysinfo_tab(lv_tabview_add_tab(tabview, INFO_SYMBOL))
  , sysinfo_panel(sysinfo_tab)
  , main_cont(lv_obj_create(main_tab))
  , print_status_panel(websocket, lock, main_cont)
  , print_panel(ws, lock, print_status_panel)
  , numpad(Numpad(main_cont))
  , extruder_panel(ws, lock, numpad, sm)
  , prompt_panel(websocket, lock, main_cont)
  , spoolman_panel(sm)
  , temp_cont(lv_obj_create(main_cont))
  , temp_chart(lv_chart_create(main_cont))
  , homing_btn(main_cont, &move, "Homing", &MainPanel::_handle_homing_cb, this)
  , extrude_btn(main_cont, &filament_img, "Extrude", &MainPanel::_handle_extrude_cb, this)
  , action_btn(main_cont, &fan, "Fans", &MainPanel::_handle_fanpanel_cb, this)
  , led_btn(main_cont, &light_img, "LED", &MainPanel::_handle_ledpanel_cb, this)
  , print_btn(main_cont, &print, "Print", &MainPanel::_handle_print_cb, this)
  , emergency_btn(main_cont, &emergency, "Stop", &MainPanel::_handle_emergency_cb, this,
  		  "Do you want to emergency stop?",
  		  [&websocket]() {
  		    LOG_DEBUG("emergency stop pressed");
  		    websocket.send_jsonrpc("printer.emergency_stop");
  		  })
{
    lv_style_init(&style);
    lv_style_set_img_recolor_opa(&style, LV_OPA_30);
    lv_style_set_img_recolor(&style, lv_color_black());
    lv_style_set_border_width(&style, 0);
    lv_style_set_bg_color(&style, pono::color_surface_base);

    ws.register_notify_update(this);

    lv_obj_add_event_cb(tabview, &MainPanel::_tabview_event_cb,
                            LV_EVENT_VALUE_CHANGED, this);
}

MainPanel::~MainPanel() {
  if (tabview != NULL) {
    lv_obj_del(tabview);
    tabview = NULL;
  }

  sensors.clear();
}

void MainPanel::subscribe() {
  LOG_TRACE("main panel subscribing");
  print_panel.subscribe();
}

void MainPanel::init(json &j) {
  std::lock_guard<std::mutex> lock(lv_lock);
  for (const auto &el : sensors) {
    auto target_value = j[json::json_pointer(fmt::format("/result/status/{}/target", el.first))];
    if (!target_value.is_null()) {
      int target = target_value.template get<int>();
      el.second->update_target(target);
    }

    auto temp_value = j[json::json_pointer(fmt::format("/result/status/{}/temperature", el.first))];
    if (!temp_value.is_null()) {
      int value = temp_value.template get<int>();
      el.second->update_series(value);
      el.second->update_value(value);
    }
  }
  auto fans = State::get_instance()->get_display_fans();
  print_status_panel.init(fans);
}

void MainPanel::consume(json &j) {  
  std::lock_guard<std::mutex> lock(lv_lock);
  for (const auto &el : sensors) {
    auto target_value = j[json::json_pointer(fmt::format("/params/0/{}/target", el.first))];
    if (!target_value.is_null()) {
      int target = target_value.template get<int>();
      el.second->update_target(target);
    }

    auto temp_value = j[json::json_pointer(fmt::format("/params/0/{}/temperature", el.first))];
    if (!temp_value.is_null()) {
      int value = temp_value.template get<int>();
      el.second->update_series(value);
      el.second->update_value(value);
    }
  }

  json &pstat_state = j["/params/0/print_stats/state"_json_pointer];
  if (!pstat_state.is_null()) {
    if (pstat_state.template get<std::string>() != "printing") {
      homing_btn.enable();
      extrude_btn.enable();
    } else {
      homing_btn.disable();
      extrude_btn.disable();
    }
  }

  led_btn.set_image(led_panel.get_main_button_image());

  // --- Pono cockpit live update (each field guarded; retains last if absent) ---
  if (home_h.arc) {
    auto prog = j["/params/0/virtual_sdcard/progress"_json_pointer];
    if (!prog.is_null()) {
      int pct = (int)(prog.template get<double>() * 100.0 + 0.5);
      lv_arc_set_value(home_h.arc, pct);
      if (home_h.pct) lv_label_set_text(home_h.pct, fmt::format("{}%", pct).c_str());
    }
    auto cl = j["/params/0/print_stats/info/current_layer"_json_pointer];
    if (!cl.is_null() && home_h.layer) {
      auto tl = j["/params/0/print_stats/info/total_layer"_json_pointer];
      lv_label_set_text(home_h.layer, fmt::format("layer {} / {}",
        cl.template get<int>(), tl.is_null() ? 0 : tl.template get<int>()).c_str());
    }
    auto fn = j["/params/0/print_stats/filename"_json_pointer];
    if (!fn.is_null() && home_h.job) {
      std::string f = fn.template get<std::string>();
      lv_label_set_text(home_h.job, f.empty() ? "Pono Print" : f.c_str());
    }
    auto et = j["/params/0/extruder/temperature"_json_pointer];
    if (!et.is_null() && home_h.nozzle) {
      int v = (int)et.template get<double>();
      lv_label_set_text(home_h.nozzle, fmt::format("{}", v).c_str());
      lv_obj_set_style_text_color(home_h.nozzle, v >= 240 ? pono::color_state_error : (v >= 50 ? pono::color_state_warning : pono::color_text_primary), 0);
    }
    auto bt = j["/params/0/heater_bed/temperature"_json_pointer];
    if (!bt.is_null() && home_h.bed) {
      int v = (int)bt.template get<double>();
      lv_label_set_text(home_h.bed, fmt::format("{}", v).c_str());
      lv_obj_set_style_text_color(home_h.bed, v >= 100 ? pono::color_state_error : (v >= 40 ? pono::color_state_warning : pono::color_text_primary), 0);
    }
    if (!pstat_state.is_null() && home_h.state_pill) {
      bool printing = pstat_state.template get<std::string>() == "printing";
      if (printing) lv_obj_clear_flag(home_h.state_pill, LV_OBJ_FLAG_HIDDEN);
      else lv_obj_add_flag(home_h.state_pill, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

static void scroll_begin_event(lv_event_t * e) {
  /*Silky tab transitions: animate the tab-switch slide. It was forced to 0
   *(instant) when the panel ran at 33 fps; at the 60 fps refresh the slide
   *is smooth. Triggered when a tab button is clicked. */
  if (lv_event_get_code(e) == LV_EVENT_SCROLL_BEGIN) {
    lv_anim_t * a = (lv_anim_t*)lv_event_get_param(e);
    if(a)  a->time = 130;  // Pono: snappier tab switch; 260ms full-screen scroll felt sluggish on this sw-rendered SoC
  }
}

// this is just to ensure we refresh the IP addresses
void MainPanel::_tabview_event_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;

    auto *self = static_cast<MainPanel*>(lv_event_get_user_data(e));
    lv_obj_t *tv = lv_event_get_target(e);

    const uint16_t idx = lv_tabview_get_tab_act(tv);

    const uint16_t sysinfo_idx = lv_obj_get_index(self->sysinfo_tab);
    if (idx == sysinfo_idx) {
        self->sysinfo_panel.foreground();
    }
}

void MainPanel::create_panel() {
  lv_obj_clear_flag(lv_tabview_get_content(tabview), LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(lv_tabview_get_content(tabview), scroll_begin_event, LV_EVENT_SCROLL_BEGIN, NULL);
  
  lv_obj_t * tab_btns = lv_tabview_get_tab_btns(tabview);
  lv_obj_set_style_bg_color(tab_btns, pono::color_accent_primary, LV_STATE_CHECKED | LV_PART_ITEMS);  // active tab highlight
  lv_obj_set_style_outline_width(tab_btns, 0, LV_PART_ITEMS | LV_STATE_FOCUS_KEY | LV_STATE_FOCUS_KEY);
  lv_obj_set_style_border_side(tab_btns, 0, LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_text_font(tab_btns, &materialdesign_font_40, LV_STATE_DEFAULT);

  lv_obj_set_style_pad_all(main_tab, 0, 0);
  lv_obj_set_style_pad_all(console_tab, 0, 0);
  lv_obj_set_style_pad_all(setting_tab, 0, 0);
  lv_obj_set_style_pad_all(sysinfo_tab, 0, 0);

  create_main(main_tab);

  // Pono native cockpit: full-screen on the active screen, over the tabview,
  // hidden until connect (show_home). The tabview stays behind as a safety net.
  home_scr = lv_obj_create(lv_scr_act());
  lv_obj_remove_style_all(home_scr);
  lv_obj_set_size(home_scr, 480, 272);
  lv_obj_set_pos(home_scr, 0, 0);
  lv_obj_clear_flag(home_scr, LV_OBJ_FLAG_SCROLLABLE);
  pono::HomeModel hm{};
  hm.printing = false; hm.progress_pct = 0; hm.layer = 0; hm.layer_total = 0;
  hm.job_name = "Pono Print"; hm.material = "ready"; hm.eta = "idle";
  hm.nozzle = 0; hm.nozzle_set = 0; hm.bed = 0; hm.bed_set = 0;
  pono::build_home(home_scr, hm, &home_h);
  lv_obj_t *taps[] = { home_h.btn_pausestop, home_h.qa[0], home_h.qa[1],
                       home_h.qa[2], home_h.tile_nozzle, home_h.tile_bed,
                       home_h.tile_tune, home_h.tile_omega };
  for (lv_obj_t *t : taps) if (t) lv_obj_add_event_cb(t, &MainPanel::_home_tap, LV_EVENT_CLICKED, this);
  lv_obj_add_flag(home_scr, LV_OBJ_FLAG_HIDDEN);  // revealed on connect
}

void MainPanel::show_home() {
  if (!home_scr) return;
  lv_obj_clear_flag(home_scr, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(home_scr);
}

// Cockpit tile taps route to the existing (proven) control panels, which
// overlay above the cockpit and close back to it.
void MainPanel::_home_tap(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  auto *s = static_cast<MainPanel *>(lv_event_get_user_data(e));
  lv_obj_t *t = lv_event_get_target(e);
  pono::HomeHandles &h = s->home_h;
  if (t == h.btn_pausestop || t == h.qa[2]) s->print_panel.foreground();           // Pause/Stop, Files
  else if (t == h.qa[0]) s->homing_panel.foreground();                             // Move
  else if (t == h.qa[1] || t == h.tile_nozzle || t == h.tile_bed) s->extruder_panel.foreground();  // Filament, temps
  else if (t == h.tile_tune)  s->ws.gcode_script("PONO_CAL_STANDARD");  // standard on-device calibrate
  else if (t == h.tile_omega) s->ws.gcode_script("PONO_CAL_OMEGA");     // enhanced 1000% suite (queues + prompts)
}

void MainPanel::handle_homing_cb(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    LOG_TRACE("clicked homing");
    homing_panel.foreground();
  }
}

void MainPanel::handle_extrude_cb(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    LOG_TRACE("clicked extruder");
    extruder_panel.foreground();
  }
}

void MainPanel::handle_fanpanel_cb(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    LOG_TRACE("clicked fan panel");
    fan_panel.foreground();
  }
}

void MainPanel::handle_ledpanel_cb(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    LOG_TRACE("clicked led panel");
    led_panel.activate();
    led_btn.set_image(led_panel.get_main_button_image());
  }
}

void MainPanel::handle_print_cb(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    LOG_TRACE("clicked print");
    print_panel.foreground();
  }
}

void MainPanel::handle_emergency_cb(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    LOG_TRACE("clicked emergency");
  }
}

void MainPanel::create_main(lv_obj_t * parent) {
  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_ROW_WRAP);

  static lv_coord_t grid_main_row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static lv_coord_t grid_main_col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
    LV_GRID_TEMPLATE_LAST};

  lv_obj_clear_flag(main_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_height(main_cont, LV_PCT(100));

  lv_obj_set_flex_grow(main_cont, 1);
  lv_obj_set_grid_dsc_array(main_cont, grid_main_col_dsc, grid_main_row_dsc);

  lv_obj_set_grid_cell(homing_btn.get_container(), LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_CENTER, 0, 1);
  lv_obj_set_grid_cell(extrude_btn.get_container(), LV_GRID_ALIGN_CENTER, 3, 1, LV_GRID_ALIGN_CENTER, 0, 1);
  lv_obj_set_grid_cell(action_btn.get_container(), LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_CENTER, 1, 1);
  lv_obj_set_grid_cell(led_btn.get_container(), LV_GRID_ALIGN_CENTER, 3, 1, LV_GRID_ALIGN_CENTER, 1, 1);
  lv_obj_set_grid_cell(print_btn.get_container(), LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_CENTER, 2, 1);
  lv_obj_set_grid_cell(emergency_btn.get_container(), LV_GRID_ALIGN_CENTER, 3, 1, LV_GRID_ALIGN_CENTER, 2, 1);

  lv_obj_clear_flag(temp_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(temp_cont, LV_PCT(50), LV_PCT(50));
  lv_obj_set_style_pad_all(temp_cont, 0, 0);

  lv_obj_set_flex_flow(temp_cont, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_grid_cell(temp_cont, LV_GRID_ALIGN_START, 0, 2, LV_GRID_ALIGN_CENTER, 0, 2);

  lv_obj_align(temp_chart, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_size(temp_chart, LV_PCT(45), LV_PCT(40));
  lv_obj_set_style_size(temp_chart, 0, LV_PART_INDICATOR);

  lv_chart_set_range(temp_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 300);
  lv_obj_set_grid_cell(temp_chart, LV_GRID_ALIGN_END, 0, 2, LV_GRID_ALIGN_END, 2, 1);
  lv_chart_set_axis_tick(temp_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 0, 6, 5, true, 50);

  lv_chart_set_div_line_count(temp_chart, 3, 8);
  lv_chart_set_point_count(temp_chart, 5000);
  lv_chart_set_zoom_x(temp_chart, 5000);
  lv_obj_scroll_to_x(temp_chart, LV_COORD_MAX, LV_ANIM_OFF);
}

void MainPanel::create_sensors(json &temp_sensors) {
  std::lock_guard<std::mutex> lock(lv_lock);
  sensors.clear();
  for (auto &sensor : temp_sensors.items()) {
    std::string key = sensor.key();
    bool controllable = sensor.value()["controllable"].template get<bool>();

    // Temp-sensor accent color path. String-keyed presets map to fixed Pono
    // tokens; numeric int config falls through to color_for_palette() with
    // NONE sentinel + out-of-range safe default in the getter.
    lv_color_t color_code = pono::color_state_warning;  // default
    if (!sensor.value()["color"].is_number()) {
      std::string color = sensor.value()["color"].template get<std::string>();
      if (color == "red") {
	      color_code = pono::color_state_error;
      } else if (color == "purple") {
	      color_code = pono::color_state_intel;
      } else if (color == "blue") {
	      color_code = pono::color_accent_primary;
      }
    } else {
      color_code = pono::color_for_palette((lv_palette_t)sensor.value()["color"].template get<int>());
    }

    std::string display_name = sensor.value()["display_name"].template get<std::string>();

    const void* sensor_img = &heater;
    if (key == "extruder") {
      sensor_img = &extruder;
    } else if (key == "heater_bed") {
      sensor_img = &bed;
    }

    lv_chart_series_t *temp_series =
      lv_chart_add_series(temp_chart, color_code, LV_CHART_AXIS_PRIMARY_Y);

    sensors.insert({key, std::make_shared<SensorContainer>(ws, temp_cont, sensor_img, 150,
			   display_name.c_str(), color_code, controllable, false, numpad, key,
        		   temp_chart, temp_series)});
  }
}

void MainPanel::create_fans(json &fans) {
  fan_panel.create_fans(fans);
}

void MainPanel::create_leds(json &leds) {
  if (leds.is_array() && !leds.empty()) {
    led_btn.enable();
  } else {
    led_btn.disable();
  }
  led_panel.init(leds);
  led_btn.set_image(led_panel.get_main_button_image());
}

void MainPanel::enable_spoolman() {
  spoolman_panel.init();
  extruder_panel.enable_spoolman();
}
