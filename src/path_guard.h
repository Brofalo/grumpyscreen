#ifndef __PATH_GUARD_H__
#define __PATH_GUARD_H__

#include <string>
#include <vector>

// Confining an untrusted path to a root directory.
//
// Header-only and dependency-free on purpose. The logic is pure string work, so
// it can be unit tested on its own (tests/test_path_guard.cpp) without dragging
// in libhv, LVGL, Config or State, which is what utils.cpp costs to compile.

namespace KUtils {

  namespace path_guard_detail {

    // Split on both separators. The value being split is attacker-supplied gcode
    // metadata, and get_thumbnail already treats '\\' as a separator when it
    // slices the gcode directory off, so treat it as one here too rather than
    // letting it through as an ordinary filename byte.
    inline std::vector<std::string> path_segments(const std::string &p) {
      std::vector<std::string> out;
      std::string cur;
      for (char c : p) {
        if (c == '/' || c == '\\') {
          if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else {
          cur += c;
        }
      }
      if (!cur.empty()) out.push_back(cur);
      return out;
    }

    // Collapse "." and "..". Deliberately lexical, with no filesystem access:
    // this runs on the websocket callback thread once per file row, and it has
    // to give the same answer whether or not the thumbnail has been extracted to
    // disk yet. `escaped` is set if a ".." tries to pop above the first segment.
    // Symlinks are out of scope by that same no-filesystem rule; closing that
    // would cost a stat per row on the ws thread.
    inline std::vector<std::string> normalize_segments(const std::vector<std::string> &segs,
                                                       bool *escaped) {
      std::vector<std::string> out;
      for (const auto &s : segs) {
        if (s == ".") continue;
        if (s == "..") {
          if (out.empty()) { if (escaped) *escaped = true; }
          else out.pop_back();
          continue;
        }
        out.push_back(s);
      }
      return out;
    }

  }  // namespace path_guard_detail

  // Join an untrusted relative path onto a root and confine the result to it.
  // Returns "" when there is no safe answer, which every caller must treat as
  // "no path", never as "use the input".
  inline std::string safe_join_under(const std::string &root, const std::string &rel) {
    using namespace path_guard_detail;

    // An empty root is not a permissive default, it is the dangerous one.
    // get_root_path returns "" whenever the roots list carries no gcodes entry,
    // and the previous fmt::format("{}/{}", "", rel) then yielded a path rooted
    // at "/", so metadata of "etc/shadow" resolved to /etc/shadow with no ".."
    // involved at all.
    if (root.empty() || rel.empty()) return "";

    // These paths are defined as relative to the gcode file's directory, so an
    // absolute or drive-qualified one is never legitimate.
    if (rel.front() == '/' || rel.front() == '\\') return "";
    if (rel.size() >= 2 && rel[1] == ':') return "";

    const bool root_absolute = root.front() == '/';
    const std::vector<std::string> root_segs =
        normalize_segments(path_segments(root), nullptr);
    if (root_segs.empty()) return "";

    std::vector<std::string> joined = path_segments(root);
    const std::vector<std::string> rel_segs = path_segments(rel);
    joined.insert(joined.end(), rel_segs.begin(), rel_segs.end());

    bool escaped = false;
    const std::vector<std::string> full_segs = normalize_segments(joined, &escaped);
    if (escaped) return "";

    // Strictly inside: equal length would mean the metadata resolved to the root
    // directory itself, which is not a thumbnail. Comparing whole segments is
    // what stops "/gcodes-evil" passing a prefix test against root "/gcodes".
    if (full_segs.size() <= root_segs.size()) return "";
    for (size_t i = 0; i < root_segs.size(); i++) {
      if (full_segs[i] != root_segs[i]) return "";
    }

    std::string out = root_absolute ? "/" : "";
    for (size_t i = 0; i < full_segs.size(); i++) {
      out += full_segs[i];
      if (i + 1 < full_segs.size()) out += "/";
    }
    return out;
  }

}  // namespace KUtils

#endif  // __PATH_GUARD_H__
