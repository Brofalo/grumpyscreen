#!/usr/bin/env python3
"""Delete one clause of src/cli.h at a time and confirm test_cli.cpp goes red.

A test that stays green when the thing it guards is removed is guarding
nothing. Every mutation below is a plausible tidy-up someone could make to
src/cli.h while believing the suite still covers them.

It found two real holes on the first run, and both were fixed rather than
argued with:

  - the kill(pid, 0) liveness probe could be deleted with every test green,
    because a reaped pid has no /proc entry either, so the comm read already
    answered it. The probe came out; the clause a test cannot bind is
    decoration.
  - "a second field after the pid" was going in through running_instance_pid,
    where it landed on a pid no process holds, so it passed whether the parse
    was strict or not. It now tests parse_pidfile_text directly.

Run it from anywhere:

    python3 tests/mutate_cli.py

Needs a host g++ and a Linux /proc, the same two things `make test` needs.
Not wired into `make test`: it is a check on the tests rather than on the
binary, and it belongs in a hand-run pass when this guard is edited.
"""
import os
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(REPO, "src")
HEADER = os.path.join(SRC, "cli.h")
TEST = os.path.join(REPO, "tests", "test_cli.cpp")

# (what a maintainer might think they are simplifying, the exact text, its replacement)
MUTATIONS = [
    (
        "drop the unrecognised-argument branch (restores the old fall-through)",
        "        o.action = Action::UsageError;\n"
        "        o.error = a.empty() ? std::string(\"empty argument\")\n"
        "                            : (\"unrecognised argument: \" + a);\n"
        "        return o;",
        "        return o;",
    ),
    (
        "drop the empty-value check on -c / --config",
        "        if (i + 1 >= argc || argv[i + 1] == nullptr || argv[i + 1][0] == '\\0') {",
        "        if (i + 1 >= argc) {",
    ),
    (
        "drop the NUL check on the pid file",
        "    if (text.find('\\0') != std::string::npos) return false;",
        "",
    ),
    (
        "drop the range check on the pid",
        "    if (end == begin || errno == ERANGE || v <= 0) return false;",
        "    if (end == begin || v <= 0) return false;",
    ),
    (
        "drop the positive check on the pid",
        "    if (end == begin || errno == ERANGE || v <= 0) return false;",
        "    if (end == begin || errno == ERANGE) return false;",
    ),
    (
        "drop the trailing-junk check on the pid file",
        "    for (; *end != '\\0'; ++end) {\n"
        "      if (!std::isspace(static_cast<unsigned char>(*end))) return false;\n"
        "    }",
        "",
    ),
    (
        "drop the getpid() self check (would refuse every clean boot)",
        "    if (pid == static_cast<long>(getpid())) return 0;                  // our own pid file",
        "",
    ),
    (
        "drop the no-evidence half of the twin test (would refuse without /proc)",
        "    return !mine.empty() && mine == theirs;",
        "    return mine == theirs;",
    ),
    (
        "drop the comm match (a recycled pid would keep the UI off)",
        "    return !mine.empty() && mine == theirs;",
        "    return !mine.empty();",
    ),
    (
        "stop consulting the twin test at all",
        "    if (!comm_is_twin(proc_comm(\"self\"), proc_comm(std::to_string(pid)))) return 0;",
        "",
    ),
]


def main():
    src = open(HEADER).read()
    rc_all = 0
    with tempfile.TemporaryDirectory() as mut:
        for name, needle, repl in MUTATIONS:
            if needle not in src:
                print("MISSING ANCHOR  %s" % name)
                print("                cli.h no longer contains the text this mutation edits.")
                rc_all = 1
                continue
            with open(os.path.join(mut, "cli.h"), "w") as f:
                f.write(src.replace(needle, repl, 1))
            binary = os.path.join(mut, "test_cli_mut")
            build = subprocess.run(
                ["g++", "-std=gnu++17", "-O2", "-Wall", "-Wextra",
                 "-I" + mut, "-I" + SRC, TEST, "-o", binary],
                capture_output=True, text=True)
            if build.returncode != 0:
                print("BUILD FAIL      %s" % name)
                print(build.stderr[:800])
                rc_all = 1
                continue
            run = subprocess.run([binary], capture_output=True, text=True)
            caught = [ln.strip()[6:].strip()
                      for ln in run.stdout.splitlines() if ln.startswith("  FAIL")]
            if run.returncode == 0:
                print("SURVIVED        %s" % name)
                print("                nothing in test_cli.cpp binds this clause.")
                rc_all = 1
            else:
                print("caught by %-2d    %s" % (len(caught), name))
                for c in caught[:3]:
                    print("                  %s" % c)

    print()
    print("RESULT:", "every clause is bound by a test" if rc_all == 0
          else "SOME CLAUSES ARE NOT BOUND BY ANY TEST")
    return rc_all


if __name__ == "__main__":
    sys.exit(main())
