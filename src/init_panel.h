#ifndef __INIT_PANEL_H__
#define __INIT_PANEL_H__

#include "lvgl/lvgl.h"
#include "websocket_client.h"
#include "main_panel.h"
#include "print_status_panel.h"

#include <mutex>

class InitPanel {
 public:
  InitPanel(MainPanel &mp, std::mutex &l);
  ~InitPanel();

  void connected(KWebSocketClient &ws);
  void disconnected(KWebSocketClient &ws);
  void set_message(const char *message);

 private:
  void arm_spinner();    // (re)start the comet spinner anim (reconnect)
  lv_obj_t *cont;
  lv_obj_t *label;       // connection status line (set_message updates this)
  lv_obj_t *joke_label;  // persistent small Hawaii boot joke
  lv_obj_t *spinner;     // canned comet spinner ("still working" cue); anim stopped on connect
  MainPanel &main_panel;
  std::mutex &lv_lock;
};

#endif // __INIT_PANEL_H__
