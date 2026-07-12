// SPDX-License-Identifier: GPL-3.0-only
// pono_stress.cpp - lifecycle + leak stress for the Pono Print UI (headless).
//
// The one-shot renderer proves a screen DRAWS. This proves the screens survive
// being built, driven, and torn down thousands of times, and that the top-layer
// overlays (busy / omega / cal-log) and their hide() paths do not leak. It
// targets the historically-fragile paths: the idle<->printing<->paused model
// swap (a past use-after-free), overlay show/hide churn, and the boot lifecycle.
//
//   pono-stress.exe [iters]     default 1000
//
// Leak signal is process RSS, not lv_mem_monitor: the sim builds with
// LV_MEM_CUSTOM=1 (malloc/free), under which lv_mem_monitor reports all zeros,
// so it cannot see a leak. RSS is measured directly from the OS and is
// authoritative regardless of the allocator. Exit 0 + "STRESS CLEAN" (flat RSS
// after warmup) = pass; an LVGL assert aborts (nonzero); sustained RSS growth
// across the run is a leak (exit 2).

#include "lvgl.h"
#include "pono_theme.h"
#include "pono_home.h"
#include "pono_anim.h"

#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <string>

#if defined(_WIN32)
  #include <windows.h>
  #include <psapi.h>
#elif defined(__linux__)
  #include <cstdio>
  #include <unistd.h>
#endif

#define PW 480
#define PH 272

static uint32_t g_tick_ms = 0;
extern "C" uint32_t custom_tick_get(void) { return g_tick_ms; }
extern "C" void lv_fs_stdio_init(void) {}
extern "C" void lv_png_init(void) {}

static lv_color_t g_buf[PW * PH];
static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *, lv_color_t *) { lv_disp_flush_ready(drv); }

static void advance(int ms) {
  for (int t = 0; t < ms; t += 16) { g_tick_ms += 16; lv_timer_handler(); }
}

// Resident set size in bytes, straight from the OS. Works under LV_MEM_CUSTOM
// (where lv_mem_monitor is blind). Returns 0 if the platform query fails.
static size_t rss_bytes() {
#if defined(_WIN32)
  PROCESS_MEMORY_COUNTERS pmc;
  if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
    return (size_t)pmc.WorkingSetSize;
  return 0;
#elif defined(__linux__)
  FILE *f = fopen("/proc/self/statm", "r");
  if (!f) return 0;
  long total = 0, resident = 0;
  int got = fscanf(f, "%ld %ld", &total, &resident);
  fclose(f);
  if (got != 2) return 0;
  return (size_t)resident * (size_t)sysconf(_SC_PAGESIZE);
#else
  return 0;  // unknown platform: leak check unavailable, lifecycle still valid
#endif
}

static unsigned long long kb(size_t bytes) { return (unsigned long long)(bytes / 1024); }

