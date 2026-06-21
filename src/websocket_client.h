#ifndef __KWEBSOCKET_CLIENT_H__
#define __KWEBSOCKET_CLIENT_H__

#include "hv/WebSocketClient.h"
#include "notify_consumer.h"
#include "hv/json.hpp"

#include <map>
#include <vector>
#include <atomic>
#include <functional>
#include <mutex>

using json = nlohmann::json;

class KWebSocketClient : public hv::WebSocketClient {
 public:
  KWebSocketClient(hv::EventLoopPtr loop);
  ~KWebSocketClient();

  int connect(const char* url,
	      std::function<void()> connected,
	      std::function<void()> disconnected);

  void register_notify_update(NotifyConsumer *consumer);
  void unregister_notify_update(NotifyConsumer *consumer);

  // void register_gcode_resp(std::function<void(json&)> cb);

  int send_jsonrpc(const std::string &method, std::function<void(json&)> cb);
  int send_jsonrpc(const std::string &method, const json &params, std::function<void(json&)> cb);  
  int send_jsonrpc(const std::string &method, const json &params, NotifyConsumer *consumer);  
  int send_jsonrpc(const std::string &method, const json &params);
  int send_jsonrpc(const std::string &method);
  int gcode_script(const std::string &gcode);
  int gcode_script(const std::string &gcode, std::function<void(json&)> cb);

  void register_method_callback(std::string resp_method,
				std::string handler_name,
				std::function<void(json&)> cb);

  // Milliseconds since the last notify_status_update landed, -1 before the
  // first one. Monotonic clock; written on the ws thread, read anywhere.
  // Feeds the UI's stale watchdog: an open socket whose notifies stopped
  // (wedged Moonraker) must not keep rendering readouts as live.
  int64_t ms_since_status_update() const;

 private:
  std::map<uint32_t, std::function<void(json&)>> callbacks;
  std::map<uint32_t, NotifyConsumer*> consumers;
  std::vector<NotifyConsumer*> notify_consumers;
  // std::vector<std::function<void(json&)>> gcode_resp_cbs;

  // method_name : { <unique-name-cb-handler> :handler-cb }
  std::map<std::string, std::map<std::string, std::function<void(json&)>>> method_resp_cbs;
  std::atomic_uint64_t id;
  std::atomic<int64_t> last_status_ms_{-1};  // steady-clock stamp of the last status notify
  // Guards callbacks/consumers/notify_consumers/method_resp_cbs and id
  // allocation across the ws thread (onmessage) and the UI thread (taps ->
  // gcode_script). Recursive because the cb send_jsonrpc variants call the
  // bare variant while holding it. ALWAYS released before invoking any
  // callback so consume()/cb may take lv_lock without inverting lock order
  // (the UI order is always lv_lock -> rpc_mutex_).
  std::recursive_mutex rpc_mutex_;
};

#endif //__KWEBSOCKET_CLIENT_H__
