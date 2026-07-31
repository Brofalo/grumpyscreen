#ifndef __PATH_GUARD_H__
#define __PATH_GUARD_H__

#include <string>

// Thumbnail path containment, lifted out of utils.cpp by #36 follow-up so a
// committed test can reach it. The logic below is byte-identical to what #32
// shipped apart from static -> inline; utils.cpp includes this and calls it
// exactly as before. utils.h cannot serve this purpose because it pulls in
// hv/json.hpp, and libhv's headers only exist once libhv has been built, so a
// test that included it could not compile standalone.

namespace KUtils {

  // A thumbnail path arrives inside gcode metadata, which anyone who can upload
  // a file controls. It gets joined onto the gcodes root and handed to LVGL as
  // an image source, so an unchecked value opens an arbitrary file on the box: a
  // ".." component walks out of the root, a leading "/" ignores the root
  // altogether, and a blocking special file would stall the thread that opens
  // it. Require a plain relative path with no traversal.
  //
  // This is lexical containment only. A symlink planted inside the gcodes root
  // still resolves wherever it points, which would need someone who already has
  // write access to that directory, so it is not what this guard is for.
  inline bool safe_thumb_path(const std::string &p) {
    if (p.empty() || p.size() > 512) return false;
    if (p.front() == '/' || p.front() == '\\') return false;  // absolute, ignores the root
    if (p.find('\0') != std::string::npos) return false;      // would truncate at c_str()
    for (size_t start = 0; start <= p.size(); ) {
      size_t sep = p.find_first_of("/\\", start);
      size_t end = (sep == std::string::npos) ? p.size() : sep;
      if (p.compare(start, end - start, "..") == 0) return false;
      if (sep == std::string::npos) break;
      start = sep + 1;
    }
    return true;
  }

}  // namespace KUtils

#endif  // __PATH_GUARD_H__
