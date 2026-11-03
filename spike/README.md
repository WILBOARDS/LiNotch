# Spike 0 / 0b — Qt 6 + wlr-layer-shell feasibility probe

Throwaway spike. Not product code, not the project performance harness.

Answers the design questions from
`docs/superpowers/specs/2026-11-03-linotch-design.md`:

- **OQ1** — after `QWindow::create()` and before the first commit, can we obtain a
  non-null `wl_display` and `wl_surface` from Qt?
- **OQ2** — what are the idle RSS and CPU floors on cosmic-comp for a 260x36
  TOP layer surface?
- **Spike 0b** — what does a Qt Quick surface cost next to the hand-painted
  `QBackingStore` path, and which one should the product use?

Results, commands and numbers: see [`EVIDENCE.md`](EVIDENCE.md).

## Requirements

- Qt 6.4.2 (Core, Gui) dev packages; `qt6-declarative-dev` and the six
  `qml6-module-*` runtime modules for the Quick half (installed 2026-11-03,
  see EVIDENCE finding 4)
- CMake >= 3.16, Ninja, a C++17 compiler
- `libwayland-dev` and `wayland-scanner`
- `pkg-config`
- `qt6-base-private-dev` for `qpa/qplatformnativeinterface.h`.
  **Not installed here**; a verbatim copy of that single header is vendored under
  `third_party/qt6/` — see `third_party/qt6/README.md`. A real build should add the
  package instead of the vendored header.

## Build

```bash
cd spike
cmake -S . -B build -G Ninja
cmake --build build
```

Produces **two** executables from one source file: `linotch-spike-gui`
(`Qt6::Gui` only) and `linotch-spike-quick` (adds `Qt6::Quick`). They must stay
separate: measuring the QPainter path in a binary that also links Quick charges
it for ~17 MB of resident Quick/Qml/Widgets/ICU pages. See EVIDENCE, "Method".

Protocol code is generated at build time from the vendored XML in `protocol/`
into `build/protocol/generated/`. Provenance and hashes: `protocol/PROVENANCE.md`.

## Run

The Wayland socket on this machine is `wayland-1` and `WAYLAND_DISPLAY` is unset:

```bash
export XDG_RUNTIME_DIR=/run/user/$(id -u)
export WAYLAND_DISPLAY=wayland-1
export QT_QPA_PLATFORM=wayland

./build/linotch-spike-gui --duration=10
```

Modes:

| Flag | Binary | Meaning |
|------|--------|---------|
| `--mode=manual` | gui | default. Create the layer surface on the Qt `wl_surface` and attach our own `wl_shm` buffer (`XRGB8888`). No Qt painting. |
| `--mode=qt-bypass` | gui | Let Qt paint via `QBackingStore` while suppressing Qt's xdg-shell role. Sets `QT_WAYLAND_USE_BYPASSWINDOWMANAGERHINT=1` and `Qt::BypassWindowManagerHint`. Paints the same pill/panel content as the QML trees. |
| `--mode=qt-quick` | quick | `QQuickView` paints the layer surface, with `qml/Pill.qml` or `qml/Panel.qml`. |
| `--mode=qt-show` | gui | Control experiment. Calls `show()` without the bypass; expected to fail with a wl_surface role conflict. |

Flags:

| Flag | Meaning |
|------|---------|
| `--duration=N` | Seconds to keep the surface mapped (default 10). |
| `--width=` / `--height=` | Surface size; `height > 100` selects the panel layout. |
| `--qml=File.qml` | Quick only; a bare filename in `spike/qml/`. |
| `--animate` | Repaint at 16 ms continuously (equaliser bars / meter widths). With `--region`, only the equaliser column is damaged. |
| `--plain` | Draw the silhouette only, no text or paths — isolates the cost of content. |
| `--no-alpha` | Skip the `QSurfaceFormat` alpha request (measured: changes nothing). |
| `--alpha-probe` | Paint the left half opaque and the right half 37 % black, to answer the transparency question by eye. |

In every mode the layer surface is created with: TOP layer, top anchor,
260x36 by default, exclusive zone 0, keyboard interactivity none, and an initial
commit with no buffer attached.

> **Do not run `--mode=qt-show` against a compositor you care about**: the
> resulting protocol error kills the Wayland connection of that client. It is
> safe here because it only affects the spike process, but it is a control
> failure, not a usable mode.

## Measuring

Spike-local sampler (Jack owns the permanent `tests/perf/idle-footprint.sh`):

```bash
# QPainter idle pill (recommended product configuration)
QT_WAYLAND_DISABLE_WINDOWDECORATION=1 DURATION=300 MODE=qt-bypass \
  ./scripts/measure-oq2.sh ./build/linotch-spike-gui

# Qt Quick idle pill (only affordable Quick configuration)
QT_QUICK_BACKEND=software QT_WAYLAND_DISABLE_WINDOWDECORATION=1 DURATION=300 MODE=qt-quick \
  ./scripts/measure-oq2.sh ./build/linotch-spike-quick

# Manual wl_shm reference
DURATION=300 MODE=manual ./scripts/measure-oq2.sh ./build/linotch-spike-gui
```

The script waits for `mapped=yes`, samples `/proc/<pid>/smaps_rollup` RSS and
`/proc/<pid>/stat` CPU once per second, and reports mean/min/max RSS plus
total-mean, steady-mean, median and max CPU. Limits default to 60 MiB RSS and
0.3% mean CPU (`RSS_LIMIT_KB`, `CPU_LIMIT_PERCENT` override them).

Pass spike flags as extra arguments, and set environment variables in the shell
(`export FOO=1`), **not** as `env FOO=1` arguments — the script appends trailing
arguments to the binary, so `env` there silently becomes argv.

`QT_WAYLAND_DISABLE_WINDOWDECORATION=1` matters in both paths: without it Qt
loads the Wayland-EGL client buffer integration and pulls in the Mesa/NVIDIA
stack. See `EVIDENCE.md`.

## Layout

```
spike/
  CMakeLists.txt              build + wayland-scanner generation, two targets
  src/main.cpp                the probe (all modes)
  qml/Pill.qml                idle content for --mode=qt-quick
  qml/Panel.qml               420x340 panel content for --mode=qt-quick
  protocol/                   vendored protocol XML + PROVENANCE.md
  third_party/qt6/            vendored QPA header + README.md
  scripts/measure-oq2.sh      spike-local OQ2 sampler
  evidence/                   raw sampler output and full spike logs
  README.md                   this file
  EVIDENCE.md                 OQ1/OQ2/Spike 0b answers, numbers, failure notes
```

`build/` is generated and gitignored.
