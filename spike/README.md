# Spike 0 — Qt 6 + wlr-layer-shell feasibility probe

Throwaway spike. Not product code, not the project performance harness.

Answers two design questions from
`docs/superpowers/specs/2026-11-03-linotch-design.md`:

- **OQ1** — after `QWindow::create()` and before the first commit, can we obtain a
  non-null `wl_display` and `wl_surface` from Qt?
- **OQ2** — what are the idle RSS and CPU floors on cosmic-comp for a 260x36
  TOP layer surface?

Results, commands and numbers: see [`EVIDENCE.md`](EVIDENCE.md).

## Requirements

Present on the target machine, nothing was installed:

- Qt 6.4.2 (Core, Gui) dev packages
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

Protocol code is generated at build time from the vendored XML in `protocol/`
into `build/protocol/generated/`. Provenance and hashes: `protocol/PROVENANCE.md`.

## Run

The Wayland socket on this machine is `wayland-1` and `WAYLAND_DISPLAY` is unset:

```bash
export XDG_RUNTIME_DIR=/run/user/$(id -u)
export WAYLAND_DISPLAY=wayland-1
export QT_QPA_PLATFORM=wayland

./build/linotch-layer-shell-spike --duration=10
```

Options:

| Flag | Meaning |
|------|---------|
| `--mode=manual` | default. Create the layer surface on the Qt `wl_surface` and attach our own `wl_shm` buffer (`XRGB8888`). No Qt painting. |
| `--mode=qt-bypass` | Let Qt paint via `QBackingStore` while suppressing Qt's xdg-shell role. Sets `QT_WAYLAND_USE_BYPASSWINDOWMANAGERHINT=1` and `Qt::BypassWindowManagerHint`. |
| `--mode=qt-show` | Control experiment. Calls `show()` without the bypass; expected to fail with a wl_surface role conflict. |
| `--duration=N` | Seconds to keep the surface mapped (default 10). |

In every mode the layer surface is created with: TOP layer, top anchor,
260x36, exclusive zone 0, keyboard interactivity none, and an initial commit
with no buffer attached.

> **Do not run `--mode=qt-show` against a compositor you care about**: the
> resulting protocol error kills the Wayland connection of that client. It is
> safe here because it only affects the spike process, but it is a control
> failure, not a usable mode.

## Measuring OQ2

Spike-local sampler (Jack owns the permanent `tests/perf/idle-footprint.sh`):

```bash
# Qt-rendered, EGL avoided (recommended path)
QT_WAYLAND_DISABLE_WINDOWDECORATION=1 DURATION=300 MODE=qt-bypass \
  ./scripts/measure-oq2.sh ./build/linotch-layer-shell-spike

# Manual wl_shm reference
DURATION=300 MODE=manual ./scripts/measure-oq2.sh ./build/linotch-layer-shell-spike
```

The script waits for `mapped=yes`, samples `/proc/<pid>/smaps_rollup` RSS and
`/proc/<pid>/stat` CPU once per second, and reports mean/min/max RSS plus
total-mean, steady-mean, median and max CPU. Limits default to 60 MiB RSS and
0.3% mean CPU (`RSS_LIMIT_KB`, `CPU_LIMIT_PERCENT` override them).

`QT_WAYLAND_DISABLE_WINDOWDECORATION=1` matters: without it Qt loads the
Wayland-EGL client buffer integration and pulls in the Mesa/NVIDIA stack, which
triples RSS. See `EVIDENCE.md`.

## Layout

```
spike/
  CMakeLists.txt              build + wayland-scanner generation
  src/main.cpp                the probe (all three modes)
  protocol/                   vendored protocol XML + PROVENANCE.md
  third_party/qt6/            vendored QPA header + README.md
  scripts/measure-oq2.sh      spike-local OQ2 sampler
  evidence/                   raw sampler output and full spike logs
  README.md                   this file
  EVIDENCE.md                 OQ1/OQ2 answers, numbers, failure notes
```

`build/` is generated and gitignored.
