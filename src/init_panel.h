#ifndef __INIT_PANEL_H__
#define __INIT_PANEL_H__

#include "lvgl/lvgl.h"
#include "websocket_client.h"
#include "main_panel.h"
#include "pono_home.h"   // pono::BootHandles + build_boot/boot_set_progress

#include <mutex>
#include <string>
#include <vector>

class InitPanel {
 public:
  InitPanel(MainPanel &mp, std::mutex &l);
  ~InitPanel();

  void connected(KWebSocketClient &ws);
  void disconnected(KWebSocketClient &ws);
  void set_message(const char *message);

 private:
  void load_jokes();                          // fill jokes_ from the device joke book
  void cycle_joke();                          // advance to the next island joke (timer cb)
  void reveal_progress();                     // Act 2: phase in progress, stop the joke (once)
  void set_stage(int pct, const char *msg);   // progress + status; takes lv_lock itself

  lv_obj_t *cont;                 // full-screen boot container
  pono::BootHandles boot_;        // flying flag + joke + status + bar
  std::vector<std::string> jokes_;
  size_t joke_idx_ = 0;
  lv_timer_t *joke_timer_ = nullptr;  // rotates the joke while we wait
  bool progress_shown_ = false;       // Act 2 latch (reveal once, on first connect)
  unsigned conn_epoch_ = 0;           // bumped on disconnect; a stale in-flight connect reply checks it before hiding boot
  MainPanel &main_panel;
  std::mutex &lv_lock;
};

#endif // __INIT_PANEL_H__
