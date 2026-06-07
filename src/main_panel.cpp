#include "main_panel.h"
#include "state.h"
#include "lvgl/lvgl.h"
#include "logger.h"
#include "pono_theme.h"  // Phase A.4: surface + accent tokens for tab UI
#include "pono_anim.h"   // busy_show/busy_hide working overlay for blocking waits
#include "utils.h"       // KUtils::interface_ip for the System screen
#include <fstream>       // /etc/pono-version, /proc/uptime for the System screen

#include <string>
#include <cstdint>

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
  , numpad(Numpad(lv_layer_top()))  // top layer: keypad overlays every screen incl. Pono sub-screens
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
  if (home_scr != nullptr) {       // Pono: lv_obj_del recurses children + deletes their anims
    lv_obj_del(home_scr);
    home_scr = nullptr;
  }

  sensors.clear();
}

void MainPanel::subscribe() {
  // The native Files screen populates on open (populate_files); there is no
  // standing file-list subscription to (re)establish. The legacy print_panel
  // file-list subscribe - and its lv_lock flag-race - retired with the panel.
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
  { auto bm = j[json::json_pointer("/result/status/bed_mesh")]; if (!bm.is_null()) render_bed_mesh(bm); }  // initial heatmap

  // Seed the Pono cockpit + Move screen from the initial full state. consume()
  // reads these same fields from /params/0 deltas; the subscribe reply nests them
  // under /result/status. Moonraker only sends a field in a delta when it CHANGES,
  // so without this a connect to an already-printing (or already-homed) machine
  // shows the idle layout, 0% progress, and "--" position until state next moves.
  if (home_h.arc) {
    auto V = [&](const char *p) { return j[json::json_pointer(p)]; };
    { auto v = V("/result/status/virtual_sdcard/progress");        if (!v.is_null()) home_progress_    = v.template get<double>(); }
    { auto v = V("/result/status/print_stats/print_duration");     if (!v.is_null()) home_duration_    = v.template get<double>(); }
    { auto v = V("/result/status/extruder/temperature");           if (!v.is_null()) home_nozzle_      = (int)v.template get<double>(); }
    { auto v = V("/result/status/extruder/target");                if (!v.is_null()) home_nozzle_set_  = (int)v.template get<double>(); }
    { auto v = V("/result/status/heater_bed/temperature");         if (!v.is_null()) home_bed_         = (int)v.template get<double>(); }
    { auto v = V("/result/status/heater_bed/target");              if (!v.is_null()) home_bed_set_     = (int)v.template get<double>(); }
    { auto v = V("/result/status/print_stats/info/current_layer"); if (!v.is_null()) home_layer_       = v.template get<int>(); }
    { auto v = V("/result/status/print_stats/info/total_layer");   if (!v.is_null()) home_layer_total_ = v.template get<int>(); }
    { auto v = V("/result/status/print_stats/filename");           if (!v.is_null()) home_job_         = v.template get<std::string>(); }
    { auto v = V("/result/status/toolhead/homed_axes");            if (!v.is_null()) move_homed_       = v.template get<std::string>(); }
    { auto v = V("/result/status/toolhead/position");
      if (v.is_array() && v.size() >= 3) { move_pos_[0]=v[0].template get<double>(); move_pos_[1]=v[1].template get<double>(); move_pos_[2]=v[2].template get<double>(); } }
    { auto v = V("/result/status/print_stats/state");
      if (!v.is_null()) { std::string s = v.template get<std::string>(); home_printing_ = (s == "printing"); home_paused_ = (s == "paused"); } }
    rebuild_home();   // reflect actual state (layout + seeded temps/progress/job) immediately

    if (move_h_.pos) {  // same seed for the Move screen position (else "--" until first jog)
      auto axis = [&](char up, char lo, double v) {
        return move_homed_.find(lo) != std::string::npos
          ? fmt::format("{} {:.1f}", up, v) : fmt::format("{} --", up);
      };
      lv_label_set_text(move_h_.pos, fmt::format("{}  {}  {}",
        axis('X','x',move_pos_[0]), axis('Y','y',move_pos_[1]), axis('Z','z',move_pos_[2])).c_str());
      lv_obj_center(move_h_.pos);
    }
  }
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

  // --- Pono cockpit: cache live values, rebuild on the idle<->printing flip
  // (build_home is sim-verified for both states), update in place otherwise. ---
  if (home_h.arc) {
    auto V = [&](const char *p) { return j[json::json_pointer(p)]; };
    { auto v = V("/params/0/virtual_sdcard/progress");        if (!v.is_null()) home_progress_    = v.template get<double>(); }
    { auto v = V("/params/0/print_stats/print_duration");     if (!v.is_null()) home_duration_    = v.template get<double>(); }
    { auto v = V("/params/0/extruder/temperature");           if (!v.is_null()) home_nozzle_      = (int)v.template get<double>(); }
    { auto v = V("/params/0/extruder/target");                if (!v.is_null()) home_nozzle_set_  = (int)v.template get<double>(); }
    { auto v = V("/params/0/heater_bed/temperature");         if (!v.is_null()) home_bed_         = (int)v.template get<double>(); }
    { auto v = V("/params/0/heater_bed/target");              if (!v.is_null()) home_bed_set_     = (int)v.template get<double>(); }
    { auto v = V("/params/0/print_stats/info/current_layer"); if (!v.is_null()) home_layer_       = v.template get<int>(); }
    { auto v = V("/params/0/print_stats/info/total_layer");   if (!v.is_null()) home_layer_total_ = v.template get<int>(); }
    { auto v = V("/params/0/print_stats/filename");           if (!v.is_null()) home_job_         = v.template get<std::string>(); }

    // Move screen: live toolhead position, each axis gated on homed_axes. Both
    // arrive as Moonraker deltas (only on change), so cache them and reformat
    // whenever either lands. Unhomed axes read "--" like the build_move default.
    {
      auto ha = V("/params/0/toolhead/homed_axes");
      if (!ha.is_null()) move_homed_ = ha.template get<std::string>();
      auto pp = V("/params/0/toolhead/position");
      if (pp.is_array() && pp.size() >= 3) {
        move_pos_[0] = pp[0].template get<double>();
        move_pos_[1] = pp[1].template get<double>();
        move_pos_[2] = pp[2].template get<double>();
      }
      if (move_h_.pos && (!ha.is_null() || pp.is_array())) {
        auto axis = [&](char up, char lo, double v) {
          return move_homed_.find(lo) != std::string::npos
            ? fmt::format("{} {:.1f}", up, v) : fmt::format("{} --", up);
        };
        lv_label_set_text(move_h_.pos,
          fmt::format("{}  {}  {}", axis('X', 'x', move_pos_[0]),
                      axis('Y', 'y', move_pos_[1]), axis('Z', 'z', move_pos_[2])).c_str());
        lv_obj_center(move_h_.pos);  // re-center as the text width changes
      }
    }

    std::string pst = pstat_state.is_null() ? std::string() : pstat_state.template get<std::string>();
    bool printing = pstat_state.is_null() ? home_printing_ : (pst == "printing");
    bool paused   = pstat_state.is_null() ? home_paused_   : (pst == "paused");

    if (printing != home_printing_ || paused != home_paused_) {
      home_printing_ = printing;
      home_paused_   = paused;
      rebuild_home();                 // swap to the matching layout (Ready / printing / paused)
    } else {
      // temps update in both states: big number + heat color, target "/ N" or "off"
      if (home_h.nozzle) {
        lv_label_set_text(home_h.nozzle, fmt::format("{}", home_nozzle_).c_str());
        lv_obj_set_style_text_color(home_h.nozzle,
          home_nozzle_ >= 240 ? pono::color_state_error :
          home_nozzle_ >= 45  ? pono::color_state_warning : pono::color_text_primary, 0);
      }
      if (home_h.nozzle_set) {
        lv_label_set_text(home_h.nozzle_set,
          home_nozzle_set_ > 0 ? fmt::format("/ {}", home_nozzle_set_).c_str() : "off");
        lv_obj_align(home_h.nozzle_set, LV_ALIGN_RIGHT_MID, -14, 0);
      }
      if (home_h.bed) {
        lv_label_set_text(home_h.bed, fmt::format("{}", home_bed_).c_str());
        lv_obj_set_style_text_color(home_h.bed,
          home_bed_ >= 100 ? pono::color_state_error :
          home_bed_ >= 45  ? pono::color_state_warning : pono::color_text_primary, 0);
      }
      if (home_h.bed_set) {
        lv_label_set_text(home_h.bed_set,
          home_bed_set_ > 0 ? fmt::format("/ {}", home_bed_set_).c_str() : "off");
        lv_obj_align(home_h.bed_set, LV_ALIGN_RIGHT_MID, -14, 0);
      }
      // mirror live temps onto the Temperature sub-screen (current + target);
      // re-align after set_text so the centered values stay centered as they grow
      auto set_temp_lbl = [](lv_obj_t *o, const std::string &txt, lv_coord_t dy) {
        if (!o) return;
        lv_label_set_text(o, txt.c_str());
        lv_obj_align(o, LV_ALIGN_TOP_MID, 0, dy);
      };
      set_temp_lbl(temp_h_.nz_cur, fmt::format("{}", home_nozzle_), 28);
      set_temp_lbl(temp_h_.nz_tgt, home_nozzle_set_ > 0 ? fmt::format("set {}", home_nozzle_set_) : std::string("off"), 76);
      set_temp_lbl(temp_h_.bd_cur, fmt::format("{}", home_bed_), 28);
      set_temp_lbl(temp_h_.bd_tgt, home_bed_set_ > 0 ? fmt::format("set {}", home_bed_set_) : std::string("off"), 76);
      // mirror nozzle onto the Filament screen banner (was a static "-- / --")
      if (fil_h_.temp) {
        lv_label_set_text(fil_h_.temp, fmt::format("{} / {}", home_nozzle_,
          home_nozzle_set_ > 0 ? std::to_string(home_nozzle_set_) : std::string("off")).c_str());
        lv_obj_align(fil_h_.temp, LV_ALIGN_RIGHT_MID, -14, 0);
      }
      // live fan speeds onto the Fans screen (5 fans; auto fans update the % only)
      {
        static const char *fp[5] = {
          "/params/0/fan/speed",
          "/params/0/fan_generic model_helper_fan/speed",
          "/params/0/fan_generic box_fan/speed",
          "/params/0/temperature_fan mainboard/speed",
          "/params/0/heater_fan extruder/speed",
        };
        for (int i = 0; i < 5; i++) {
          auto fv = V(fp[i]);
          if (fv.is_null()) continue;
          int fpct = (int)(fv.template get<double>() * 100.0 + 0.5);
          if (fan_h_.val[i])    lv_label_set_text(fan_h_.val[i], fmt::format("{}%", fpct).c_str());
          if (fan_h_.slider[i]) lv_slider_set_value(fan_h_.slider[i], fpct, LV_ANIM_OFF);
        }
      }
      { auto bm = V("/params/0/bed_mesh"); if (!bm.is_null()) render_bed_mesh(bm); }  // heatmap on mesh change
      if (printing) {  // progress + layer + ETA only while a job runs
        lv_disp_trig_activity(NULL);  // keep the dashboard awake while printing (no blank mid-print)
        int pct = (int)(home_progress_ * 100.0 + 0.5);
        lv_arc_set_value(home_h.arc, pct);
        if (home_h.pct) {
          lv_label_set_text(home_h.pct, fmt::format("{}%", pct).c_str());
          lv_obj_align_to(home_h.pct, home_h.arc, LV_ALIGN_CENTER, 0, 0);
        }
        if (home_h.layer) {
          lv_label_set_text(home_h.layer, fmt::format("layer {} / {}", home_layer_, home_layer_total_).c_str());
          lv_obj_align_to(home_h.layer, home_h.arc, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);
        }
        if (home_h.eta && home_progress_ > 0.01 && home_duration_ > 1.0) {
          double remain = home_duration_ * (1.0 - home_progress_) / home_progress_;
          if (remain < 0.0) remain = 0.0;
          int mins = (int)(remain / 60.0 + 0.5);
          home_eta_ = mins >= 60 ? fmt::format("{}:{:02d} left", mins / 60, mins % 60)
                                 : fmt::format("{} min left", mins);
          lv_label_set_text(home_h.eta, home_eta_.c_str());
          if (home_h.layer) lv_obj_align_to(home_h.eta, home_h.layer, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);
        }
      }
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
  hm.job_name = ""; hm.material = "PA-CF . 0.25 diamond"; hm.eta = "";
  hm.nozzle = 0; hm.nozzle_set = 0; hm.bed = 0; hm.bed_set = 0;
  pono::build_home(home_scr, hm, &home_h);
  attach_home_taps();
  create_pono_screens();   // native Move/Filament/Temps/Fans/Files/Tune overlays (hidden)
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
  if (t == h.btn_pausestop) {                           // Resume (paused) / Pause (printing) / Print->Files (idle)
    if (s->home_paused_)        s->ws.gcode_script("RESUME");
    else if (s->home_printing_) s->ws.gcode_script("PAUSE");
    else { s->populate_files(); s->show_pono(s->files_scr_); }
  }
  else if (t == h.btn_cancel) {                         // abort the running/paused job (confirmed)
    s->confirm("Cancel this print?", [s]{ s->ws.gcode_script("CANCEL_PRINT"); });
  }
  else if (t == h.qa[0]) s->show_pono(s->move_scr_);    // Move
  else if (t == h.qa[1]) s->show_pono(s->fil_scr_);     // Filament
  else if (t == h.qa[2]) { s->populate_files(); s->show_pono(s->files_scr_); }  // Files
  else if (t == h.qa[3]) s->show_pono(s->fan_scr_);     // Fans
  else if (t == h.tile_nozzle || t == h.tile_bed) s->show_pono(s->temp_scr_);   // temps
  else if (t == h.tile_tune) s->show_pono(s->tune_scr_);  // Tune
  else if (t == h.tile_more) s->show_pono(s->more_scr_);  // More menu
}

// ---- Pono native sub-screen management ----

void MainPanel::create_pono_screens() {
  auto make = [&]() -> lv_obj_t * {
    lv_obj_t *s = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(s);
    lv_obj_set_size(s, 480, 272);
    lv_obj_set_pos(s, 0, 0);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s, LV_OBJ_FLAG_HIDDEN);
    return s;
  };
  move_scr_  = make(); pono::build_move(move_scr_, &move_h_);
  fil_scr_   = make(); pono::build_filament(fil_scr_, &fil_h_);
  temp_scr_  = make(); pono::build_temps(temp_scr_, &temp_h_);
  fan_scr_   = make(); pono::build_fans(fan_scr_, &fan_h_);
  files_scr_ = make(); pono::build_files(files_scr_, &files_h_);
  tune_scr_  = make(); pono::build_tune(tune_scr_, &tune_h_);
  more_scr_  = make(); pono::build_more(more_scr_, &more_h_);
  settings_scr_ = make(); pono::build_settings(settings_scr_, &settings_h_);
  mesh_scr_   = make(); pono::build_mesh(mesh_scr_, &mesh_h_);
  system_scr_ = make(); pono::build_system(system_scr_, &system_h_);
  power_scr_  = make(); pono::build_power(power_scr_, &power_h_);
  lights_scr_ = make(); pono::build_lights(lights_scr_, &lights_h_);

  lv_obj_t *taps[] = {
    move_h_.back, move_h_.xplus, move_h_.xminus, move_h_.yplus, move_h_.yminus,
    move_h_.zplus, move_h_.zminus, move_h_.home_xy, move_h_.home_all, move_h_.motors_off,
    move_h_.step[0], move_h_.step[1], move_h_.step[2], move_h_.step[3],
    fil_h_.back, fil_h_.load, fil_h_.unload, fil_h_.extrude, fil_h_.retract,
    fil_h_.preset[0], fil_h_.preset[1], fil_h_.preset[2], fil_h_.cooldown,
    temp_h_.back, temp_h_.nz_preset[0], temp_h_.nz_preset[1], temp_h_.nz_preset[2], temp_h_.nz_off,
    temp_h_.bd_preset[0], temp_h_.bd_preset[1], temp_h_.bd_preset[2], temp_h_.bd_off,
    temp_h_.nz_minus, temp_h_.nz_plus, temp_h_.bd_minus, temp_h_.bd_plus,
    temp_h_.nz_cur, temp_h_.bd_cur,
    fan_h_.back,
    files_h_.back,
    tune_h_.back, tune_h_.standard, tune_h_.omega,
    tune_h_.cals[0], tune_h_.cals[1], tune_h_.cals[2], tune_h_.cals[3], tune_h_.cals[4],
    more_h_.back, more_h_.wifi, more_h_.expert, more_h_.mesh, more_h_.led, more_h_.system, more_h_.power,
    mesh_h_.back, system_h_.back,
    lights_h_.back, lights_h_.case_off, lights_h_.case_50, lights_h_.case_full,
    lights_h_.hot_off, lights_h_.hot_50, lights_h_.hot_full,
    power_h_.back, power_h_.restart_klipper, power_h_.restart_fw, power_h_.reboot, power_h_.shutdown,
    settings_h_.back, settings_h_.speed, settings_h_.flow, settings_h_.zoff, settings_h_.pa, settings_h_.fan,
    settings_h_.speed_p[0], settings_h_.speed_p[1], settings_h_.speed_p[2],
    settings_h_.flow_p[0], settings_h_.flow_p[1], settings_h_.flow_p[2],
    settings_h_.fan_p[0], settings_h_.fan_p[1], settings_h_.fan_p[2],
    settings_h_.zoff_minus, settings_h_.zoff_plus,
  };
  for (lv_obj_t *t : taps) if (t) lv_obj_add_event_cb(t, &MainPanel::_sub_tap, LV_EVENT_CLICKED, this);
  for (int i = 0; i < 3; i++)
    if (fan_h_.slider[i]) lv_obj_add_event_cb(fan_h_.slider[i], &MainPanel::_fan_slider_cb, LV_EVENT_RELEASED, this);
  if (tune_h_.speed)      lv_obj_add_event_cb(tune_h_.speed, &MainPanel::_fan_slider_cb, LV_EVENT_RELEASED, this);

  // Modal confirm dialog on the top layer (above every sub-screen).
  pono::build_confirm(lv_layer_top(), &confirm_h_);
  if (confirm_h_.cancel)  lv_obj_add_event_cb(confirm_h_.cancel,  &MainPanel::_confirm_tap, LV_EVENT_CLICKED, this);
  if (confirm_h_.confirm) lv_obj_add_event_cb(confirm_h_.confirm, &MainPanel::_confirm_tap, LV_EVENT_CLICKED, this);
  if (confirm_h_.scrim)   lv_obj_add_event_cb(confirm_h_.scrim,   &MainPanel::_confirm_tap, LV_EVENT_CLICKED, this);
}

void MainPanel::confirm(const char *msg, std::function<void()> action) {
  pending_confirm_ = std::move(action);
  if (confirm_h_.msg) lv_label_set_text(confirm_h_.msg, msg);
  if (confirm_h_.scrim)  { lv_obj_clear_flag(confirm_h_.scrim, LV_OBJ_FLAG_HIDDEN); lv_obj_move_foreground(confirm_h_.scrim); }
  if (confirm_h_.card)   { lv_obj_clear_flag(confirm_h_.card,  LV_OBJ_FLAG_HIDDEN); lv_obj_move_foreground(confirm_h_.card); }
}

void MainPanel::_confirm_tap(lv_event_t *e) {
  auto *s = static_cast<MainPanel *>(lv_event_get_user_data(e));
  lv_obj_t *t = lv_event_get_target(e);
  bool ok = (t == s->confirm_h_.confirm);
  if (s->confirm_h_.card)  { lv_obj_add_flag(s->confirm_h_.card,  LV_OBJ_FLAG_HIDDEN); lv_obj_move_background(s->confirm_h_.card); }
  if (s->confirm_h_.scrim) { lv_obj_add_flag(s->confirm_h_.scrim, LV_OBJ_FLAG_HIDDEN); lv_obj_move_background(s->confirm_h_.scrim); }
  if (ok && s->pending_confirm_) s->pending_confirm_();
  s->pending_confirm_ = nullptr;
}

void MainPanel::show_pono(lv_obj_t *scr) {
  lv_obj_t *all[] = {move_scr_, fil_scr_, temp_scr_, fan_scr_, files_scr_, tune_scr_, more_scr_, settings_scr_, mesh_scr_, system_scr_, power_scr_, lights_scr_};
  for (lv_obj_t *s : all) if (s) lv_obj_add_flag(s, LV_OBJ_FLAG_HIDDEN);
  if (home_scr) lv_obj_add_flag(home_scr, LV_OBJ_FLAG_HIDDEN);
  if (scr) { lv_obj_clear_flag(scr, LV_OBJ_FLAG_HIDDEN); lv_obj_move_foreground(scr); }
}

void MainPanel::back_to_home() {
  lv_obj_t *all[] = {move_scr_, fil_scr_, temp_scr_, fan_scr_, files_scr_, tune_scr_, more_scr_, settings_scr_, mesh_scr_, system_scr_, power_scr_, lights_scr_};
  for (lv_obj_t *s : all) if (s) lv_obj_add_flag(s, LV_OBJ_FLAG_HIDDEN);
  show_home();
}

void MainPanel::render_bed_mesh(const json &bm) {
  auto pm = bm[json::json_pointer("/probed_matrix")];
  if (pm.is_array() && pm.size() > 0 && pm[0].is_array()) {
    int rows = (int)pm.size(), cols = (int)pm[0].size();
    mesh_z_.clear();
    float zmin = 1e9f, zmax = -1e9f;
    for (auto &rowj : pm)
      for (auto &cj : rowj) {
        float z = (float)cj.template get<double>();
        mesh_z_.push_back(z);
        if (z < zmin) zmin = z;
        if (z > zmax) zmax = z;
      }
    if (mesh_h_.grid && (int)mesh_z_.size() == rows * cols) {
      pono::mesh_render(mesh_h_.grid, mesh_z_.data(), rows, cols, zmin, zmax);
      if (mesh_h_.range)
        lv_label_set_text(mesh_h_.range, fmt::format("Range: {:.2f} .. {:.2f} mm", zmin, zmax).c_str());
    }
  }
  auto pn = bm[json::json_pointer("/profile_name")];
  if (!pn.is_null() && mesh_h_.profile)
    lv_label_set_text(mesh_h_.profile, fmt::format("Profile: {}", pn.template get<std::string>()).c_str());
}

void MainPanel::populate_system() {
  std::string ver = "unknown";
  { std::ifstream f("/etc/pono-version"); std::string line;
    while (std::getline(f, line)) {
      const std::string key = "DISTRO_VERSION=";
      if (line.rfind(key, 0) == 0) {
        ver = line.substr(key.size());
        if (!ver.empty() && ver.front() == '"') ver.erase(0, 1);
        if (!ver.empty() && ver.back() == '"') ver.pop_back();
        break;
      }
    }
  }
  if (system_h_.version) lv_label_set_text(system_h_.version, ver.c_str());

  std::string host = "pono-print";
  { std::ifstream f("/proc/sys/kernel/hostname"); std::getline(f, host); }
  if (system_h_.host) lv_label_set_text(system_h_.host, host.c_str());

  // Pick the interface that actually holds a routable IPv4. The old code asked
  // only the wifi interface, which reads 0.0.0.0 on this wired-only printer.
  // Prefer ethernet, then wifi, then any other non-loopback link.
  std::string ip;
  auto good = [](const std::string &s) { return !s.empty() && s != "0.0.0.0"; };
  std::vector<std::string> cand{"eth0"};
  std::string wifi_if = KUtils::get_wifi_interface();
  if (!wifi_if.empty()) cand.push_back(wifi_if);
  for (const auto &nm : KUtils::get_interfaces()) cand.push_back(nm);
  for (const auto &nm : cand) {
    if (nm == "lo") continue;
    std::string c = KUtils::interface_ip(nm);
    if (good(c)) { ip = c; break; }
  }
  if (system_h_.ip) lv_label_set_text(system_h_.ip, ip.empty() ? "--" : ip.c_str());

  { std::ifstream f("/proc/uptime"); double up = 0; f >> up;
    int hh = (int)up / 3600, mm = ((int)up % 3600) / 60;
    if (system_h_.uptime) lv_label_set_text(system_h_.uptime, fmt::format("{}h {}m", hh, mm).c_str()); }

  { std::ifstream f("/sys/class/thermal/thermal_zone0/temp"); long mdeg = -1; f >> mdeg;
    if (system_h_.mcu) lv_label_set_text(system_h_.mcu, mdeg > 0 ? fmt::format("{:.1f} C", mdeg / 1000.0).c_str() : "--"); }
}

void MainPanel::_sub_tap(lv_event_t *e) {
  auto *s = static_cast<MainPanel *>(lv_event_get_user_data(e));
  lv_obj_t *t = lv_event_get_target(e);
  pono::MoveHandles &mv = s->move_h_;
  pono::FilamentHandles &fl = s->fil_h_;
  pono::TempsHandles &tp = s->temp_h_;
  pono::FansHandles &fn = s->fan_h_;
  pono::TuneHandles &tu = s->tune_h_;
  // back chips
  if (t == mv.back || t == fl.back || t == tp.back || t == fn.back ||
      t == s->files_h_.back || t == tu.back ||
      t == s->more_h_.back || t == s->settings_h_.back ||
      t == s->mesh_h_.back || t == s->system_h_.back || t == s->power_h_.back ||
      t == s->lights_h_.back) { s->back_to_home(); return; }
  // Move jog (relative)
  double st = s->move_step_;
  if (t == mv.xplus)  { s->ws.gcode_script(fmt::format("G91\nG1 X{} F6000\nG90", st)); return; }
  if (t == mv.xminus) { s->ws.gcode_script(fmt::format("G91\nG1 X-{} F6000\nG90", st)); return; }
  if (t == mv.yplus)  { s->ws.gcode_script(fmt::format("G91\nG1 Y{} F6000\nG90", st)); return; }
  if (t == mv.yminus) { s->ws.gcode_script(fmt::format("G91\nG1 Y-{} F6000\nG90", st)); return; }
  if (t == mv.zplus)  { s->ws.gcode_script(fmt::format("G91\nG1 Z{} F600\nG90", st)); return; }
  if (t == mv.zminus) { s->ws.gcode_script(fmt::format("G91\nG1 Z-{} F600\nG90", st)); return; }
  if (t == mv.home_xy)    { pono::busy_show("Homing X / Y"); s->ws.gcode_script("G28 X Y", [s](json &) { std::lock_guard<std::mutex> lk(s->lv_lock); pono::busy_hide(); }); return; }
  if (t == mv.home_all)   { pono::busy_show("Homing all axes"); s->ws.gcode_script("G28", [s](json &) { std::lock_guard<std::mutex> lk(s->lv_lock); pono::busy_hide(); }); return; }
  if (t == mv.motors_off) { s->ws.gcode_script("M84"); return; }
  for (int i = 0; i < 4; i++) if (t == mv.step[i]) {
    static const double vals[4] = {0.1, 1.0, 10.0, 100.0};
    s->move_step_ = vals[i];
    for (int k = 0; k < 4; k++) {
      if (!mv.step[k]) continue;
      bool on = (k == i);
      lv_obj_set_style_bg_color(mv.step[k], on ? pono::color_accent_primary : pono::color_surface_elevated, 0);
      lv_obj_t *l = lv_obj_get_child(mv.step[k], 0);
      if (l) lv_obj_set_style_text_color(l, on ? pono::color_surface_base : pono::color_text_secondary, 0);
    }
    return;
  }
  // Filament
  if (t == fl.load)    { pono::busy_show("Loading filament"); s->ws.gcode_script("LOAD_FILAMENT", [s](json &) { std::lock_guard<std::mutex> lk(s->lv_lock); pono::busy_hide(); }); return; }
  if (t == fl.unload)  { pono::busy_show("Unloading filament"); s->ws.gcode_script("UNLOAD_FILAMENT", [s](json &) { std::lock_guard<std::mutex> lk(s->lv_lock); pono::busy_hide(); }); return; }
  if (t == fl.extrude) { s->ws.gcode_script("M83\nG1 E25 F300"); return; }
  if (t == fl.retract) { s->ws.gcode_script("M83\nG1 E-25 F1800"); return; }
  if (t == fl.preset[0]) { s->ws.gcode_script("SET_HEATER_TEMPERATURE HEATER=extruder TARGET=220"); return; }
  if (t == fl.preset[1]) { s->ws.gcode_script("SET_HEATER_TEMPERATURE HEATER=extruder TARGET=240"); return; }
  if (t == fl.preset[2]) { s->ws.gcode_script("SET_HEATER_TEMPERATURE HEATER=extruder TARGET=260"); return; }
  if (t == fl.cooldown)  { s->ws.gcode_script("TURN_OFF_HEATERS"); return; }
  // Temps
  if (t == tp.nz_preset[0]) { s->ws.gcode_script("SET_HEATER_TEMPERATURE HEATER=extruder TARGET=220"); return; }
  if (t == tp.nz_preset[1]) { s->ws.gcode_script("SET_HEATER_TEMPERATURE HEATER=extruder TARGET=240"); return; }
  if (t == tp.nz_preset[2]) { s->ws.gcode_script("SET_HEATER_TEMPERATURE HEATER=extruder TARGET=260"); return; }
  if (t == tp.nz_off)       { s->ws.gcode_script("SET_HEATER_TEMPERATURE HEATER=extruder TARGET=0"); return; }
  if (t == tp.bd_preset[0]) { s->ws.gcode_script("SET_HEATER_TEMPERATURE HEATER=heater_bed TARGET=60"); return; }
  if (t == tp.bd_preset[1]) { s->ws.gcode_script("SET_HEATER_TEMPERATURE HEATER=heater_bed TARGET=80"); return; }
  if (t == tp.bd_preset[2]) { s->ws.gcode_script("SET_HEATER_TEMPERATURE HEATER=heater_bed TARGET=75"); return; }
  if (t == tp.bd_off)       { s->ws.gcode_script("SET_HEATER_TEMPERATURE HEATER=heater_bed TARGET=0"); return; }
  // Temps manual steppers: nudge the live target by 5 C (clamped to safe range)
  {
    auto clampi = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };
    if (t == tp.nz_minus) { s->ws.gcode_script(fmt::format("SET_HEATER_TEMPERATURE HEATER=extruder TARGET={}",  clampi(s->home_nozzle_set_ - 5, 0, 330))); return; }
    if (t == tp.nz_plus)  { s->ws.gcode_script(fmt::format("SET_HEATER_TEMPERATURE HEATER=extruder TARGET={}",  clampi(s->home_nozzle_set_ + 5, 0, 330))); return; }
    if (t == tp.bd_minus) { s->ws.gcode_script(fmt::format("SET_HEATER_TEMPERATURE HEATER=heater_bed TARGET={}", clampi(s->home_bed_set_ - 5, 0, 120))); return; }
    if (t == tp.bd_plus)  { s->ws.gcode_script(fmt::format("SET_HEATER_TEMPERATURE HEATER=heater_bed TARGET={}", clampi(s->home_bed_set_ + 5, 0, 120))); return; }
  }
  // Temps keypad: tap the big number to type an exact target (clamped to heater limits)
  if (t == tp.nz_cur) { s->numpad.set_callback([s](double v){ int n=(int)(v+0.5); n=n<0?0:(n>330?330:n); s->ws.gcode_script(fmt::format("SET_HEATER_TEMPERATURE HEATER=extruder TARGET={}",  n)); }); s->numpad.foreground_reset(); return; }
  if (t == tp.bd_cur) { s->numpad.set_callback([s](double v){ int n=(int)(v+0.5); n=n<0?0:(n>120?120:n); s->ws.gcode_script(fmt::format("SET_HEATER_TEMPERATURE HEATER=heater_bed TARGET={}", n)); }); s->numpad.foreground_reset(); return; }
  // Fans (quick)
  // Tune
  if (t == tu.standard) { s->ws.gcode_script("PONO_CAL_STANDARD"); return; }
  if (t == tu.omega)    { s->ws.gcode_script("PONO_CAL_OMEGA"); return; }
  for (int i = 0; i < 5; i++) if (t == tu.cals[i]) { s->ws.gcode_script("PONO_CAL_STANDARD"); return; }  // individual cals -> guided standard (placeholder)
  // More menu rows
  if (t == s->more_h_.wifi)    { s->setting_panel.show_wifi(); return; }       // reuse the wpa scan/connect panel
  if (t == s->more_h_.expert)  { s->show_pono(s->settings_scr_); return; }     // Expert Tune surface
  if (t == s->more_h_.mesh)    { s->show_pono(s->mesh_scr_); return; }         // Bed mesh heatmap
  if (t == s->more_h_.system)  { s->populate_system(); s->show_pono(s->system_scr_); return; }
  if (t == s->more_h_.power)   { s->show_pono(s->power_scr_); return; }
  if (t == s->more_h_.led)     {
    lv_obj_t *cb[3] = {s->lights_h_.case_off, s->lights_h_.case_50, s->lights_h_.case_full};
    lv_obj_t *hb[3] = {s->lights_h_.hot_off,  s->lights_h_.hot_50,  s->lights_h_.hot_full};
    pono::seg_highlight(cb, 3, s->led_case_level_);
    pono::seg_highlight(hb, 3, s->led_hot_level_);
    s->show_pono(s->lights_scr_); return;
  }
  // Power actions
  if (t == s->power_h_.restart_klipper) { s->confirm("Restart Klipper?",       [s]{ s->ws.gcode_script("RESTART"); }); return; }
  if (t == s->power_h_.restart_fw)      { s->confirm("Restart firmware?",      [s]{ s->ws.gcode_script("FIRMWARE_RESTART"); }); return; }
  if (t == s->power_h_.reboot)          { s->confirm("Reboot the printer?",    [s]{ s->ws.send_jsonrpc("machine.reboot"); }); return; }
  if (t == s->power_h_.shutdown)        { s->confirm("Shut down the printer?", [s]{ s->ws.send_jsonrpc("machine.shutdown"); }); return; }
  // Lights (SET_LED white channel) - fire the command AND move the highlight
  // to the chosen level so the active selection is visible.
  pono::LightsHandles &li = s->lights_h_;
  lv_obj_t *cb[3] = {li.case_off, li.case_50, li.case_full};
  lv_obj_t *hb[3] = {li.hot_off,  li.hot_50,  li.hot_full};
  if (t == li.case_off)  { s->ws.gcode_script("SET_LED LED=case WHITE=0");    s->led_case_level_ = 0; pono::seg_highlight(cb, 3, 0); return; }
  if (t == li.case_50)   { s->ws.gcode_script("SET_LED LED=case WHITE=0.5");  s->led_case_level_ = 1; pono::seg_highlight(cb, 3, 1); return; }
  if (t == li.case_full) { s->ws.gcode_script("SET_LED LED=case WHITE=1.0");  s->led_case_level_ = 2; pono::seg_highlight(cb, 3, 2); return; }
  if (t == li.hot_off)   { s->ws.gcode_script("SET_LED LED=hotend WHITE=0");   s->led_hot_level_ = 0; pono::seg_highlight(hb, 3, 0); return; }
  if (t == li.hot_50)    { s->ws.gcode_script("SET_LED LED=hotend WHITE=0.5"); s->led_hot_level_ = 1; pono::seg_highlight(hb, 3, 1); return; }
  if (t == li.hot_full)  { s->ws.gcode_script("SET_LED LED=hotend WHITE=1.0"); s->led_hot_level_ = 2; pono::seg_highlight(hb, 3, 2); return; }
  // Expert Tune (live): value pills open the keypad, presets apply directly,
  // z-offset uses live babystep. Every control writes straight to Klipper and
  // updates its pill so the change is visible immediately.
  pono::SettingsHandles &se = s->settings_h_;
  if (t == se.speed) { s->numpad.set_callback([s](double v){ int sp=(int)(v+0.5); sp=sp<10?10:(sp>300?300:sp); s->ws.gcode_script(fmt::format("M220 S{}", sp)); pono::pill_set(s->settings_h_.speed, fmt::format("{}%", sp).c_str()); }); s->numpad.foreground_reset(); return; }
  if (t == se.flow)  { s->numpad.set_callback([s](double v){ int fl=(int)(v+0.5); fl=fl<50?50:(fl>200?200:fl); s->ws.gcode_script(fmt::format("M221 S{}", fl)); pono::pill_set(s->settings_h_.flow, fmt::format("{}%", fl).c_str()); }); s->numpad.foreground_reset(); return; }
  if (t == se.pa)    { s->numpad.set_callback([s](double v){ double a=v<0?0:(v>1.0?1.0:v); s->ws.gcode_script(fmt::format("SET_PRESSURE_ADVANCE ADVANCE={:.3f}", a)); pono::pill_set(s->settings_h_.pa, fmt::format("{:.3f}", a).c_str()); }); s->numpad.foreground_reset(); return; }
  if (t == se.zoff)  { s->numpad.set_callback([s](double v){ s->tune_zoff_=v; s->ws.gcode_script(fmt::format("SET_GCODE_OFFSET Z={:.3f} MOVE=0", v)); pono::pill_set(s->settings_h_.zoff, fmt::format("{:.3f}", v).c_str()); }); s->numpad.foreground_reset(); return; }
  if (t == se.fan)   { s->numpad.set_callback([s](double v){ int p=(int)(v+0.5); p = p<0?0:(p>100?100:p); s->ws.gcode_script(fmt::format("M106 S{}", p*255/100)); pono::pill_set(s->settings_h_.fan, fmt::format("{}%", p).c_str()); }); s->numpad.foreground_reset(); return; }
  if (t == se.speed_p[0]) { s->ws.gcode_script("M220 S50");  pono::pill_set(se.speed, "50%");  return; }
  if (t == se.speed_p[1]) { s->ws.gcode_script("M220 S100"); pono::pill_set(se.speed, "100%"); return; }
  if (t == se.speed_p[2]) { s->ws.gcode_script("M220 S150"); pono::pill_set(se.speed, "150%"); return; }
  if (t == se.flow_p[0])  { s->ws.gcode_script("M221 S95");  pono::pill_set(se.flow, "95%");  return; }
  if (t == se.flow_p[1])  { s->ws.gcode_script("M221 S100"); pono::pill_set(se.flow, "100%"); return; }
  if (t == se.flow_p[2])  { s->ws.gcode_script("M221 S105"); pono::pill_set(se.flow, "105%"); return; }
  if (t == se.fan_p[0])   { s->ws.gcode_script("M106 S0");   pono::pill_set(se.fan, "0%");   return; }
  if (t == se.fan_p[1])   { s->ws.gcode_script("M106 S128"); pono::pill_set(se.fan, "50%");  return; }
  if (t == se.fan_p[2])   { s->ws.gcode_script("M106 S255"); pono::pill_set(se.fan, "100%"); return; }
  if (t == se.zoff_minus) { s->tune_zoff_ -= 0.01; s->ws.gcode_script("SET_GCODE_OFFSET Z_ADJUST=-0.01 MOVE=0"); pono::pill_set(se.zoff, fmt::format("{:.3f}", s->tune_zoff_).c_str()); return; }
  if (t == se.zoff_plus)  { s->tune_zoff_ += 0.01; s->ws.gcode_script("SET_GCODE_OFFSET Z_ADJUST=0.01 MOVE=0");  pono::pill_set(se.zoff, fmt::format("{:.3f}", s->tune_zoff_).c_str()); return; }
}

