# Spike 0 — evidence report

Answers to OQ1 and OQ2 from
`docs/superpowers/specs/2026-11-03-linotch-design.md`, measured on the target
machine against the running cosmic-comp. Throwaway spike; not product code.

## Verdict

- **OQ1 — yes.** After `QWindow::create()` and before any commit, Qt's Wayland
  platform plugin exposes a non-null `wl_display` and a non-null `wl_surface`.
  Measured 3–4 ms after `create()` returned. A layer surface created on that
  `wl_surface` gets a `configure` 1 ms later and is on screen at ~10 ms.
- **OQ2 — 21.6 MB RSS / 0.000% CPU** over 300 s for a Qt-painted 260x36 TOP
  layer surface, **provided Qt's EGL stack is not loaded**. With Qt's default
  Wayland-EGL path the same 260x36 surface costs **111.1 MB RSS** and fails the
  design's 60 MiB target. The cheap fix is `QT_WAYLAND_DISABLE_WINDOWDECORATION=1`
  (see "Fallback notes").
- The design's literal sequence — create the layer role, then call
  `QWindow::show()` — **does not work**: Qt tries to add an xdg-shell role to a
  surface that already has one and the compositor kills the connection.

## Environment

| Item | Value |
|------|-------|
| OS / kernel | Pop!_OS 24.04 LTS / 7.2.6-76070206-generic |
| Compositor | cosmic-comp `0.1~1792508925~24.04~8cb1802` |
| Wayland socket | `wayland-1` (`WAYLAND_DISPLAY` unset; exported for all runs) |
| Qt | 6.4.2 (Core, Gui), QPA `wayland` |
| Toolchain | CMake 3.28.3, Ninja 1.11.1, GCC 13.3.0 |
| wayland | libwayland-dev, wayland-scanner 1.23.1 |
| Machine | 12 cores, 15,753,812 kB RAM |

Advertised by cosmic-comp: `zwlr_layer_shell_v1` v5, `wl_shm` v2. The Qt
`wl_surface` is protocol version 4, so `wl_surface_damage_buffer` is available.

## OQ1 — ordering used

The spike's `start()` (`src/main.cpp:117`) does exactly this and logs each step:

| # | Step | Timestamp (manual run) |
|---|------|------------------------|
| 1 | `QGuiApplication` with `QT_QPA_PLATFORM=wayland` | 0 ms |
| 2 | `QWindow` constructed and `resize(260,36)` | 3 ms |
| 3 | **`window->create()`** (`src/main.cpp:131`) — creates the `QWaylandWindow`, which initialises the `wl_surface`; `isVisible()==0`, no commit has been sent | 3 ms |
| 4 | `nativeResourceForIntegration("display")` (`src/main.cpp:141`) → `wl_display*` **non-null** | 3 ms |
| 5 | `nativeResourceForWindow("surface", window)` (`src/main.cpp:142`) → `wl_surface*` **non-null** | 3 ms |
| 6 | `wl_display_get_registry` + bind `wl_shm` v1 and `zwlr_layer_shell_v1` v5 | 8 ms |
| 7 | `zwlr_layer_shell_v1_get_layer_surface` with TOP / top anchor / 260x36 / exclusive zone 0 / keyboard none (`src/main.cpp:168-181`) | 8 ms |
| 8 | **First commit**: `wl_surface_commit` with no buffer attached (`src/main.cpp:201`) | 8 ms |
| 9 | `configure` event, serial acked; requested = using = 260x36 | 9 ms |
| 10 | Buffer attached (owned by mode) and committed → `mapped=yes` | 10 ms |
| 11 | `wl_callback.done` — compositor presented the surface | 16 ms |

Notes:

- `QWindow::winId()` is **not** the `wl_surface` (it returns 1). The QPA native
  interface is required.
- Qt does not commit between step 3 and step 8, so the ordering constraint
  "after `create()`, before the first commit" is satisfied by construction and
  is visible in the log (`visible=0` at step 3).
- The spike never dispatches the `wl_display` itself. It only calls
  `wl_display_flush`; the Qt event loop owns the default queue and delivers our
  registry/configure/frame events. No `wl_display_roundtrip`, no second thread.
- This uses `QPlatformNativeInterface`, which is QPA private API
  (`third_party/qt6/README.md`).

Raw log: `evidence/measure-manual-60s.txt`, phase lines repeated from
`evidence/logs/manual-60s.log`.

## OQ2 — idle footprint

