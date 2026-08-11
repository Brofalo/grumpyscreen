// test_cli.cpp
//
// The witness for #47: a bad flag must exit non-zero without touching the
// framebuffer, and a second instance must be refused.
//
// What it is guarding. main() was `int main(void)`, so argv was never read and
// every argument reached the code that opens /dev/fb0 and /dev/input/event0.
// Measured on the bench printer (192.168.50.112, Pono Print 0.1.15) on
// 2026-08-11: `ssh root@... grumpyscreen --version` printed no version, it
// started a SECOND grumpyscreen. The two fought over the framebuffer and left a
// torn composite; killing the stray did not heal it, and the survivor came back
// rendering only the E-STOP button on black. The touchscreen was dead until
// `/etc/init.d/grumpyscreen restart`. So this is not a cosmetic argument
// nicety: an unrecognised flag took out the screen that carries the E-STOP.
//
// The two halves are tested the way they actually fail. cli::parse is pure, so
// the assertion that matters is the sweep at the end: for a list of things an
// operator might really type, NOTHING but a config path is allowed to come back
// as Action::Run. cli::running_instance_pid does real syscalls, so it is tested
// against a real forked process and a real pid file rather than a mock, because
// the case that decides whether the printer boots with a UI at all is whether
// it can tell a live twin from a stale file.

#include <csignal>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

#include "cli.h"

static int failures = 0;

static void check(bool ok, const std::string &label) {
    std::cout << (ok ? "  PASS  " : "  FAIL  ") << label << "\n";
    if (!ok) failures++;
}

// argv[0] is supplied here, so the cases below read as what an operator types.
static cli::Options parse_args(const std::vector<const char *> &args) {
    std::vector<const char *> v;
    v.push_back("grumpyscreen");
    for (const char *a : args) v.push_back(a);
    return cli::parse(static_cast<int>(v.size()), v.data());
}

static void write_file(const std::string &path, const std::string &body) {
    std::ofstream f(path, std::ios::trunc);
    f << body;
}