void MainPanel::_fan_slider_cb(lv_event_t *e) {
  auto *s = static_cast<MainPanel *>(lv_event_get_user_data(e));
  lv_obj_t *t = lv_event_get_target(e);
  int v = lv_slider_get_value(t);
  if (t == s->tune_h_.speed) {                 // Expert Tune feedrate (M220), not a fan
    s->ws.gcode_script(fmt::format("M220 S{}", v));
    if (s->tune_h_.speed_val) lv_label_set_text(s->tune_h_.speed_val, fmt::format("{}%", v).c_str());
    return;
  }
  // Per-fan sliders: 0 part-cooling (M106), 1 model fan, 2 box fan (generics).
  if (t == s->fan_h_.slider[0])       s->ws.gcode_script(fmt::format("M106 S{}", (int)(v * 255 / 100)));
  else if (t == s->fan_h_.slider[1])  s->ws.gcode_script(fmt::format("SET_FAN_SPEED FAN=model_helper_fan SPEED={:.2f}", v / 100.0));
  else if (t == s->fan_h_.slider[2])  s->ws.gcode_script(fmt::format("SET_FAN_SPEED FAN=box_fan SPEED={:.2f}", v / 100.0));
  else return;
  for (int i = 0; i < 3; i++)
    if (t == s->fan_h_.slider[i] && s->fan_h_.val[i])
      lv_label_set_text(s->fan_h_.val[i], fmt::format("{}%", v).c_str());
}

