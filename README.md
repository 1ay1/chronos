<h1 align="center">⟡ chronos</h1>

<p align="center">
  A pretty terminal dashboard for time, calendar, and sky —<br>
  the go-to glance-panel for your Linux rice.
</p>

<p align="center">
  <sub>Built on <a href="https://github.com/1ay1/maya">maya</a> · C++26 · truecolor · zero runtime deps</sub>
</p>

---

```
  ╭──────────────────────────────────────────╮  ╭──────────────────────────────────╮
  │  ⟡ CHRONOS    WEDNESDAY · June 10, 2026   │  │  ▌ WORLD CLOCKS                   │
  │                                           │  │  Local ☀ 16:42  Wed 10 Jun        │
  │    █   ██       █  █  ██                   │  │  UTC   ☀ 14:42  Wed 10 Jun        │
  │   ██  █      █  █  █ █  █                  │  │  NYC   ☀ 10:42  Wed 10 Jun        │
  │    █  ███       ████   █                   │  │  TOK   ☽ 23:42  Wed 10 Jun        │
  │          :22                              │  │  SYD   ☽ 00:42  Thu 11 Jun        │
  ╰──────────────────────────────────────────╯  ╰──────────────────────────────────╯
  ╭──────────────────────────────────────────╮  ╭──────────────────────────────────╮
  │  ▌ CALENDAR                               │  │  ▌ SKY                            │
  │       June 2026                           │  │  ↑ 05:44                22:16 ↓   │
  │  Mo Tu We Th Fr Sa Su                     │  │  ···········☀‾‾‾‾‾‾               │
  │   8  9 10 11 12 13 14                      │  │  daylight 16h 32m                 │
  │  15 16 17 18 19 20 21                      │  │  🌘 Waning crescent               │
  │  22 23 24 25 26 27 28                      │  │  ███░░░░░░░░░░░ 23%               │
  ╰──────────────────────────────────────────╯  ╰──────────────────────────────────╯
```

## Features

- **Big live clock** — seven-segment HH:MM with ticking seconds, plus full date.
- **Month calendar** — today highlighted, weekends accented, navigate freely.
- **World clocks** — six zones with day/night glyphs (☀/☽), live from the OS tz database.
- **Sky panel** — accurate sunrise/sunset (NOAA solar algorithm), daylight length,
  a live sun-arc showing the sun's position across the day, and the current
  moon phase with illumination bar.
- **Upcoming events** — rolling countdown to the next holidays & solstices.
- **All truecolor**, Tokyo-Night palette, rounded panels. Looks at home in any rice.

## Build

Requires a C++26 compiler (GCC 15+ recommended) and CMake.

```bash
git clone --recurse-submodules <this repo>
cd chronos
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/chronos
```

If you already cloned without `--recurse-submodules`:

```bash
git submodule update --init --recursive
```

## Keys

| Key       | Action            |
|-----------|-------------------|
| `h` / `←` | previous month    |
| `l` / `→` | next month        |
| `g` / `↑` | previous year     |
| `G` / `↓` | next year         |
| `t`       | jump to today     |
| `a`       | toggle big clock  |
| `q` / Esc | quit              |

## Location (for accurate sun & moon)

`chronos` defaults to London. Export your coordinates to get correct
sunrise/sunset and daylight for your location:

```bash
export CHRONOS_LAT=40.7128
export CHRONOS_LON=-74.0060
chronos
```

Drop those in your shell rc and you're set.

## Customizing your rice

Everything is in `src/`:

- `src/main.cpp` — palette (`namespace pal`), layout, panels, the big-font clock.
- `src/timeutil.hpp` — `default_zones()` (your world clocks) and the holiday list.
- `src/astro.hpp` — sun/moon math.

Change the world clocks by editing `default_zones()`; recolor by editing `pal`.

## License

MIT. maya is MIT too.
