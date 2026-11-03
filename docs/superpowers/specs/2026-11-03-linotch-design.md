# LiNotch design

A lightweight, desktop-agnostic notch for Linux: a pill anchored to the top edge of the
screen that stays out of the way, and opens into a glanceable panel with media,
notifications, system meters and quick controls.

Status: design agreed, no implementation yet. The five unknowns in *Open questions* are
resolved by Spike 0 before any feature code is written.

## Hardware and OS this was designed against

Primary development machine, verified on 2026-11-03:

- ASUS Vivobook M7400QE
- Pop!_OS 24.04 LTS, `XDG_CURRENT_DESKTOP=COSMIC`, `XDG_SESSION_TYPE=wayland`
- Compositor: `cosmic-comp` (wlroots-derived, implements `zwlr_layer_shell_v1`)
- Hybrid graphics: AMD Cezanne [Radeon Vega Series] (kernel driver in use on the VGA
  controller) plus NVIDIA GA107M [GeForce RTX 3050 Ti Mobile] (`nvidia` driver)
- 15 GiB RAM
- Qt 6.4.2 (`/usr/lib/x86_64-linux-gnu`), GCC 13.3.0, CMake 3.28.3, Ninja 1.11.1,
  wayland-scanner 1.23.1
- Installed and relevant: `qt6-base-dev`, `qt6-wayland`, `python3-gi`
- Not installed: GTK4 typelibs, libadwaita, any layer-shell binding, `rustc`/`cargo`

Two facts from this machine drove the design. First, `liblayershellqtinterface` in the
Pop!_OS 24.04 archive depends on `qtbase5-dev` — there is **no Qt6 layer-shell-qt**
package, so the binding has to come from somewhere else. Second,
`cosmic-notifications` (a child of `cosmic-session`, not a systemd user unit) owns
`org.freedesktop.Notifications`, and introspection shows it publishes only the standard
interface — no history, no extra signals. There is no way to observe desktop
notifications passively. Mirroring them means becoming the notification server.

## Goals

1. Idle cost that is invisible: the notch is the kind of thing you leave running for
   months, so the steady state must cost almost nothing in memory and CPU.
2. Works on any Linux desktop that can host a top-anchored always-on-top surface, and
   degrades without breaking when a feature's backend is absent.
3. Never leaves the desktop in a worse state than it found it. This is the whole
   notification story: LiNotch displaces an existing notification daemon and must put it
   back, including when LiNotch is killed.
4. Everything the notch does is reachable from the command line, so it is scriptable and
   testable without a screen.

## Non-goals for v1

- A GNOME Shell extension. `mutter` does not implement `zwlr_layer_shell_v1`, so a
  client process cannot be a notch on GNOME at all; the only real answer is a separate
  extension in JavaScript. Recorded as a future plan, sharing the same D-Bus service so
  no feature logic would be duplicated.
- Per-app notification rules, reply-from-notch, camera/microphone indicators, clipboard
  history, workspace switcher.
- Themes beyond a dark/light foreground choice, and a plugin system for tiles.

## Modes

One window, one Wayland surface, two modes. Not two surfaces: a second surface means a
second scene-graph render pass and a crossfade that buys nothing.

State is owned by compiled code, `enum class NotchMode { Idle, Opened }` in
`core/NotchState`, exposed to QML as a context property. QML `State` and `Transitions`
render it. Timers and D-Bus callbacks need to read and change the mode, and they are all
C++; if the authority lived in QML, every service would need a round trip into the UI
layer to do its job.

### Idle

- 260 x 36 px, anchored to the top edge and horizontally centred, `exclusive_zone` 0.
- Square top corners flush with the bezel, ~18 px radius on the bottom two, so it reads
  as a notch hanging from the top rather than a floating pill.
- Solid black fill. Qt 6.4 has no `ShaderEffects` module, so there is no backdrop blur
  available anyway; a real MacBook notch is opaque, so the constraint and the target look
  agree.
- Content, left to right: `HH:MM`, hairline separator, media indicator (present only while
  a player is active), unread indicator (present only when count > 0), battery glyph and
  percentage.
- One timer for the whole mode, aligned to the minute boundary. Nothing repaints unless a
  published value actually changed.

### Opened

- 420 x 340 px, grown downward from the idle rect; width and height animate together over
  ~180 ms with a small overshoot.
- Stacked sections: header (full date and time, close affordance), now-playing with
  previous/play-pause/next, meters (battery, CPU, RAM, network as thin bars), control grid
  (volume, brightness, do-not-disturb, Wi-Fi, Bluetooth), unread notifications.
- A control tile is **absent**, not disabled, when its backend is not available.
- The section tree is created on first open and destroyed 8 s after the last close. This
  is what makes "full control centre" and "lightweight" compatible: the steady state stays
  the idle state, forever.