All runs: 260x36 mapped layer surface, no input, no animation, no repaint after
the first frame. RSS from `/proc/<pid>/smaps_rollup`, CPU from
`/proc/<pid>/stat`. Design thresholds: **RSS ≤ 60 MiB (61,440 kB)**, **mean CPU
≤ 0.3% over 300 s**.

| Variant (mode) | Window | RSS mean (kB) | RSS min/max (kB) | PSS at exit (kB) | CPU total mean | CPU steady mean | Verdict |
|---|---|---|---|---|---|---|---|
| manual `wl_shm`, Qt not painting | 60 s | 20,860 | 20,860 / 20,860 | 7,224 | 0.000% | 0.000% | PASS |
| `qt-bypass` + `QT_WAYLAND_DISABLE_WINDOWDECORATION=1` | 5 s | 21,660 | — | 6,005 | — | — | PASS |
| **`qt-bypass` + `QT_WAYLAND_DISABLE_WINDOWDECORATION=1`** | **300 s** | **21,620** | **21,620 / 21,620** | **7,578** | **0.000%** | **0.000%** | **PASS** |
| `qt-bypass`, EGL forced off by requesting a missing integration | 60 s | 21,644 | 21,644 / 21,644 | 7,605 | 0.000% | 0.000% | PASS |
| same | 300 s | 21,616 | 21,616 / 21,616 | 7,603 | 0.000% | 0.000% | PASS |
| `qt-bypass` with Qt's default Wayland-EGL stack | 60 s | 111,080 | 111,080 / 111,080 | 33,550 | 0.000% | 0.000% | **FAIL (RSS)** |

Reading:

- RSS is flat to the kilobyte in every run: no growth, no repaint, no buffer
  churn while idle. One frame callback was delivered, then silence.
- CPU is 0.000% in aggregate over both 60 s and 300 s windows (12 cores; the
  sampler excludes only the first two samples from "steady mean").
- The ~20.9–21.6 MB range is the floor for a Qt Gui process holding a Wayland
  connection and one shm buffer. Qt's Gui library alone accounts for most of it.
- The 111 MB case is not Qt overhead per se: it is the Mesa/NVIDIA EGL stack.
  Per-library RSS summed from `/proc/<pid>/smaps` for that run:
  `libLLVM.so.20.1` 43,968 kB, `libnvidia-*` 29,656 kB,
  `libgallium-26.1.6` 7,152 kB — ~81 MB of the 111 MB. With the fix below,
  only the `libEGL.so.1` dispatcher is mapped (72 kB resident) and no driver
  library is loaded.

Artifacts (sampler output and full spike logs): `evidence/measure-*.txt`,
`evidence/logs/*.log`.

## Failure and fallback notes

1. **`show()` after the layer role is fatal.**
   `--mode=qt-show` creates the layer surface, then calls `window->show()`
   without the bypass:

   ```
   [8 ms] control mode: calling QWindow::show() WITHOUT bypass; expecting a wl_surface role conflict
   xdg_wm_base#29: error 0: Surface already has a role.
   The Wayland connection experienced a fatal error: Protocol error
   exit=1
   ```

   `show()` runs `QWaylandWindow::initWindow()`, which creates an xdg-shell
   surface/toplevel on the same `wl_surface`. There is no way to have both
   roles. The layer role must be the only role on this surface.

2. **Working Qt-rendering path: bypass + late `show()`.**
   Set `QT_WAYLAND_USE_BYPASSWINDOWMANAGERHINT=1` **before** `QGuiApplication`
   and `Qt::BypassWindowManagerHint` before `create()`
   (`src/main.cpp:470-472`, `:128-129`). Qt then skips shell-surface creation.
   `show()` is called only after `configure` has been acked
   (`src/main.cpp:280`), and content is flushed through a `QBackingStore`
   (`beginPaint` → `QPainter(backingStore->paintDevice())` → `endPaint` →
   `flush`, `src/main.cpp:284-296`). The surface maps and receives a frame
   callback.

3. **Qt's default EGL path blows the RSS budget.**
   Without any knob, the first `flush()` loads the Wayland-EGL client buffer
   integration. RSS goes from about the 21 MB idle level (see manual mode) to
   ~109 MB after `show()`; `libLLVM` alone is ~44 MB resident. This fails the
   60 MiB criterion at any surface size — the layer surface geometry is
   irrelevant.

