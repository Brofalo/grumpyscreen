// test_path_guard.cpp
//
// Regression gate for safe_thumb_path, the guard #32 added to get_thumbnail.
// #32 validated it by extracting the function at test time and compiling it
// standalone, which proved it worked but left nothing in the tree to stop it
// regressing: tests/ still held only test_config.cpp. This is that gate.
//
// Why it matters. `relative_path` comes out of a gcode file's thumbnail metadata
// via Moonraker's server.files.metadata, so anyone who can upload a file controls
// it. main_panel hands the joined result to lv_img_set_src as "A:" + path, so
// LVGL opens and decodes whatever it names, on the UI thread under lv_lock, once
// per row as the Files list populates, as root. The result is drawn on a 480x320
// panel, so the risk is not disclosure: it is aiming an image decoder at
// arbitrary bytes, or at a device node or fifo that blocks, and stalling the only
// local control surface. That surface carries the E-STOP.
//
// The guard is called on the ALREADY-JOINED value (metadata prefixed with the
// gcode file's directory), so these cases are written the way it is actually
// called.

#include <cassert>
#include <iostream>
#include <string>
#include "path_guard.h"

using KUtils::safe_thumb_path;

static int failures = 0;

static void check(bool ok, const std::string &label) {
    std::cout << (ok ? "  PASS  " : "  FAIL  ") << label << "\n";
    if (!ok) failures++;
}

static void accepts(const std::string &p, const std::string &why) {
    check(safe_thumb_path(p), "accepts " + why);
}

static void rejects(const std::string &p, const std::string &why) {
    check(!safe_thumb_path(p), "rejects " + why);
}

int main() {
    std::cout << "legitimate thumbnail paths still pass\n";
    accepts("benchy.gcode", "a bare filename");
    accepts(".thumbs/benchy-300x300.png", "the usual .thumbs layout");
    accepts("subdir/.thumbs/part.png", "a nested gcode directory");
    // A leading dot is a component name, not a traversal. Rejecting these would
    // break every real thumbnail, since Moonraker writes them under .thumbs.
    accepts(".thumbs/..hidden.png", "a filename that merely starts with dots");
    accepts("a..b/x.png", "dots inside a component name");
    accepts("...", "a three-dot component, which is a legal filename");

    std::cout << "traversal is rejected wherever it sits\n";
    rejects("../../../../etc/shadow", "traversal at the head");
    rejects("sub/../../../../etc/passwd", "traversal in the middle");
    rejects("sub/..", "traversal at the tail");
    rejects("..", "a bare parent reference");
    rejects("../evil.png", "a single level up");

    std::cout << "absolute paths are rejected under either separator\n";
    rejects("/etc/shadow", "a slash-absolute path");
    rejects("\\etc\\shadow", "a backslash-absolute path");

    std::cout << "backslash counts as a separator, not a filename byte\n";
    // get_thumbnail itself treats '\\' as a separator when it slices the gcode
    // directory off, so the guard has to agree or the two disagree about where
    // components begin.
    rejects("..\\..\\..\\..\\etc\\shadow", "backslash traversal");
    rejects("sub\\..\\..\\..\\etc\\passwd", "mixed-separator traversal");

    std::cout << "the two cases a naive guard misses\n";
    // The NUL check has to be load-bearing on its own, so this case is chosen so
    // that ONLY the NUL check can catch it. The component here is "..\0", which
    // is not equal to ".." so the component comparison passes it, while c_str()
    // truncates the whole path to exactly "..". A first draft of this test used
    // "ok.png\0/../../etc/shadow", which the component check already rejected, so
    // it passed for the wrong reason and stayed green when the NUL check was
    // deleted.
    rejects(std::string("..\0/safe.png", 12), "a NUL smuggling .. past the component check");
    rejects(std::string("sub/..\0/x.png", 13), "the same trick mid-path");
    rejects(std::string(513, 'a'), "a path past the 512 length cap");
    accepts(std::string(512, 'a'), "a path exactly at the cap");

    std::cout << "degenerate input\n";
    rejects("", "the empty string");

    std::cout << "\nRESULT: " << (failures ? std::to_string(failures) + " FAILED" : "ALL PASS")
              << "\n";
    return failures ? 1 : 0;
}