void MainPanel::_file_row_cb(lv_event_t *e) {
  auto *s = static_cast<MainPanel *>(lv_event_get_user_data(e));
  lv_obj_t *row = lv_event_get_target(e);
  size_t idx = (size_t)(uintptr_t)lv_obj_get_user_data(row);
  if (idx < s->files_names_.size()) {
    std::string fn = s->files_names_[idx];
    std::string base = fn.substr(fn.find_last_of('/') + 1);  // npos+1 == 0 -> whole string
    if (base.size() > 38) base = base.substr(0, 36) + "..";
    s->confirm(fmt::format("Print {}?", base).c_str(), [s, fn]{
      json p = {{"filename", fn}};
      s->ws.send_jsonrpc("printer.print.start", p, [](json &) {});
      s->back_to_home();
    });
  }
}

void MainPanel::populate_files() {
  if (!files_h_.list) return;
  json p = {{"root", "gcodes"}};
  ws.send_jsonrpc("server.files.list", p, [this](json &j) {
    std::lock_guard<std::mutex> lock(this->lv_lock);
    if (!this->files_h_.list) return;
    lv_obj_clean(this->files_h_.list);
    this->files_names_.clear();
    uint32_t gen = ++this->files_gen_;  // any async metadata from a prior populate is now stale
    auto &res = j["/result"_json_pointer];
    if (res.is_array()) {
      int n = 0;
      for (auto &f : res) {
        if (n++ >= 40) break;
        std::string path = f.value("path", std::string());
        if (path.empty()) continue;
        this->files_names_.push_back(path);
        double sz = f.value("size", 0.0);  // bytes (server.files.list carries size)
        std::string meta = sz >= 1048576.0 ? fmt::format("{:.1f} MB", sz / 1048576.0)
                         : sz >= 1024.0     ? fmt::format("{:.0f} KB", sz / 1024.0)
                                            : fmt::format("{:.0f} B", sz);
        pono::files_add_row(this->files_h_.list, path.c_str(), meta.c_str());
        lv_obj_t *row = lv_obj_get_child(this->files_h_.list,
                                         lv_obj_get_child_cnt(this->files_h_.list) - 1);
        if (row) {
          lv_obj_set_user_data(row, (void *)(uintptr_t)(this->files_names_.size() - 1));
          lv_obj_add_event_cb(row, &MainPanel::_file_row_cb, LV_EVENT_CLICKED, this);
          // async per-file metadata: thumbnail + est time + filament type
          json mp = {{"filename", path}};
          this->ws.send_jsonrpc("server.files.metadata", mp, [this, gen, row, path](json &meta) {
            std::lock_guard<std::mutex> mlock(this->lv_lock);
            if (gen != this->files_gen_) return;  // Files list was rebuilt; row is stale
            auto thumb = KUtils::get_thumbnail(path, meta, 0.17);
            std::string ipath = thumb.first.empty() ? "" : ("A:" + thumb.first);
            int zoom = thumb.second > 0 ? (int)(46 * 256 / (int)thumb.second) : 0;
            std::string m;
            auto et = meta["/result/estimated_time"_json_pointer];
            if (et.is_number()) {
              int secs = (int)et.template get<double>();
              m = secs >= 3600 ? fmt::format("{}h {}m", secs / 3600, (secs % 3600) / 60)
                               : fmt::format("{}m", secs / 60);
            }
            auto ft = meta["/result/filament_type"_json_pointer];
            if (ft.is_string()) {
              auto t = ft.template get<std::string>();
              m = m.empty() ? t : m + "  .  " + t;
            }
            pono::files_apply_meta(row, ipath.c_str(), zoom, m.c_str());
          });
        }
      }
    }
    if (this->files_names_.empty())
      pono::files_add_row(this->files_h_.list, "No gcode files", "upload via Mainsail");
  });
}

