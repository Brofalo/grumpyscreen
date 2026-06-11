# Pono Print on-device touch UI

Native LVGL touch UI for the Pono Print Centauri Carbon firmware. Runs on the 480x272 LCD on the printer mainboard. No X, no Wayland, no Node, no Electron. Just LVGL on /dev/fb0.

This is the UI binary that ships in the Pono Print SWU build.

## What's here

| Path | What |
|---|---|
| `src/pono_theme.h` | Kukui token registry (11 colors + 9 fonts + spacing + radii + frame + motion durations) |
| `src/pono_theme.cpp` | Token definitions + LVGL theme provider with per-widget-class `apply_cb` |
| `src/main.cpp` | Entry. Calls `pono::theme_init(disp)` after `lv_init()` |
| `src/pono_home.cpp` | The cockpit + every native sub-screen (one grid, one token set) |
| `assets/pono/fonts/` | Compiled LVGL .c font files for IBM Plex Mono + Instrument Serif |
| `sim/` | SDL2 desktop simulator harness for iteration without flashing |
| `tools/regen_pono_fonts.sh` | Font regen via `lv_font_conv` (one-shot, results committed) |
| `themes/` | Legacy 2-color JSON themes (kept for compat; superseded by `pono_theme.cpp`) |

## Quick build (printer target via Yocto)

The Yocto recipe lives in the cosmos-staging repo at `meta-opencentauri/recipes-apps/grumyscreen/`. It pins this fork's `pono` branch via bbappend override.

```sh
# In the cosmos build env
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

The complete UI/UX spec lives in the cosmos-staging repo at `docs/design/pono-print-ui-design.md` (Phase A-G full design) and `docs/design/pono-print-phase-a-technical-brief.md` (Phase A bring-up plan). All implementation in this fork tracks those documents.

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
