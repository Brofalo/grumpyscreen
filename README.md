# Pono Print on-device touch UI

Native LVGL touch UI for the Pono Print Centauri Carbon firmware. Runs on the 480x272 LCD on the printer mainboard. No X, no Wayland, no Node, no Electron. Just LVGL on /dev/fb0.

This is the UI binary that ships in the Pono Print SWU build.

## What's here

| Path | What |
|---|---|
| `src/pono_theme.h` | Kukui token registry (11 colors + 9 fonts + spacing + radii + frame + motion durations) |
| `src/pono_theme.cpp` | Token definitions + LVGL theme provider with per-widget-class `apply_cb` |
| `src/main.cpp` | Entry. Parses argv, refuses to be a second instance, then calls `pono::theme_init(disp)` after `lv_init()` |
| `src/cli.h` | Argument parsing + the single-instance check, both of which answer before any device is opened |
| `src/pono_home.cpp` | The cockpit + every native sub-screen (one grid, one token set) |
| `assets/pono/fonts/` | Compiled LVGL .c font files for IBM Plex Mono + Instrument Serif |
| `sim/` | SDL2 desktop simulator harness for iteration without flashing |
| `tools/regen_pono_fonts.sh` | Font regen via `lv_font_conv` (one-shot, results committed) |
| `themes/` | Legacy 2-color JSON themes (kept for compat; superseded by `pono_theme.cpp`) |

## Quick build (printer target via Yocto)

The Yocto recipe lives in the pono-print-os repo at `meta-opencentauri/recipes-apps/grumyscreen/`. It pins this fork's `pono` branch via bbappend override.

```sh
# In the pono-print build env
bitbake -c clean grumpyscreen && bitbake grumpyscreen
```

## Quick build (desktop simulator)

```sh
# Linux
sudo apt install libsdl2-dev g++ make
cd sim
make -f Makefile.sim
./grumpyscreen-sim
```

Full simulator + CI screenshot regression instructions in [sim/README.md](sim/README.md).

## Design spec

The complete UI/UX spec lives in the pono-print-os repo at `docs/design/pono-print-ui-design.md` (Phase A-G full design) and `docs/design/pono-print-phase-a-technical-brief.md` (Phase A bring-up plan). All implementation in this fork tracks those documents.

## Running it on the printer

The service owns the screen. Start and stop it through the init script, not by
running the binary:

```sh
/etc/init.d/grumpyscreen restart
```

The binary takes these and nothing else. Anything unrecognised exits 2 with a
message, and starting a second copy while one is running exits 1, both before
`/dev/fb0` or the touchscreen is opened:

```
  -c, --config PATH   read the config from PATH instead of
                      /etc/klipper/config/grumpyscreen.cfg
      --version       print the version and exit
  -h, --help          print this help and exit
```

This is worth stating because it did not used to be true. `main()` took no
arguments at all, so `grumpyscreen --version` over ssh did not print a version,
it launched a second UI: the two copies fought over the framebuffer, and the
touchscreen stayed unusable until the service was restarted. Measured on the
bench printer on 2026-08-11, Pono Print 0.1.15.

`tests/test_cli.cpp` is the gate on all of that and runs in CI via `make test`.
`tests/mutate_cli.py` is the check on the gate: it deletes one clause of
`src/cli.h` at a time and fails if the tests stay green. Run it by hand after
editing either file.

## Phase status

- **Phase A** (current): theme bring-up. Token registry + theme provider + font pipeline shipped in the initial commit. Panel refactor (A.4) is the bulk of the remaining work.
- **Phase B-F**: per-screen rewrites against the spec.
- **Phase G**: depth pass (Foundry "Ask Pono" + Hailo first-layer detection + MAUI inbox + cross-printer corpus advisor).

## License

GPL-3.0-only.

Source-tree provenance (the legal credit chain required by GPL-3 §5):

- This work is a derivative of [`jamesturton/grumpyscreen`](https://github.com/jamesturton/grumpyscreen) branch `opencentauri`, SRCREV `b085d2e00bc3dcb068b7fe98ec9dff59489acc77`.
- `jamesturton/grumpyscreen` is itself a derivative of [`ballaswag/guppyscreen`](https://github.com/ballaswag/guppyscreen).
- The LVGL framework is licensed MIT and embedded as a submodule.
- Material Design Icons by [pictogrammers.com](https://pictogrammers.com/library/mdi/), Apache 2.0.
- IBM Plex Mono font by [IBM](https://github.com/IBM/plex), OFL.
- Instrument Serif font by [Instrument](https://github.com/Instrument/instrument-serif), OFL.

Source distribution: this repository at https://github.com/Brofalo/grumpyscreen is the canonical source for the binary that ships in Pono Print SWU images. Submodule pinning preserves upstream LVGL and lv_drivers SRCREVs at the commits used for builds.