int main() {
    std::cout << "the three actions that do not open a device\n";
    check(parse_args({"--version"}).action == cli::Action::Version, "--version asks for a version");
    check(parse_args({"--help"}).action == cli::Action::Help, "--help asks for help");
    check(parse_args({"-h"}).action == cli::Action::Help, "-h asks for help");

    std::cout << "the config path the binary already supports\n";
    check(parse_args({}).action == cli::Action::Run, "no arguments still runs");
    check(parse_args({}).config_path.empty(), "no arguments leaves the default config in force");
    check(parse_args({"-c", "/tmp/a.cfg"}).config_path == "/tmp/a.cfg", "-c takes a path");
    check(parse_args({"--config", "/tmp/a.cfg"}).config_path == "/tmp/a.cfg", "--config takes a path");
    check(parse_args({"--config=/tmp/a.cfg"}).config_path == "/tmp/a.cfg", "--config= takes a path");
    check(parse_args({"-c", "/tmp/a.cfg"}).action == cli::Action::Run, "a config path still runs");

    std::cout << "a config option with nothing to read is an error, not a default\n";
    check(parse_args({"-c"}).action == cli::Action::UsageError, "-c with no path");
    check(parse_args({"--config"}).action == cli::Action::UsageError, "--config with no path");
    check(parse_args({"--config="}).action == cli::Action::UsageError, "--config= with no path");
    check(parse_args({"-c", ""}).action == cli::Action::UsageError, "-c with an empty path");
    check(!parse_args({"-c"}).error.empty(), "the error carries a message to print");

    std::cout << "an unrecognised argument beats a good one on the same line\n";
    // A typo next to a real flag is still a typo. If this returned Version the
    // binary would exit 0 and the caller would never learn the flag was wrong.
    check(parse_args({"--version", "--bogus"}).action == cli::Action::UsageError,
          "--version --bogus is an error");
    check(parse_args({"--bogus", "--version"}).action == cli::Action::UsageError,
          "--bogus --version is an error");
    check(parse_args({"--help", "--version"}).action == cli::Action::Help,
          "--help wins over --version when both are valid");

    std::cout << "nothing an operator can typo reaches the run path\n";
    // The regression this file exists for. Every one of these used to reach
    // main() unread and start the UI on /dev/fb0.
    const std::vector<const char *> strays = {
        "--version=1",   // the flag with a value it does not take
        "-v",            // the short form we deliberately do not define
        "-V",
        "version",       // the flag without its dashes
        "--versoin",     // a transposition
        "--Version",     // wrong case
        "--verison",
        "--vers",        // a prefix, which getopt would have accepted
        "-help",         // one dash too few
        "--h",
        "--",            // the end-of-options marker, which we do not implement
        "-",
        "start",         // an init-script verb aimed at the binary
        "status",
        "restart",
        "--config-path", // a near miss on the option we do define
        "/etc/klipper/config/grumpyscreen.cfg",  // a bare path is not an option
        "",              // an empty argument
    };
    for (const char *s : strays) {
        const cli::Options o = parse_args({s});
        check(o.action == cli::Action::UsageError && !o.error.empty(),
              std::string("rejects ") + (s[0] == '\0' ? "an empty argument" : s));
    }

    std::cout << "what a pid file has to say before it is believed\n";
    // Tested on the parser directly, not through running_instance_pid. Going in
    // by the front door, every one of these lands on a pid no live process
    // holds, so the /proc lookup downstream returns 0 and the case passes
    // whether the parse is strict or not. That is the trap test_path_guard.cpp
    // records: a case that passes for a reason other than the one it names
    // stays green when the check it is supposed to hold is deleted. The
    // mutation sweep caught exactly that here.
    auto rejects_text = [](const std::string &body, const std::string &why) {
        long pid = -1;
        check(!cli::parse_pidfile_text(body, pid), "rejects " + why);
    };
    long parsed = 0;
    check(cli::parse_pidfile_text("1234\n", parsed) && parsed == 1234, "accepts what busybox writes");
    parsed = 0;
    check(cli::parse_pidfile_text("1234", parsed) && parsed == 1234, "accepts it without the newline");
    rejects_text("", "an empty file");
    rejects_text("\n", "a lone newline");
    rejects_text("not-a-pid\n", "a file full of junk");
    rejects_text("0\n", "a zero pid");
    rejects_text("-1\n", "a negative pid");
    rejects_text("99999999999999999999999999\n", "a number past the range of the type");
    rejects_text("1 2\n", "a second field after the pid");
    rejects_text("12x\n", "junk stuck to the end of the pid");
    rejects_text(std::string("12\0" "34", 5), "a NUL splitting the number");

    std::cout << "who counts as a twin, given what /proc said about both\n";
    // The last case is the one that cannot be reached through the file, because
    // producing it means taking /proc away from the running test. It is the
    // fail-open rule in one line: with nothing known about either process, the
    // guard must let the UI start. A printer that boots with no UI has no local
    // E-STOP, which is worse than a torn framebuffer that one restart clears.
    check(cli::comm_is_twin("grumpyscreen", "grumpyscreen"), "two grumpyscreens");
    check(!cli::comm_is_twin("grumpyscreen", "sh"), "an unrelated process on a recycled pid");
    check(!cli::comm_is_twin("grumpyscreen", ""), "a pid that has gone since the file was written");
    check(!cli::comm_is_twin("", ""), "no /proc to ask, so no evidence to refuse on");
    check(!cli::comm_is_twin("", "grumpyscreen"), "no comm of our own to compare");

    std::cout << "and the ways a well-formed pid file still does not mean stop\n";
    const std::string pidfile = "/tmp/grumpyscreen_test_cli_" + std::to_string(getpid()) + ".pid";
    std::remove(pidfile.c_str());
    check(cli::running_instance_pid(pidfile.c_str()) == 0, "no pid file at all");
    write_file(pidfile, "not-a-pid\n");
    check(cli::running_instance_pid(pidfile.c_str()) == 0, "a pid file the parser rejects");

    // The case that decides whether the printer boots with a UI. The init
    // script starts us with `start-stop-daemon -S -b -m`, and -m writes the pid
    // of the process it forked, which is this one. If self did not read as
    // clear, every clean start would refuse itself and the LCD would stay dark.
    write_file(pidfile, std::to_string(getpid()) + "\n");
    check(cli::running_instance_pid(pidfile.c_str()) == 0, "our own pid, written by start-stop-daemon");

    // A pid can be recycled onto something unrelated. pid 1 is alive and is not
    // us, so it must not be read as a twin.
    if (getpid() != 1) {
        write_file(pidfile, "1\n");
        check(cli::running_instance_pid(pidfile.c_str()) == 0, "a live pid that is not grumpyscreen");
    }

    std::cout << "a live twin is refused, and only while it is alive\n";
    // A real process, not a mock: fork keeps the parent's comm, which is what
    // makes the child indistinguishable from a second copy of this binary.
    const pid_t child = fork();
    if (child == 0) {
        for (;;) pause();
        _exit(0);
    }
    check(child > 0, "the test could fork a stand-in twin");
    if (child > 0) {
        write_file(pidfile, std::to_string(child) + "\n");
        check(cli::running_instance_pid(pidfile.c_str()) == child,
              "a running twin is reported by pid");

        kill(child, SIGKILL);
        waitpid(child, nullptr, 0);
        // Same file, same bytes. Only the process is gone, which is exactly the
        // state a crashed instance leaves behind, and it must not keep the UI off.
        check(cli::running_instance_pid(pidfile.c_str()) == 0,
              "the same pid file once the twin is gone");
    }
    std::remove(pidfile.c_str());

    std::cout << "\nRESULT: " << (failures ? std::to_string(failures) + " FAILED" : "ALL PASS")
              << "\n";
    return failures ? 1 : 0;
}