4. **Cheap, principled fix: `QT_WAYLAND_DISABLE_WINDOWDECORATION=1`.**
   With decorations disabled, Qt's `QWaylandWindow::supportsWindowDecoration()`
   returns before loading the client buffer integration, so the EGL stack is
   never loaded; Qt falls back to its internal `wl_shm` path. Measured
   21.6 MB / 0.000% CPU over 300 s, surface mapped and presented normally.
   This is the recommended configuration for the product shell surface.
   Cost: no server-side decorations — acceptable, since a layer surface has
   none anyway.

5. **Worst-case fallback: manual `wl_shm`.**
   `--mode=manual` never asks Qt to paint: it allocates a memfd, mmaps it,
   fills XRGB8888, creates a `wl_shm_pool`, attaches, damages and commits.
   20.9 MB RSS / 0.000% CPU. This is the floor if Qt's backing store path is
   ever rejected, at the cost of implementing our own raster/paint path.

6. **QML/Quick not measured.** `qt6-declarative-dev` and
   `qml6-module-qtquick` are not installed and no packages may be installed, so
   the QML tree and its footprint are untested. Expectation to verify later:
   the default Quick backend is OpenGL and would load the EGL stack (fails
   60 MiB); `QT_QUICK_BACKEND=software` plus
   `QT_WAYLAND_DISABLE_WINDOWDECORATION=1` is the likely configuration that
   stays near the 21.6 MB floor. Treat that as an open item, not a result.

7. **Single output only.** `get_layer_surface` was called with a null output,
   so cosmic-comp placed the surface on its chosen output. Multi-monitor
   placement (`wl_output` selection, per-output surfaces) was not tested.

8. **Presentation is proven by protocol events, not a screenshot.** The spike
   logs `configure`, `mapped=yes` after commit, and `wl_callback.done`. No
   desktop screenshot was taken, to avoid capturing the user's screen.

## Reproduce

Clean-build check: `cmake -S . -B <dir> -G Ninja && cmake --build <dir>` returns
0 with no warnings, and the resulting binary maps the surface and receives a
frame callback (`--mode=manual --duration=2`).

```bash
cd /home/kufgr/LiNotch/spike
cmake -S . -B build -G Ninja && cmake --build build

export XDG_RUNTIME_DIR=/run/user/$(id -u) WAYLAND_DISPLAY=wayland-1 QT_QPA_PLATFORM=wayland

# OQ1 + control failure
./build/linotch-layer-shell-spike --mode=manual  --duration=10
./build/linotch-layer-shell-spike --mode=qt-show --duration=2     # expected rc=1

# OQ2, 300 s
DURATION=300 MODE=manual ./scripts/measure-oq2.sh ./build/linotch-layer-shell-spike
QT_WAYLAND_DISABLE_WINDOWDECORATION=1 DURATION=300 MODE=qt-bypass \
  ./scripts/measure-oq2.sh ./build/linotch-layer-shell-spike
```

## Provenance and dependencies

- `protocol/wlr-layer-shell-unstable-v1.xml` — wlr-protocols commit
  `1106a4a1c57562ddd438c547da4c0d6dc150d624`, SHA-256
  `87e0b9c8…b5e2ed8`, interfaces v5/v5, unmodified.
- `protocol/xdg-shell.xml` — wayland-protocols commit
  `3eab2dccf4bfdd85c16d312a6c8c8fbc2a6e73a8`, SHA-256 `8cd3980d…6647d9de`,
  vendored only because layer-shell's `get_popup` references
  `xdg_popup_interface` at link time.
- `third_party/qt6/qpa/qplatformnativeinterface.h` — verbatim from
  `qt6-base-private-dev` 6.4.2+dfsg-21.1build5. `qt6-base-private-dev` is not
  installed and could not be installed (sudo needs a password). A real build
  should depend on that package rather than the vendored header.
- Generated protocol code is built by `wayland-scanner` into
  `build/protocol/generated/` and is not committed. One build-only `sed`
  renames the generated parameter `namespace` to `namespace_` because it is a
  C++ keyword; the vendored XML is untouched and the ABI is unchanged.

## Residual risks

- QML/Quick footprint unmeasured (blocking follow-up before the shell-spine
  decision).
- `QT_WAYLAND_DISABLE_WINDOWDECORATION` is a Qt environment knob whose name is
  about decorations, not about avoiding EGL. Its effect here is real and
  measured, but it is an implementation detail of Qt 6.4's Wayland plugin; a
  Qt upgrade could change it. A more explicit guard would be a link/runtime
  assertion that `libgallium`/`libnvidia-eglcore` are not loaded.
- Only Qt 6.4.2 was tested; newer Qt may change the bypass/decoration
  behaviour.
- Layer-shell v5 was exercised against cosmic-comp only.
