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

// Build the full 480x272 home cockpit into `parent` (a screen-sized object).
// Adds the living ocean background, left rail, top bar, job hero (progress
// ring), glanceable temp strip, OMEGA tile and quick-action row. Returns the
// arc object so callers that animate progress live can keep a handle.
lv_obj_t *build_home(lv_obj_t *parent, const HomeModel &m);

// Build the Tune screen into `parent`: the two calibrate tiers (Standard +
// the enhanced OMEGA), a grid of individual calibrations (bed mesh, pressure
// advance, flow, input shaper, z-offset), and a live speed slider. Pure LVGL
// + theme; the real app wires taps + the slider to Moonraker.
void build_tune(lv_obj_t *parent);

} // namespace pono