- CPU and RAM sampling runs at 1 s only while opened. Battery, network and media state are
  D-Bus signal driven and cost nothing when nothing changes.

### Entering and leaving Opened

- A global shortcut toggles it. Default `Alt+Super+N`, user-configurable.
- `Esc` closes it, handled inside the app via layer-shell keyboard interactivity, so it
  needs no global grab.
- Notifications never change the mode. An arrival lights the idle indicator; the user
  opens the panel to read it. Nothing moves on its own, so nothing costs a frame unasked.

Why not plain `Alt+Super`: the freedesktop portal represents a trigger as
`MODIFIERS+KEYSYM`, and per-desktop shortcut tables have the same shape, so a
modifier-only chord is not expressible through any route that survives the app quitting.
The default therefore carries a letter key.

## Platform support

The gate is the compositor, not the distribution.

| Platform | v1 | Mechanism |
|---|---|---|
| wlroots Wayland — COSMIC, Sway, Hyprland, labwc, Wayfire, river | yes | `zwlr_layer_shell_v1`, TOP layer, top anchor, exclusive zone 0 |
| KDE Plasma Wayland | yes | KWin implements layer-shell; its own panels use it |
| X11, any DE | yes | `_NET_WM_WINDOW_TYPE_DOCK` + `_NET_WM_STRUT_PARTIAL` + always-on-top via raw libxcb |
| GNOME / mutter | **no** | mutter does not implement layer-shell; needs a Shell extension (future plan) |

The X11 backend needs raw libxcb rather than Qt X11 helpers: `QX11Extras` was removed in
Qt 6 and the replacement `QNativeInterface::QX11Application` is newer than the 6.4.2 this
project targets. Selecting the backend at runtime from `QGuiApplication::platformName()`
keeps one binary.

## layer-shell binding

Route: **hand-vendored protocol in the application.**

1. Vendor `wlr-layer-shell-unstable-v1.xml` into `protocol/`, pinned, with the upstream
   copy it came from recorded in a header comment.
2. Generate client code with `wayland-scanner` (already installed).
3. Obtain the Wayland objects from Qt: `wl_display` from
   `QPlatformNativeInterface::nativeResourceForIntegration("display")`, `wl_surface` from
   `nativeResourceForWindow("surface", window)`.
4. Link `libwayland-client` only.

The ordering constraint is the subtle part: Qt creates the `wl_surface` lazily, so it does
not exist until the window is created, and `get_layer_surface()` must be issued before the
surface's first commit. The sequence has to be `window->create()` → fetch surface →
`zwlr_layer_shell_v1_get_layer_surface()` → set layer, anchor, size, margins, exclusive
zone, keyboard interactivity → `window->show()`. Do it in the wrong order and the
compositor either rejects the surface or maps it as an ordinary window.

Keyboard interactivity is `none` while idle and on-demand while opened, using
`lock_keyboard`/`unlock_keyboard` where the compositor advertises
`zwlr_layer_surface_v1` interface version 2 or newer, and plain `set_keyboard_interactivity`
otherwise.

Fallback ladder if the assumption fails, in order: reimplement as an in-tree
`QtWaylandClient` extension subclass (hooks real surface creation, more correct across
remap and output change, but private Qt API that a 6.4 → 6.7 upgrade can break); build
KDE `layer-shell-qt` from source (its kf6 branch wants `extra-cmake-modules` >= 6.0 and
these repositories offer 5.115, so the version guard would have to be patched); or move
the surface to C/GTK3 with the apt-packaged `gtk-layer-shell`, which is proven but changes
the stack and forfeits QML animation.

## Architecture

```
protocol/wlr-layer-shell-unstable-v1.xml   vendored, pinned
src/main.cpp                     argv, backend selection, lifecycle
src/core/NotchState              Idle/Opened, transition guards, close timer -> QML
src/core/Config                  QSettings ini, defaults, CLI-visible
src/core/Registry                provider list, capability snapshot for the UI
src/platform/SurfaceBackend.h    attach() / setRect() / setFocusMode()
src/platform/LayerShellBackend   Wayland implementation of the above
src/platform/X11DockBackend      libxcb dock window + strut
src/services/NotificationsService  own bus name, forward, record displaced daemon
src/services/DaemonRestoreService  /proc cmdline + user-unit discovery, relaunch
src/services/MediaService          MPRIS watcher, active player, transport controls
src/services/AudioService          PipeWire/Pulse D-Bus, ALSA control fallback
src/services/PowerService          Upower: battery state, backlight
src/services/NetService            NetworkManager, iwd fallback
src/services/ShortcutService       portal -> per-desktop writer -> CLI floor
src/services/SysmonService         /proc/stat, /proc/meminfo, /sys/class/net
src/cli/                           linotch subcommands
qml/                               Notch.qml, IdlePill.qml, Panel.qml, tiles/*.qml
systemd/linotch.service.in
data/dev.linotch.Notch.desktop     autostart, env snapshot then unit start
packaging/debian/                  linotch, linotch-headless
tests/
```

