# The calibration screen

Jack, 2026-08-02, watching a Full Calibration run: "the UI/UX for the tune is
messy, when in doubt animate it out."

He is right and the diagnosis is specific. Today a calibration puts up
`pono::busy_show(cal_msg_)`: a full-screen scrim, the comet spinner, and one line
of text from `SET_DISPLAY_TEXT`. That is the correct component for homing or a
filament load. **A Full Calibration takes about an hour, and a busy spinner is a
promise that something is nearly done.** Holding that promise for an hour is what
reads as messy. The spinner is not wrong motion, it is motion carrying no
information.

Register is NIGHT: this is an instrument you monitor, not a bench tool you sit
with. pono-design's motion budget explicitly allows instruments to run
continuous, so the fix is not less animation, it is animation that MEANS
something.

## What is actually available, measured live 2026-08-02 mid-probe

Do not design against assumptions here. These were read off the running machine
while a mesh calibration was in progress.

| Source | Value during calibration | Use |
|---|---|---|
| `display_status.progress` | **0, the whole time** | USELESS. Do not bind a bar to it. |
| `display_status.message` | `Calibrating 2/3: Bed mesh` | phase, 3 states in an hour |
| `// probe at X,Y is z=...` responses | one per probed point, real coords and Z | **the real progress signal** |
| `toolhead.position` | live X, Y, Z | the head's position on the bed map |
| `bed_mesh.mesh_min` / `mesh_max` | `0,0` until the mesh COMPLETES | not usable mid-run; take the grid from configfile |
| `configfile.settings.bed_mesh.probe_count` | `11x11` = **121 points** | the denominator |
| `configfile.settings.bed_mesh.mesh_min/max` | `20,20` to `246,246` | the grid's real extent |

**The trap worth stating loudly: `display_status.progress` never leaves 0 during
a calibration.** Anyone who reaches for the obvious field gets a bar that sits at
zero for an hour and looks broken. Progress must be counted from probe events.

The phase string is the only phase signal, and it is prefixed `Calibrating N/3`,
which `main_panel.cpp` already keys on to decide `cal_active`.

## The design

Three phases, and they are not equal, so they should not look equal.

    PID          no measurable progress    spinner is honest here
    BED MESH     121 discrete points       draw it
    SHAPER       no measurable progress    spinner is honest here

### 1. A phase rail, always visible

Three pips across the top, machined, hairline-ruled, IBM Plex Mono caps at
.14em:

    ── PID ──────── MESH ──────── SHAPER ──

- completed: phosphor `#8fd6ad`
- running: amber lamp `#e2a13c`, and it is the only lit thing
- pending: faint `#6a6457`

That alone answers "how much is left" better than an hour of spinner, and it
costs three labels and a rule.

### 2. The mesh phase draws the bed

This is the "animate it out" moment and it is the whole point of the screen.

An 11 by 11 grid of cells laid out to the real `mesh_min`/`mesh_max` extent.
Each `// probe at X,Y is z=` event fills the cell nearest that coordinate:

- the cell just probed flashes amber for ~200 ms, then settles
- settled cells render phosphor, with opacity carrying the Z deviation, so the
  tilt becomes visible as the map builds
- unprobed cells stay at hairline `rgba(232,226,214,.09)`

The user watches the bed map assemble itself, in the serpentine order the probe
actually walks. It is progress, it is diagnostic, and it needs no invented
choreography because the machine is already doing the motion. Under it, one
instrument line:

    POINT 47 / 121      BED 45.1 C      Z -0.89

`toolhead.position` can drive a small amber crosshair over the grid, which is
free and makes the screen read as live even between probe events (a double
sample takes several seconds and the screen must not look frozen in that gap).

### 3. Keep the spinner where it is honest

PID and shaper have no countable unit, so they keep the comet spinner, but under
the phase rail rather than as the whole screen. The spinner stops being a lie
once something else on screen carries the progress.

## Motion budget

Per pono-design: entrances 140 to 240 ms ease-out, transform and opacity only;
instruments may run continuous. The cell flash is 200 ms ease-out opacity, which
sits inside that. No new continuous animation is introduced; the existing comet
spinner already owns that slot.

Hard constraint from `pono_anim.h`, and it governs the implementation: the
Centauri is 2 cores, ARMv7, no GPU, and Klipper needs the CPU. The canned
animations are bitmap flipbooks specifically so there is no per-frame vector
drawing. **The grid must therefore be static LVGL objects whose style is
recoloured on an event, never a redrawn canvas.** 121 small `lv_obj_t` cells
created once, each getting an `lv_obj_set_style_bg_color` on its probe event, is
about 121 cheap objects and one style write per probe. That is affordable. A
per-frame canvas redraw is not, and would steal exactly the CPU the probe needs.

## Why this is not scope creep

Every input already flows through the websocket client grumpyscreen runs, and
`main_panel.cpp` already parses `display_status.message` and already decides
`cal_active`. The new work is a panel and a parser for one response line. No new
data path, no new dependency, no firmware change.

## Not built

Specified only, 2026-08-02. Written while the first correctly-calibrated tune was
running, so the measurements above are from a real run rather than a guess. The
build is a grumpyscreen change and wants the cross-compile toolchain and a device
to look at, and this box has known traps in that loop.
