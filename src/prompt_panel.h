#ifndef __PROMPT_PANEL_H__
#define __PROMPT_PANEL_H__

#include "lvgl/lvgl.h"
#include "websocket_client.h"
#include "notify_consumer.h"
#include "button_container.h"
#include "prompt_layout.h"

#include <map>
#include <memory>
#include <mutex>

struct SharedButton {
    SharedButton(lv_obj_t *bbtn) : btn(bbtn) {};
    lv_obj_t *btn;
};

class PromptPanel : public NotifyConsumer {
    public:
        PromptPanel(KWebSocketClient &ws, std::mutex &lock, lv_obj_t *parent);
        ~PromptPanel();

        void handle_macro_response(json &j);
        void consume(json &j);

        lv_obj_t *get_container();
        void handle_callback(lv_event_t *event);

        static void _handle_callback(lv_event_t *event) {
            PromptPanel *panel = (PromptPanel*)event->user_data;
            panel->handle_callback(event);
        };

        void foreground();
        void background();
        void reset();   // link-loss: force the dialog down + clear showing_ (caller holds lv_lock)
        bool is_showing() const { return showing_; }  // a prompt is up -> cockpit cal overlay must yield

    private:

        void check_height();

        KWebSocketClient &ws;
        pono::PromptLayout layout;   // owns the card/header/body/footer geometry
        lv_obj_t *promptpanel_cont;
        lv_obj_t *prompt_cont;       // == layout.cont
        lv_obj_t *flex;              // == layout.body
        lv_obj_t *header;            // == layout.header
        lv_obj_t *button_group_cont;
        lv_obj_t *footer_cont;       // == layout.footer
        bool showing_ = false;   // set on action:prompt_show, cleared on prompt_end (both under lv_lock)

};

#endif // __PROMPT_PANEL_H__
