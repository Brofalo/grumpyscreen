#ifndef __K_THEME_H__
#define __K_THEME_H__

#include "hv/json.hpp"

#include <string>

using json = nlohmann::json;

class ThemeConfig {
private:
  static ThemeConfig *instance;
  std::string path;

protected:
  json data;

public:
  ThemeConfig();
  ThemeConfig(ThemeConfig &o) = delete;
  void operator=(const ThemeConfig &) = delete;
  void init(const std::string config_path);

  template<typename T> T get(const std::string &json_ptr) {
    // Never throw out of a config read and never mutate `data`: a missing key
    // or wrong-typed value yields a default-constructed T (empty string / 0)
    // and the caller guards. (Plain operator[] would insert + could throw.)
    try {
      json node = data.value(json::json_pointer(json_ptr), json());
      if (!node.is_null()) {
        return node.template get<T>();
      }
    } catch (...) {}
    return T{};
  };

  static ThemeConfig *get_instance();

};

#endif // __K_THEME_H__
