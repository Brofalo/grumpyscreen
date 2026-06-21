/*
 * websocket client
 *
 * @build   make examples
 * @server  bin/websocket_server_test 8888
 * @client  bin/websocket_client_test ws://127.0.0.1:8888/test
 * @clients bin/websocket_client_test ws://127.0.0.1:8888/test 100
 * @python  scripts/websocket_server.py
 * @js      html/websocket_client.html
 *
 */

#include "websocket_client.h"
#include "logger.h"

#include <algorithm>
#include <chrono>

// Monotonic now in ms for the status-staleness stamp (wall clock would
// lie across an NTP step).
static int64_t mono_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();
}

using namespace hv;
using json = nlohmann::json;

KWebSocketClient::KWebSocketClient(EventLoopPtr loop)
  : WebSocketClient(loop)
  , id(0)
{
}

KWebSocketClient::~KWebSocketClient() {
}

int KWebSocketClient::connect(const char* url,
			      std::function<void()> connected,
			      std::function<void()> disconnected) {
  LOG_DEBUG("websocket connecting");
  // set callbacks
  onopen = [this, connected]() {
    const HttpResponsePtr& resp = getHttpResponse();
    LOG_DEBUG("onopen {}", resp->body.c_str());
    connected();
  };
  onmessage = [this, connected, disconnected](const std::string &msg) {
    json j;
    try {
      j = json::parse(msg);
    } catch (const std::exception &e) {
      // A malformed/truncated frame (e.g. a still-starting Moonraker) must
      // never abort the ws thread.
      LOG_ERROR("ws: dropping unparseable message: {}", e.what());
      return;
    }

    try {
      if (j.contains("id")) {
        // Copy the matching consumer/callback OUT under the lock and erase it
        // BEFORE invoking: the callback can re-enter send_jsonrpc and mutate
        // these maps, which would invalidate a held iterator. Invoke unlocked
        // so consume()/cb can take lv_lock without inverting lock order.
        NotifyConsumer *consumer = nullptr;
        std::function<void(json&)> cb;
        uint32_t rid = j["id"].template get<uint32_t>();
        {
          std::lock_guard<std::recursive_mutex> lk(rpc_mutex_);
          auto entry = consumers.find(rid);
          if (entry != consumers.end()) { consumer = entry->second; consumers.erase(entry); }
          auto cb_entry = callbacks.find(rid);
          if (cb_entry != callbacks.end()) { cb = cb_entry->second; callbacks.erase(cb_entry); }
        }
        if (consumer) consumer->consume(j);
        if (cb) cb(j);
      }

      if (j.contains("method")) {
        std::string method = j["method"].template get<std::string>();
        if ("notify_status_update" == method) {
          last_status_ms_.store(mono_ms(), std::memory_order_relaxed);
          std::vector<NotifyConsumer*> snapshot;
          {
            std::lock_guard<std::recursive_mutex> lk(rpc_mutex_);
            snapshot = notify_consumers;
          }
          for (const auto &entry : snapshot) {
            entry->consume(j);
          }
        } else if ("notify_klippy_disconnected" == method) {
          LOG_DEBUG("klippy disconnected");
          disconnected();
        } else if ("notify_klippy_shutdown" == method) {
          LOG_DEBUG("klippy shutdown");
          disconnected();
        } else if ("notify_klippy_ready" == method) {
          LOG_DEBUG("klippy connected");
          connected();
        }

        std::vector<std::function<void(json&)>> handlers;
        {
          std::lock_guard<std::recursive_mutex> lk(rpc_mutex_);
          auto entry = method_resp_cbs.find(method);
          if (entry != method_resp_cbs.end()) {
            for (const auto &handler_entry : entry->second) {
              handlers.push_back(handler_entry.second);
            }
          }
        }
        for (auto &handler : handlers) {
          handler(j);
        }
      }
    } catch (const std::exception &e) {
      // Any get<>()/handler throw on the ws thread is contained here instead
      // of terminating the process.
      LOG_ERROR("ws: error handling message: {}", e.what());
    }
  };

  onclose = [disconnected]() {
    LOG_DEBUG("onclose");
    disconnected();
  };

  // ping
  setPingInterval(10000);

  reconn_setting_t reconn;
  reconn_setting_init(&reconn);
  reconn.min_delay = 200;
  reconn.max_delay = 2000;
  reconn.delay_policy = 2;
  setReconnect(&reconn);

  http_headers headers;
  return open(url, headers);
};

