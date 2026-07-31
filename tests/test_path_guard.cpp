// test_path_guard.cpp
//
// safe_join_under confines an untrusted path to a root. The untrusted value is
// `relative_path` out of a gcode file's thumbnail metadata, which reaches
// get_thumbnail via Moonraker's server.files.metadata and ends up at
// lv_img_set_src as "A:" + path. LVGL then opens and decodes that file on the UI
// thread, so an unconfined path lets a crafted gcode aim the image decoder at any
// file the process can open, and the process is root on the appliance. The
// realistic damage is hanging or crashing the only local control surface, which
// carries the E-STOP, on a machine with a 320C hotend.
//
// Every ESCAPE case below returns the traversed path under the old
// fmt::format("{}/{}", gcode_root, relative_path).

#include <cassert>
#include <iostream>
#include <string>
#include "path_guard.h"

using KUtils::safe_join_under;

static int failures = 0;

static void check(bool ok, const std::string &label) {
    std::cout << (ok ? "  PASS  " : "  FAIL  ") << label << "\n";
    if (!ok) failures++;
}

static void allows(const std::string &root, const std::string &rel,
                   const std::string &expect) {
    const std::string got = safe_join_under(root, rel);
    check(got == expect, "allows " + rel + " -> " + (got.empty() ? "(refused)" : got));
}

static void refuses(const std::string &root, const std::string &rel,
                    const std::string &why) {
    const std::string got = safe_join_under(root, rel);
    check(got.empty(), "refuses " + why + (got.empty() ? "" : " but returned " + got));
}

int main() {
    const std::string ROOT = "/home/pono/printer_data/gcodes";

    std::cout << "legitimate thumbnails still resolve\n";
    allows(ROOT, "benchy.gcode", ROOT + "/benchy.gcode");
    allows(ROOT, ".thumbs/benchy-300x300.png", ROOT + "/.thumbs/benchy-300x300.png");
    allows(ROOT, "subdir/.thumbs/part.png", ROOT + "/subdir/.thumbs/part.png");
    // A "." component is noise, not an escape.
    allows(ROOT, "./.thumbs/a.png", ROOT + "/.thumbs/a.png");
    // Descending and coming back stays inside.
    allows(ROOT, "sub/../.thumbs/a.png", ROOT + "/.thumbs/a.png");
    // A trailing separator on the root must not double up.
    allows(ROOT + "/", ".thumbs/a.png", ROOT + "/.thumbs/a.png");

    std::cout << "traversal is refused\n";
    refuses(ROOT, "../../../../etc/shadow", "parent traversal to /etc/shadow");
    refuses(ROOT, "../evil.png", "a single level up");
    refuses(ROOT, "a/../../../../../../etc/passwd", "traversal buried mid-path");
    refuses(ROOT, "..", "a bare parent reference");
    // Blocking devices matters as much as blocking secrets: LVGL opening a
    // character device or a fifo is what hangs the UI thread.
    refuses(ROOT, "../../../../dev/urandom", "a character device");

    std::cout << "absolute and drive-qualified paths are refused\n";
    refuses(ROOT, "/etc/shadow", "an absolute path");
    refuses(ROOT, "\\etc\\shadow", "a backslash-absolute path");
    refuses(ROOT, "C:/Windows/win.ini", "a drive-qualified path");

    std::cout << "backslash is treated as a separator, not a filename byte\n";
    refuses(ROOT, "..\\..\\..\\..\\etc\\shadow", "backslash traversal");
    refuses(ROOT, "sub\\..\\..\\..\\..\\..\\etc\\passwd", "mixed backslash traversal");

    std::cout << "the empty root is the dangerous case, not the permissive one\n";
    // get_root_path returns "" when the roots list carries no gcodes entry. The
    // old code then built "/" + rel, so this needed no traversal at all.
    refuses("", "etc/shadow", "an empty root");
    refuses("", "../../etc/shadow", "an empty root with traversal");

    std::cout << "degenerate inputs\n";
    refuses(ROOT, "", "an empty relative path");
    refuses(ROOT, ".", "a path resolving to the root itself");
    refuses(ROOT, "sub/..", "a path resolving back to the root itself");

    std::cout << "a sibling directory sharing a name prefix is not inside\n";
    // Whole-segment comparison is what stops this; a plain string prefix test
    // would accept it.
    refuses(ROOT, "../gcodes-evil/x.png", "a prefix-sharing sibling");

    std::cout << "\nRESULT: " << (failures ? std::to_string(failures) + " FAILED" : "ALL PASS")
              << "\n";
    return failures ? 1 : 0;
}
