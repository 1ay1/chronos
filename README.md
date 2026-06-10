<h1 align="center">⟡ chronos</h1>

<p align="center">
  A <b>living sky</b> for your terminal — a graphical weather & clock app<br>
  in the spirit of a native phone clock/weather screen, drawn pixel-by-pixel.
</p>

<p align="center">
  <sub>Built on <a href="https://github.com/1ay1/maya">maya</a> · C++26 · half-block truecolor renderer · real astronomy</sub>
</p>

---

## What it is

`chronos` renders a **real-time sky** that changes with the actual local time
and your location:

- **Dawn, day, golden hour, dusk, night** — the whole sky gradient is driven by
  the sun's true altitude (a NOAA solar-position calculation), so at 6am you get
  a pink horizon and a low sun, at noon a deep blue dome, at dusk burning orange,
  and at night a star-field with the moon.
- **A real sun** that arcs across the sky by its computed azimuth & altitude,
  with a soft glow and a bright disc.
- **A real moon** at the correct phase — the lit/unlit limb is shaded from the
  synodic phase angle, with a gentle halo.
- **Drifting clouds** (animated fBm noise) lit by the sun's colour — white at
  noon, salmon at sunset, slate at night.
- **Twinkling stars** that fade in as the sun sinks below the horizon.
- **A rolling hill silhouette** along the horizon.

Everything is painted onto maya's half-block canvas (two RGB pixels per
character cell), the same technique the maya `raymarch` / `fps` demos use — so
this is genuine per-pixel graphics, not box-drawing.

## Overlaid HUD

- Big seven-segment **clock** + full date (`a` toggles big/compact).
- Your **location** and the current **moon phase** name.
- **Sunrise / sunset / daylight length** and the live **sun altitude**.
- Toggleable floating **calendar** (`c`) and **world-clocks** (`w`) panels.

## Time warp

Want to *watch* a full day go by? Press `+` to fast-forward the sky (each press
doubles the speed), `-` to run it backwards, `0` to snap back to live time.
Dawn breaks, the sun climbs and sets, stars come out — in seconds.

## Build

Requires a C++26 compiler (GCC 15+ recommended) and CMake.

```bash
git clone --recurse-submodules <this repo>
cd chronos
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/chronos
```

Already cloned without submodules? `git submodule update --init --recursive`.

Use a **truecolor terminal** (kitty, wezterm, alacritty, foot, recent
xterm/iTerm2) for the full gradient — chronos emits 24-bit colour.

## Keys

| Key        | Action                         |
|------------|--------------------------------|
| `a`        | big ⇄ compact clock            |
| `c`        | calendar panel                 |
| `w`        | world-clocks panel             |
| `+` / `-`  | time warp faster / reverse     |
| `0`        | back to live time              |
| `h` / `l`  | prev / next month (in calendar)|
| `q` / Esc  | quit                           |

## Location

`chronos` defaults to London. Export your coordinates (and an optional label)
for an accurate sky, sunrise/sunset and moon:

```bash
export CHRONOS_LAT=40.7128
export CHRONOS_LON=-74.0060
export CHRONOS_PLACE="New York"
chronos
```

Put those in your shell rc and it's a permanent fixture of your rice.

## How it works

Every section is a self-contained **graphical widget** that paints itself into a
rectangle via a shared `Painter` (the only thing that touches maya's half-block
canvas). Adding a new panel is just a new `Widget` subclass.

```
src/
  gfx.hpp              Col + colour math + noise + Painter
                       (pixels, discs, rounded panels, gauges, sparklines,
                        glow-text-over-gradient)
  widget.hpp           Rect, per-frame Ctx, the Widget base interface
  widgets/
    sky.hpp            living gradient sky + sun/moon/stars/clouds/horizon
    clock.hpp          big seven-segment clock + date
    location.hpp       top-right place pill + live/warp badge
    sun.hpp            sun-path arc card + sunrise/sunset/daylight
    moon.hpp           phase-accurate moon disc + illumination gauge
    calendar.hpp       month-grid card (h/l to navigate)
    clocks.hpp         multi-zone world-clocks card
    statusbar.hpp      bottom keybar
  astro.hpp            sun position, sunrise/sunset, moon phase (no deps)
  timeutil.hpp         world-clock zones, tz offset
  main.cpp             App: owns widgets, builds Ctx, lays out, routes events
```

The `App` composes the widgets, computes the shared astronomy once per frame
into a `Ctx`, assigns each widget a `Rect`, and dispatches `paint` / `on_key`.
The renderer is single-threaded by design: maya's SIMD cell-diff only ships the
cells that actually changed, so a slowly-evolving sky costs almost nothing.

## License

MIT. maya is MIT too.