int64_t KWebSocketClient::ms_since_status_update() const {
  int64_t t = last_status_ms_.load(std::memory_order_relaxed);
  return t < 0 ? -1 : mono_ms() - t;
}

int KWebSocketClient::send_jsonrpc(const std::string &method,
				   const json &params,
				   std::function<void(json&)> cb) {
  // Hold the lock across register + send so the callback is keyed on exactly
  // the id this rpc carries, with no interleaving id++ from the ws thread.
  std::lock_guard<std::recursive_mutex> lk(rpc_mutex_);
  callbacks[id] = cb;
  return send_jsonrpc(method, params);
}

int KWebSocketClient::send_jsonrpc(const std::string &method, std::function<void(json&)> cb) {
  std::lock_guard<std::recursive_mutex> lk(rpc_mutex_);
  callbacks[id] = cb;
  return send_jsonrpc(method);
}

int KWebSocketClient::send_jsonrpc(const std::string &method, const json &params, NotifyConsumer *consumer) {
  std::lock_guard<std::recursive_mutex> lk(rpc_mutex_);
  consumers[id] = consumer;
  return send_jsonrpc(method, params);
}

void KWebSocketClient::register_notify_update(NotifyConsumer *consumer) {
  std::lock_guard<std::recursive_mutex> lk(rpc_mutex_);
  if (std::find(notify_consumers.begin(), notify_consumers.end(), consumer) == std::end(notify_consumers)) {
    notify_consumers.push_back(consumer);
  }
}

void KWebSocketClient::unregister_notify_update(NotifyConsumer *consumer) {
  std::lock_guard<std::recursive_mutex> lk(rpc_mutex_);
  // Two-arg erase: remove_if returns end() when nothing matches, and
  // erase(end()) alone is undefined behavior. The range form is the fix.
  notify_consumers.erase(std::remove_if(
    notify_consumers.begin(), notify_consumers.end(),
    [consumer](NotifyConsumer *c) {
      return c == consumer;
    }), notify_consumers.end());
}

int KWebSocketClient::send_jsonrpc(const std::string &method, const json &params) {
  std::lock_guard<std::recursive_mutex> lk(rpc_mutex_);
  json rpc;
  rpc["jsonrpc"] = "2.0";
  rpc["method"] = method;
  rpc["params"] = params;
  rpc["id"] = id++;

  LOG_DEBUG("send_jsonrpc: {}", rpc.dump());
  return send(rpc.dump());
}

int KWebSocketClient::send_jsonrpc(const std::string &method) {
  std::lock_guard<std::recursive_mutex> lk(rpc_mutex_);
  json rpc;
  rpc["jsonrpc"] = "2.0";
  rpc["method"] = method;
  rpc["id"] = id++;

  LOG_DEBUG("send_jsonrpc: {}", rpc.dump());
  return send(rpc.dump());
}

int KWebSocketClient::gcode_script(const std::string &gcode) {
  json cmd = {{ "script", gcode }};
  LOG_TRACE("{}", gcode);
  return send_jsonrpc("printer.gcode.script", cmd);
}

// Variant that routes the gcode.script RPC response to cb. Klipper replies when
// the gcode finishes executing, so cb fires on completion (e.g. homing done).
// cb runs on the websocket thread; UI work inside it must hold the lv lock.
int KWebSocketClient::gcode_script(const std::string &gcode, std::function<void(json&)> cb) {
  json cmd = {{ "script", gcode }};
  LOG_TRACE("{}", gcode);
  return send_jsonrpc("printer.gcode.script", cmd, cb);
}

void KWebSocketClient::register_method_callback(std::string resp_method,
						std::string handler_name,
						std::function<void(json&)> cb) {
  std::lock_guard<std::recursive_mutex> lk(rpc_mutex_);
  const auto &entry = method_resp_cbs.find(resp_method);
  if (entry == method_resp_cbs.end()) {
    LOG_DEBUG("registering new method {}, handler {}", resp_method, handler_name);
    std::map<std::string, std::function<void(json&)>> handler_map;
    handler_map.insert({handler_name, cb});
    method_resp_cbs.insert({resp_method, handler_map});
  } else {
    LOG_DEBUG("found existing resp_method {} with handlers, updating handler callback {}",
		  resp_method, handler_name);
    entry->second.insert({handler_name, cb});
  }
}
