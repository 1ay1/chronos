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

Requires a C++26 compiler (GCC 15+ recommended), CMake, and **libcurl**
(`libcurl4-openssl-dev` on Debian/Ubuntu) for the live weather feed.

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

`chronos` **finds your location automatically** on startup: it looks up the
approximate latitude/longitude and city of your public IP (via
[ip-api.com](https://ip-api.com), no key, off-thread) and points the sky,
sunrise/sunset, moon, and weather at where you actually are. The lookup is
best-effort — if you're offline or the request is blocked, it quietly falls back
to London.

To pin an exact spot (and skip the IP lookup), export your coordinates and an
optional label:

```bash
export CHRONOS_LAT=40.7128
export CHRONOS_LON=-74.0060
export CHRONOS_PLACE="New York"
chronos
```

Any coordinate you set takes precedence over auto-location. Put those in your
shell rc and it's a permanent fixture of your rice.

## Live weather

The weather card shows **real current conditions** — temperature, feels-like,
today's high/low, humidity, and wind — pulled from the
[Open-Meteo](https://open-meteo.com) public API (no key, no signup) for your
location. Fetching happens on a background thread so the UI
never stalls; data refreshes every ~10 minutes. Until the first fetch lands (or
if you're offline) the card shows a quiet “fetching…” / “offline” state and keeps
the last good reading.

### The sky reacts to the weather

The living sky isn't just decoration — it **animates to match the real
conditions**. The fetched WMO weather code drives the scene and cross-fades
between states over a couple of seconds:

- **Clear / partly cloudy** — open sky with a few drifting cumulus.
- **Overcast** — a full grey cloud deck rolls in.
- **Rain** — wind-slanted streaks fall under a heavy sky (harder rain = denser).
- **Snow** — flakes drift down, swaying as they fall.
- **Fog** — a soft grey veil thickens toward the horizon and washes out colour.
- **Thunderstorm** — the sky darkens to a brooding charcoal and **lightning
  flashes** strike at random.

Wind speed scales how fast the clouds advect. To preview any scene without
waiting on the weather, force a code:

```bash
CHRONOS_WX_CODE=95 CHRONOS_WX_WIND=30 ./build/chronos   # thunderstorm
CHRONOS_WX_CODE=75 ./build/chronos                      # heavy snow
CHRONOS_WX_CODE=45 ./build/chronos                      # fog
```

### Reflections & aurora

A still **lake** sits at the foot of the scene and mirrors the sky in real time
— the gradient, a rippling specular glint from the sun (or moon at night), and
whatever the weather is doing, all wobbling with travelling surface waves.

When your auto-located latitude is far enough north (or south), clear nights get
an **aurora**: undulating green-to-violet curtains drifting across the upper sky.
It pays off the IP geolocation — chase the lights just by being somewhere cold.

### Golden hour, rainbows & constellations

A few finishing touches make the scene feel alive at the edges of the day:

- **Golden-hour grade** — for the ~40 minutes around sunrise and sunset, the
  whole frame gets a warm filmic colour grade: amber highlights, a cool teal
  lift in the shadows, and an overall low-sun glow. It ramps up as the sun drops
  and fades to neutral by midday (and to night by full dark).
- **Rainbow after rain** — when the rain eases off while the sun is still up and
  low, a **42° rainbow arc** appears in the sky opposite the sun, with a fainter
  reversed secondary bow outside it.
- **Constellations** — on clear nights the Big Dipper, Orion, and Cassiopeia are
  drawn as bright twinkling stars joined by faint lines, drifting slowly west
  through the night. They fade out under cloud or fog.

Try them with the weather override — golden hour fires automatically at a low
sun, and a clearing shower paints the bow:

```bash
CHRONOS_WX_CODE=61 ./build/chronos    # rain — wait for it to clear for a rainbow
CHRONOS_LAT=45 CHRONOS_LON=10 ./build/chronos   # mid-latitude clear night = constellations
```

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
    weather.hpp        live conditions card (Open-Meteo, real data)
    calendar.hpp       month-grid card (h/l to navigate)
    clocks.hpp         multi-zone world-clocks card
    statusbar.hpp      bottom keybar
  astro.hpp            sun position, sunrise/sunset, moon phase (no deps)
  timeutil.hpp         world-clock zones, tz offset
  weather.hpp          Open-Meteo fetch service (libcurl, background thread)
  geo.hpp              auto-locate via public IP (ip-api.com, off-thread)
  main.cpp             App: owns widgets, builds Ctx, lays out, routes events
```

The `App` composes the widgets, computes the shared astronomy once per frame
into a `Ctx`, assigns each widget a `Rect`, and dispatches `paint` / `on_key`.
The renderer is single-threaded by design: maya's SIMD cell-diff only ships the
cells that actually changed, so a slowly-evolving sky costs almost nothing.

## License

MIT. maya is MIT too.