int main(int argc, char **argv) {
  int iters = argc > 1 ? atoi(argv[1]) : 1000;

  lv_init();
  static lv_disp_draw_buf_t dbuf;
  static lv_color_t buf1[PW * PH];
  lv_disp_draw_buf_init(&dbuf, buf1, NULL, PW * PH);
  static lv_disp_drv_t ddrv;
  lv_disp_drv_init(&ddrv);
  ddrv.hor_res = PW; ddrv.ver_res = PH; ddrv.flush_cb = flush_cb; ddrv.draw_buf = &dbuf;
  lv_disp_t *disp = lv_disp_drv_register(&ddrv);
  pono::theme_init(disp);

  // The E-STOP rides lv_layer_top for the whole session (built once, like the app).
  pono::build_estop(lv_layer_top());

  advance(200);
  size_t base_rss = rss_bytes();
  size_t peak_rss = base_rss;
  const bool have_rss = base_rss != 0;
  printf("start: rss=%llu KB  iters=%d  (leak signal=%s)\n",
         kb(base_rss), iters, have_rss ? "process RSS" : "UNAVAILABLE on this platform");

  size_t sample_at_500 = 0;
  for (int i = 0; i < iters; i++) {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);                 // tear down the previous screen's tree

    switch (i % 14) {
      case 0: pono::build_home(scr, pono::demo_home_model()); break;
      case 1: pono::build_home(scr, pono::demo_home_idle_model()); break;
      case 2: pono::build_home(scr, pono::demo_home_paused_model()); break;
      case 3: { static pono::HomeHandles hh; pono::build_home(scr, pono::demo_home_model(), &hh);
                pono::home_set_stale(&hh, 23); } break;
      case 4: pono::build_home(scr, pono::demo_home_idle_model()); pono::busy_show("Homing all axes");
              advance(300); pono::busy_hide(); break;
      case 5: pono::build_home(scr, pono::demo_home_model()); pono::omega_status_show("OMEGA 8/29: Bridging OK");
              advance(300); pono::omega_status_hide(); break;
      case 6: pono::build_home(scr, pono::demo_home_idle_model());
              pono::cal_log_show("Finding true Z", "next bed mesh", 5, 40, false, nullptr, nullptr);
              advance(300); pono::cal_log_hide(); break;
      case 7: { static pono::BootHandles bh; pono::build_boot(scr, &bh);
                pono::boot_play_intro(&bh); pono::boot_set_progress(&bh, 60, "Loading printer state...");
                advance(3200); } break;   // let the boot one-shot timers fire before teardown
      case 8: { static pono::SystemHandles sh; pono::build_system(scr, &sh);
                const pono::IntegrityState st[] = { pono::IntegrityState::OfficialSigned,
                                                    pono::IntegrityState::Unofficial,
                                                    pono::IntegrityState::Unknown };
                pono::system_set_integrity(&sh, st[i % 3]); } break;
      case 9:  pono::build_move(scr); break;
      case 10: pono::build_filament(scr); break;
      case 11: pono::build_temps(scr); break;
      case 12: pono::build_files(scr); break;
      case 13: pono::build_tune(scr); break;
    }
    advance(120);                       // drive glow pulse / ocean drift / one-shots
    lv_refr_now(disp);

    size_t r = rss_bytes();
    if (r > peak_rss) peak_rss = r;
    if (i == 500) sample_at_500 = r;
    if (i && i % 500 == 0)
      printf("  iter %4d: rss=%llu KB  peak=%llu KB\n", i, kb(r), kb(peak_rss));
  }

  // settle + final read
  lv_obj_clean(lv_scr_act());
  advance(200);
  size_t end_rss = rss_bytes();
  long net_kb = (long)kb(end_rss) - (long)kb(base_rss);
  // Post-warmup delta: the fonts/draw-buffers are static and the screen pool
  // fills in the first few hundred iters, so mid[500]->end is the real leak
  // signal. base->end includes one-time warmup and is informational.
  long warm_kb = sample_at_500 ? (long)kb(end_rss) - (long)kb(sample_at_500) : 0;
  printf("end:   rss=%llu KB  peak=%llu KB\n", kb(end_rss), kb(peak_rss));
  printf("net growth base->end: %+ld KB  (post-warmup mid[500]->end: %+ld KB)\n", net_kb, warm_kb);

  if (!have_rss) {
    // Lifecycle still proven (no assert/crash), but leaks go unchecked.
    printf("STRESS LIFECYCLE-ONLY: %d cycles, no assert/crash; RSS unavailable, leak NOT checked\n", iters);
    return 0;
  }
  // RSS is page-granular and the allocator caches, so tolerate a few hundred KB
  // of post-warmup noise; a real per-iter leak grows RSS by megabytes over
  // thousands of iterations and clears this bar easily.
  if (sample_at_500 && warm_kb > 2048) {
    printf("STRESS LEAK SUSPECT: RSS grew %+ld KB post-warmup across %d iters\n", warm_kb, iters);
    return 2;
  }
  printf("STRESS CLEAN: %d build/teardown cycles, no assert/crash, RSS flat post-warmup (%+ld KB)\n", iters, warm_kb);
  return 0;
}
