# Pono Print desktop simulator

Phase A.5 per `docs/design/pono-print-phase-a-technical-brief.md`. Lets you iterate on Pono Print screens on your dev machine without flashing the printer every cycle.

## What it does

Opens a 480×272 SDL2 window that runs the same LVGL UI the printer's LCD runs. Mouse clicks substitute for touch. Same `pono::theme_init()` registers the same theme. Screen functions are the same C++ — copy them between simulator and binary verbatim.

## Quick start

### Linux

```sh
sudo apt install libsdl2-dev g++ make
cd grumpyscreen/sim
make -f Makefile.sim
./grumpyscreen-sim
```

### macOS

```sh
brew install sdl2
cd grumpyscreen/sim
make -f Makefile.sim
./grumpyscreen-sim
```

### Windows (MSYS2 / MinGW)

```sh
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2 make
cd grumpyscreen/sim
make -f Makefile.sim
./grumpyscreen-sim.exe
```

## Arguments

| Flag | Behavior |
|---|---|
| `--screen=<name>` | Open a specific screen on launch (e.g. `home`, `queue`, `files`). Phase A scaffold ships only the placeholder; later phases add real screen names. |
| `--capture=<path>` | Render one frame, capture the SDL framebuffer to `<path>` (PNG). For CI screenshot regression. |
| `--exit` | Exit after first render. Combined with `--capture` for headless screenshots. |

## CI screenshot regression

The Phase A.5 design uses screenshot regression as the primary automated test. CI grabs each spec'd screen via:

```sh
for screen in home files print filament move calibration settings \
              queue wizard macros logs maintenance; do
    xvfb-run -s "-screen 0 480x272x24" \
        ./grumpyscreen-sim --screen=$screen --capture screenshots/$screen.png --exit
    diff screenshots/$screen.png tests/screenshots/$screen.png \
        || (echo "screenshot regression: $screen"; exit 1)
done
```

Reference PNGs live at `tests/screenshots/<screen>.png` in the fork. Regenerate with `./tools/regen_screenshot_baselines.sh` after intentional UI changes.

## What this scaffold does NOT include

- Real screen build calls (placeholder content only). Add real `pono_home_panel_create()` etc as Phase B-G ships.
- Touch gesture simulation (LVGL SDL backend doesn't synthesize multi-touch).
- Network-dependent features (Moonraker, Foundry, Hailo). For those, stub the data with mock JSON responses.
- ARM cross-compilation (use the Yocto recipe for that; simulator is desktop-only).