Two seams only. `platform/` answers *which compositor world am I in*;
`Registry` plus each provider's `available()` answers *which services exist on this
machine*. Everything else is orthogonal, and a provider can be developed, tested and
removed without touching another one.

**Data flow is one-direction.** Services subscribe to D-Bus and `/proc` and publish Qt
properties. `NotchState` owns mode and timers and nothing else. QML reads properties and
calls four invokables: `toggle()`, `close()`, `mediaControl(action)`,
`applyToggle(key, value)`. No service holds a pointer to a QML object, and no QML file
talks to D-Bus. This is also what makes the headless build cheap: the same services plus
`NotchState` link with no `platform/` and no `qml/`.

**Error handling: absent, never broken.**

- A provider whose backend is missing removes its tile and reports through
  `linotch status`. No dialogs, ever.
- Failing to claim `org.freedesktop.Notifications` (held `DO_NOT_QUEUE` by something
  uncooperative, say) disables notifications and the rest of the notch still starts.
- No layer-shell global and no X11 strut support: exit early with a message naming the
  compositor and pointing at the GNOME roadmap section, rather than mapping a broken
  window.
- On X11, if another client already reserves the top strut, position anyway and warn in
  `linotch status`.
- A provider that dies mid-session (Pulse restarts, NetworkManager reloads) is
  re-discovered via the registry, and the tile reappears when the name returns.

## Notifications

Takeover plus supervised forwarding, made desktop-agnostic by *displace then restore what
was actually there*.

At startup, before claiming anything:

1. `GetConnectionUnixProcessID` for `org.freedesktop.Notifications`. If there is none,
   record that the slot was empty.
2. Resolve that PID's `/proc/<pid>/cmdline` and executable, and ask `systemctl --user`
   whether the PID belongs to a unit.
3. Record both in `~/.cache/linotch/displaced.json`, and subscribe to the current owner's
   unique name so a `Notify()` burst during startup is not dropped.
4. Claim `org.freedesktop.Notifications` with `REPLACE` semantics.

Each incoming `Notify()` renders in the notch and is forwarded to the displaced daemon's
unique name if it is still alive, so the desktop's own notification centre keeps working.
`CloseNotification`, `GetCapabilities`, actions and hints pass through in both directions.

Restoration runs on every exit path: a clean quit, `SIGTERM`, and an unclean death via the
user unit's `ExecStopPost=/usr/bin/linotch restore-notifications`. If a unit existed,
restart the unit; otherwise re-exec the recorded command line. `linotch
restore-notifications` is idempotent and refuses to run while LiNotch itself is alive, so
the two paths cannot fight.

Consequence worth stating plainly: while LiNotch runs, it *is* the notification server.
That is what the user asked for, and the systemd unit plus the record file are what make
it recoverable rather than a hostage situation.

## Shortcut

Three routes, tried in this order, and the third is always built.

1. `org.freedesktop.portal.GlobalShortcuts` — portable, no root, user confirms the binding
   in the DE dialog. Whether the COSMIC portal backend implements this interface is an
   open question.
2. A per-desktop writer, if the desktop exposes its shortcut table over D-Bus. On COSMIC,
   `com.system76.CosmicSettingsDaemon` is on the session bus with config namespaces of the
   form `com.system76.CosmicSettingsDaemon.Config.<schema>.V1`; whether that reaches the
   keybinding table is an open question.
3. Floor: D-Bus service `dev.linotch.Notch` plus `linotch toggle`, and a README instruction
   to point a custom shortcut at it. Works everywhere, needs no grab, and is how the panel
   gets tested in CI.

## Configuration, CLI, autostart

`~/.config/linotch/linotch.conf` via `QSettings`: pill and panel geometry, corner radius,
foreground, output name (multi-monitor), which sections are enabled, panel auto-close
delay, shortcut, and a master `notifications/takeover` switch that lets a `dunst` or `mako`
user keep their daemon and still use everything else.

`linotch` subcommands: `toggle`, `open`, `close`, `notify` (`-t` title, `-b` body,
`-i` icon), `status` (compositor, backend, per-provider availability, displaced daemon
record, RSS and measured CPU), `providers`, `config get|set`, `env-snapshot`,
`restore-notifications`, `daemon` (headless).

