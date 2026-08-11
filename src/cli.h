#ifndef __CLI_H__
#define __CLI_H__

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include <sys/types.h>
#include <unistd.h>

// Argument handling for the grumpyscreen binary, kept in a header so a committed
// test can reach it without linking LVGL, the way path_guard.h does.
//
// main() was `int main(void)`, so every argument reached the same main() that
// opens /dev/fb0 and /dev/input/event0 and starts the full UI. Measured on the
// bench printer (192.168.50.112, Pono Print 0.1.15) on 2026-08-11:
// `ssh root@... grumpyscreen --version` printed no version, it launched a SECOND
// grumpyscreen. Two processes then held the framebuffer and drew over each
// other, killing the stray did not heal it, the survivor stopped repainting and
// came back showing only the E-STOP button on black, and the touchscreen stayed
// unusable until `/etc/init.d/grumpyscreen restart`. That screen carries the
// E-STOP, so an unrecognised flag took out the local halt control.
//
// Both answers below therefore have to land BEFORE anything opens a device:
// what the arguments asked for, and whether a copy is already running.

namespace cli {

  // The init script (pono-print-os meta-opencentauri/recipes-apps/grumyscreen/
  // files/grumpyscreen.init) starts us with
  //   start-stop-daemon -S -b -m -p /var/run/gui.pid -x /usr/bin/grumpyscreen
  // so this file exists whenever the service owns the screen. gui-switcher.init
  // kills through the same path.
  inline constexpr const char *default_pidfile = "/var/run/gui.pid";

  inline constexpr const char *default_config = "/etc/klipper/config/grumpyscreen.cfg";

  inline constexpr const char *usage_text =
    "Usage: grumpyscreen [OPTIONS]\n"
    "\n"
    "Touch UI for Klipper on the Pono Print LCD. With no options it reads\n"
    "/etc/klipper/config/grumpyscreen.cfg, then takes over /dev/fb0 and the\n"
    "touchscreen, so it is normally started by /etc/init.d/grumpyscreen and not\n"
    "by hand. It refuses to start while another copy is running.\n"
    "\n"
    "Options:\n"
    "  -c, --config PATH   read the config from PATH instead of the default\n"
    "      --version       print the version and exit\n"
    "  -h, --help          print this help and exit\n"
    "\n"
    "Exit status: 0 ok, 1 another instance is already running, 2 bad arguments.\n";

  enum class Action {
    Run,         // start the UI
    Version,     // print the version, touch nothing
    Help,        // print the usage, touch nothing
    UsageError,  // complain and exit non-zero, touch nothing
  };

  struct Options {
    Action action = Action::Run;
    std::string config_path;  // empty means default_config
    std::string error;        // set only when action == UsageError
  };

  // Pure. Anything not named here is an error rather than something ignored,
  // because the whole failure was an unrecognised argument falling through to
  // the run path.
  inline Options parse(int argc, const char *const *argv) {
    Options o;
    bool want_help = false;
    bool want_version = false;

    for (int i = 1; i < argc; i++) {
      const std::string a = (argv[i] == nullptr) ? std::string() : std::string(argv[i]);

      if (a == "--version") {
        want_version = true;
      } else if (a == "--help" || a == "-h") {
        want_help = true;
      } else if (a == "-c" || a == "--config") {
        if (i + 1 >= argc || argv[i + 1] == nullptr || argv[i + 1][0] == '\0') {
          o.action = Action::UsageError;
          o.error = a + " needs a path";
          return o;
        }
        o.config_path = argv[++i];
      } else if (a.rfind("--config=", 0) == 0) {
        o.config_path = a.substr(9);
        if (o.config_path.empty()) {
          o.action = Action::UsageError;
          o.error = "--config= needs a path";
          return o;
        }
      } else {
        // Returns immediately, so `--version --bogus` is an error rather than a
        // version print: a typo next to a good flag is still a typo.
        o.action = Action::UsageError;
        o.error = a.empty() ? std::string("empty argument")
                            : ("unrecognised argument: " + a);
        return o;
      }
    }

    if (want_help) o.action = Action::Help;
    else if (want_version) o.action = Action::Version;
    return o;
  }

  // Pure. A pid file is written by start-stop-daemon, not by us, and survives a
  // crash, so it is a claim to check rather than a fact.
  inline bool parse_pidfile_text(const std::string &text, long &out) {
    if (text.find('\0') != std::string::npos) return false;
    const char *begin = text.c_str();
    char *end = nullptr;
    errno = 0;
    const long v = std::strtol(begin, &end, 10);
    if (end == begin || errno == ERANGE || v <= 0) return false;
    for (; *end != '\0'; ++end) {
      if (!std::isspace(static_cast<unsigned char>(*end))) return false;
    }
    out = v;
    return true;
  }

  inline std::string read_small_file(const std::string &path) {
    std::ifstream f(path);
    if (!f.is_open()) return std::string();
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
  }

  // /proc/<pid>/comm is the exec'd file's basename, and it is missing once the
  // process is reaped, so one read answers both questions a pid file raises: is
  // it still alive, and is it another copy of us rather than an unrelated
  // process holding a recycled pid. A kill(pid, 0) liveness probe was here as
  // well and came out: no case separates the two, since a reaped pid has no
  // /proc entry either, and tests/mutate_cli.py could delete the probe with
  // every test still green.
  inline std::string proc_comm(const std::string &who) {
    std::string c = read_small_file("/proc/" + who + "/comm");
    while (!c.empty() && (c.back() == '\n' || c.back() == '\r')) c.pop_back();
    return c;
  }

  // Pure, and separate from the read above so both of its answers can be
  // reached by a test. An empty `theirs` is a process that has gone since the
  // pid file was written. An empty `mine` means there is no /proc to ask at
  // all, and the two empties must not compare equal into a refusal: that would
  // be the guard keeping the UI off the glass on the strength of knowing
  // nothing.
  inline bool comm_is_twin(const std::string &mine, const std::string &theirs) {
    return !mine.empty() && mine == theirs;
  }

  // The pid of another live grumpyscreen, or 0 if we are clear to start.
  //
  // Every uncertain case answers 0, on purpose. The failure this prevents is a
  // torn framebuffer, which one restart fixes; the failure a false positive
  // would cause is a printer that boots with no UI and therefore no local
  // E-STOP, which is worse. So it refuses only on evidence.
  //
  // The getpid() case is the one that keeps the normal boot working: with -m the
  // init script's start-stop-daemon writes the pid of the process it forked,
  // which is this one, so on every clean start the pid file already names us.
  inline long running_instance_pid(const char *pidfile = default_pidfile) {
    long pid = 0;
    if (!parse_pidfile_text(read_small_file(pidfile), pid)) return 0;  // absent or junk
    if (pid == static_cast<long>(getpid())) return 0;                  // our own pid file

    if (!comm_is_twin(proc_comm("self"), proc_comm(std::to_string(pid)))) return 0;
    return pid;
  }

}  // namespace cli

#endif  // __CLI_H__
