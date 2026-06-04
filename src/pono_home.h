// SPDX-License-Identifier: GPL-3.0-only
// pono_home.h - Pono Print home cockpit builder
//
// The home screen redesign (task #33). Pure LVGL + pono_theme: NO websocket,
// state, Moonraker, fmt or libhv deps, so it links into the headless sim
// (sim/pono_headless.cpp) AND the real app (main_panel.cpp) from one source.
//
// The sim feeds a demo HomeModel; MainPanel feeds live Moonraker values and
// wires the real callbacks. Layout mirrors docs/pono-home-mockup.svg frame 1.
#pragma once
#include "lvgl.h"

namespace pono {

// Plain data the home renders. No LVGL, no app types: trivially constructed
// by the sim and populated from Moonraker status in the real app.
struct HomeModel {
  bool printing;
  int  progress_pct;     // 0..100
  int  layer;
  int  layer_total;
  const char *job_name;  // "DA OMEGA CUBE"
  const char *material;  // "PA-CF . 0.25 diamond"
  int  nozzle;           // current C
  int  nozzle_set;       // target C
  int  bed;              // current C
  int  bed_set;          // target C
  const char *eta;       // "1:12 left"
};

// The mockup's values, for sim render + first-boot placeholder.
HomeModel demo_home_model();

// Idle-state values (no print running) - the state the printer sits in most.
HomeModel demo_home_idle_model();

// Live handles into a built home. The app keeps these to update values in
// place (no rebuild) and to attach tap callbacks. NULL-safe: every field may
// be null, callers must guard. The sim ignores it (passes nullptr).
struct HomeHandles {
  // live-updated values
  lv_obj_t *arc = nullptr;         // progress arc (lv_arc_set_value)
  lv_obj_t *pct = nullptr;         // "47%"
  lv_obj_t *layer = nullptr;       // "layer 84 / 180"
  lv_obj_t *job = nullptr;         // job name
  lv_obj_t *material = nullptr;    // material/profile line
  lv_obj_t *eta = nullptr;         // "1:12 left"
  lv_obj_t *nozzle = nullptr;      // nozzle current-temp number
  lv_obj_t *nozzle_set = nullptr;  // nozzle target "/250"
  lv_obj_t *bed = nullptr;         // bed current-temp number
  lv_obj_t *bed_set = nullptr;     // bed target "/60"
  lv_obj_t *state_pill = nullptr;  // PRINTING pill (hide when idle)
  lv_obj_t *state_dot = nullptr;   // pulsing beat inside the pill (anim gated to printing)
  // tappable launchers (app attaches event cbs)
  lv_obj_t *tile_tune = nullptr;
  lv_obj_t *tile_omega = nullptr;
  lv_obj_t *tile_nozzle = nullptr;
  lv_obj_t *tile_bed = nullptr;
  lv_obj_t *btn_pausestop = nullptr;
  lv_obj_t *qa[4] = {nullptr, nullptr, nullptr, nullptr}; // Move/Filament/Files/Fans
  lv_obj_t *tile_more = nullptr;  // 5th nav tile -> More menu
};

// Build the full 480x272 home cockpit into `parent` (a screen-sized object).
// Returns the arc; if `out` is non-null, fills it with live handles + tap
// targets so the app can update values and wire callbacks. Sim passes nullptr.
lv_obj_t *build_home(lv_obj_t *parent, const HomeModel &m, HomeHandles *out = nullptr);

// Start/stop the small "alive" pulse on the PRINTING pill's dot. Call only on
// the idle<->printing transition; a per-frame restart would stutter the beat.
// Keeps the always-on idle dashboard at zero animation cost.
void set_state_pulse(lv_obj_t *dot, bool on);

// ---- Native sub-screen handles (the app wires actions + live values) ----
struct MoveHandles {
  lv_obj_t *back = nullptr, *pos = nullptr;
  lv_obj_t *xplus = nullptr, *xminus = nullptr, *yplus = nullptr, *yminus = nullptr;
  lv_obj_t *zplus = nullptr, *zminus = nullptr;
  lv_obj_t *home_xy = nullptr, *home_all = nullptr, *motors_off = nullptr;
  lv_obj_t *step[4] = {nullptr, nullptr, nullptr, nullptr};  // 0.1 / 1 / 10 / 100 mm
};

// Build the native Move (jog) screen: XY cross, Z jog, step selector, home/off,
// and a live position readout. The app wires taps to relative moves + homing.
void build_move(lv_obj_t *parent, MoveHandles *h = nullptr);

struct FilamentHandles {
  lv_obj_t *back = nullptr, *temp = nullptr;
  lv_obj_t *load = nullptr, *unload = nullptr, *extrude = nullptr, *retract = nullptr;
  lv_obj_t *preset[3] = {nullptr, nullptr, nullptr};  // PLA / PETG / PA-CF preheat
  lv_obj_t *cooldown = nullptr;
};
void build_filament(lv_obj_t *parent, FilamentHandles *h = nullptr);

struct TempsHandles {
  lv_obj_t *back = nullptr;
  lv_obj_t *nz_cur = nullptr, *nz_tgt = nullptr, *bd_cur = nullptr, *bd_tgt = nullptr;
  lv_obj_t *nz_preset[3] = {nullptr, nullptr, nullptr}, *nz_off = nullptr;
  lv_obj_t *bd_preset[3] = {nullptr, nullptr, nullptr}, *bd_off = nullptr;
  lv_obj_t *nz_minus = nullptr, *nz_plus = nullptr;  // manual -/+ step (nozzle)
  lv_obj_t *bd_minus = nullptr, *bd_plus = nullptr;  // manual -/+ step (bed)
};
void build_temps(lv_obj_t *parent, TempsHandles *h = nullptr);

// More menu: the 5th cockpit tile. Lists secondary tools (Wi-Fi, Expert Tune,
// restart) as tappable rows. The app wires each row to its action.
struct MoreHandles {
  lv_obj_t *back = nullptr;
  lv_obj_t *wifi = nullptr, *expert = nullptr, *restart = nullptr;
};
void build_more(lv_obj_t *parent, MoreHandles *h = nullptr);

struct FansHandles {
  lv_obj_t *back = nullptr;
  lv_obj_t *part_val = nullptr, *part_slider = nullptr, *aux_val = nullptr;
  lv_obj_t *off = nullptr, *p50 = nullptr, *full = nullptr;
};
void build_fans(lv_obj_t *parent, FansHandles *h = nullptr);

struct FilesHandles {
  lv_obj_t *back = nullptr, *list = nullptr;
};
void build_files(lv_obj_t *parent, FilesHandles *h = nullptr);
// Append a file row to the Files list (the app populates from Moonraker).
void files_add_row(lv_obj_t *list, const char *name, const char *meta);

struct TuneHandles {
  lv_obj_t *back = nullptr, *standard = nullptr, *omega = nullptr;
  lv_obj_t *speed = nullptr, *speed_val = nullptr;
  lv_obj_t *cals[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};  // Bed Mesh/PA/Flow/Shaper/Z-Offset
};

// Build the Tune screen into `parent`: the two calibrate tiers (Standard +
// the enhanced OMEGA), a grid of individual calibrations (bed mesh, pressure
// advance, flow, input shaper, z-offset), and a live speed slider. Pure LVGL
// + theme; the real app wires taps + the slider to Moonraker.
void build_tune(lv_obj_t *parent, TuneHandles *h = nullptr);

// Build the Expert Tune screen into `parent`: category chips + a scrollable
// list of tunable parameters with real editors (slider / switch / value
// field). The full-tuneability surface; the real app populates it from the
// shared settings schema and writes changes back. If back_out is non-null it
// receives the back chip so the app can wire return-to-home navigation.
void build_settings(lv_obj_t *parent, lv_obj_t **back_out = nullptr);

} // namespace pono