User services are not handed `WAYLAND_DISPLAY`, so the autostart entry runs
`linotch env-snapshot`, which writes `~/.config/linotch/wayland-env.conf`, and then
`systemctl --user start linotch`. The unit reads that file via `EnvironmentFile=`, and
carries `Restart=on-failure`, `ExecStopPost=` for notification restoration, and
`PartOf=graphical-session.target`. Autostart plus a real unit, rather than autostart alone,
is what buys crash-restart and stop hooks.

Debian packaging produces two binaries: `linotch` (Qt Quick UI) and `linotch-headless`
(CLI plus services, Qt Base and `qt6-base-private` D-Bus only, no QML, no GL) — which also
covers running the notch logic over SSH or on a session where the surface cannot map.

## Acceptance criteria

Provisional until Spike 0 measures what a Qt 6.4.2 QML surface actually costs on this
machine; they become CI-blocking at the end of Spike 0 and are not renegotiated after that.

- Idle on Wayland: RSS <= 60 MB, mean CPU <= 0.3 % over a 300 s window, no frame
  committed while nothing changes.
- Opening the panel may peak <= 110 MB; RSS returns within 5 MB of the idle measurement
  within 8 s of close.
- `linotch status` after a `kill -9` shows a working notification daemon. This is the test
  that the takeover is safe.
- Idle, X11: RSS <= 75 MB (no compositor effects to lean on, but no scene graph either).
- On a machine with no PipeWire, no NetworkManager and no Upower, the notch still starts,
  shows clock and notifications, and `status` lists exactly which tiles it withheld.
- `tests/perf/idle-footprint.sh` samples `/proc/<pid>/smaps_rollup` and CPU deltas over
  300 s and fails the build on regression.

## Testing

- QtTest unit tests per provider under `dbus-run-session`, against fake peers: an in-tree
  mock MPRIS player that registers `org.mpris.MediaPlayer2.fake` on the test bus, a mock
  notification sender issuing `Notify()` with hints and actions, and a mock Upower.
- Round-trip test for the risky one: a stand-in daemon claims the name, LiNotch starts,
  displaces and records it, forwards a notification, quits, and the stand-in is running
  again on the bus.
- Wayland integration in `cage` and `weston` headless, asserting layer, anchor,
  exclusive zone 0, and that opening does not steal focus from a second client.
- X11 integration on `Xvfb` with a window manager that honours `_NET_WM_STRUT_PARTIAL`,
  asserting the strut is actually requested.
- Footprint gate as above. Manual visual pass on COSMIC only, because that is the machine
  that exists here; the CI compositors cover the rest.

## Sub-specs and build order

Each gets its own spec, plan and implementation cycle.

| # | Deliverable | Why here in the order |
|---|---|---|
| 0 | Spike: layer-shell feasibility, footprint floor, and the three open questions | Existential. Invalidates 1-6 if it fails |
| 1 | Shell spine: `SurfaceBackend`, layer-shell and xcb backends, `NotchState`, both modes, clock | Everything hangs off this |
| 2 | Notifications: takeover, forward, record, restore, unit hooks | Riskiest behaviour, needs its own contract |
| 3 | Media: MPRIS watcher and transport | Independent |
| 4 | Meters: `/proc`, Upower battery, network | Independent |
| 5 | Control centre: Audio, Power backlight, Net, DND, shortcut | Largest backend surface |
| 6 | Config, CLI, autostart, `.deb` split | Cuts across all of the above |

Spike 0 and sub-spec 1 are designed together, here. Sub-spec 2 onward are specified after
the spine runs.

## Open questions

Each is answered by a measurement, not a debate. All three first probes are read-only.

1. Does Qt 6.4.2's Wayland plugin return a non-null `wl_surface` from
   `nativeResourceForWindow("surface")` after `create()` and before the first commit?
   Determines the whole binding route.
2. What is the RSS and CPU floor of an empty QWindow with a layer-shell surface on this
   machine? Determines whether the acceptance numbers are honest.
3. Does `cosmic-notifications` keep running after it loses `org.freedesktop.Notifications`?
   Decides whether supervised forwarding is real or sub-spec 2 needs redesigning.
   Deliberately disruptive to test; asked again before running.
4. Does the COSMIC portal backend implement `org.freedesktop.portal.GlobalShortcuts`?
5. Can `com.system76.CosmicSettingsDaemon` be used to register a keybinding?

## Archive note

`archive/python-prototype/` holds the original Python/GTK4 attempt (`main.py`,
`notch_window.py`, and a notes file named `.md`). It is kept for reference and does not
run: it imports the GTK4 and Adwaita typelibs, which are not installed, and
`notch_window.py` is truncated mid-statement at line 110. It also mixes API versions —
`GtkLayerShell` 0.1 is the GTK3 binding, not the GTK4 one. Superseded by this document.