void MainPanel::attach_home_taps() {
  lv_obj_t *taps[] = { home_h.btn_pausestop, home_h.btn_cancel, home_h.qa[0], home_h.qa[1],
                       home_h.qa[2], home_h.qa[3], home_h.tile_nozzle, home_h.tile_bed,
                       home_h.tile_tune, home_h.tile_omega, home_h.tile_more };
  for (lv_obj_t *t : taps) if (t) lv_obj_add_event_cb(t, &MainPanel::_home_tap, LV_EVENT_CLICKED, this);
}

// Rebuild the cockpit in the layout matching the current state. build_home is
// sim-verified for both idle and printing, so a rebuild guarantees the right
// layout rather than swapping fonts/labels/colors in place. Called only on the
// idle<->printing flip (rare), so the cost is irrelevant.
void MainPanel::rebuild_home() {
  if (!home_scr) return;
  pono::HomeModel m{};
  m.printing = home_printing_ || home_paused_;   // printing layout for both (frozen while paused)
  m.paused = home_paused_;
  m.progress_pct = (int)(home_progress_ * 100.0 + 0.5);
  m.layer = home_layer_; m.layer_total = home_layer_total_;
  m.job_name = home_job_.c_str();
  m.material = "PA-CF . 0.25 diamond";
  m.nozzle = home_nozzle_; m.nozzle_set = home_nozzle_set_;
  m.bed = home_bed_; m.bed_set = home_bed_set_;
  if (m.printing && home_progress_ > 0.01 && home_duration_ > 1.0) {
    double remain = home_duration_ * (1.0 - home_progress_) / home_progress_;
    if (remain < 0.0) remain = 0.0;
    int mins = (int)(remain / 60.0 + 0.5);
    home_eta_ = mins >= 60 ? fmt::format("{}:{:02d} left", mins / 60, mins % 60)
                           : fmt::format("{} min left", mins);
  } else {
    home_eta_.clear();
  }
  m.eta = home_eta_.c_str();
  lv_obj_clean(home_scr);                  // drop old children + their anims
  pono::build_home(home_scr, m, &home_h);  // repopulates home_h with fresh handles
  attach_home_taps();
  home_pulsing_ = home_printing_ && !home_paused_;  // build_home pulses only while actively printing
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
  // Legacy tabview button (hidden under the Pono cockpit). The live print entry
  // is the cockpit Files tile -> _file_row_cb -> printer.print.start.
  (void)event;
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
