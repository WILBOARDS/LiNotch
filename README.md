# LiNotch

A notch for Linux — a black pill anchored to the top edge of the screen that expands into
a panel with media controls, notifications, system meters and quick toggles. Built to be
left running: the steady state is a single small surface that does not repaint, does not
poll, and does not reserve screen space.

**Status: design phase.** The architecture is agreed and written down, and no
implementation exists yet. Read
[docs/superpowers/specs/2026-11-03-linotch-design.md](docs/superpowers/specs/2026-11-03-linotch-design.md).
The first thing to happen is a throwaway spike that proves the Qt 6.4 Wayland surface path
works before any feature is written.

## Why it does not already exist

`zwlr-layer-shell_v1` is the only Wayland protocol that gives a client a top-anchored,
above-all-windows, no-focus-steal, no-exclusive-zone surface. wlroots compositors and KWin
implement it, so a notch is possible there. `mutter` (GNOME) does not implement it at all,
which is why a notch on GNOME has to be a Shell extension rather than an app — LiNotch
cannot be one, and does not pretend otherwise.

## Compatibility

| Session | Supported in v1 | How |
|---|---|---|
| COSMIC, Sway, Hyprland, labwc, Wayfire, river | yes | `zwlr_layer_shell_v1` |
| KDE Plasma on Wayland | yes | KWin implements layer-shell |
| Any DE on X11 | yes | `_NET_WM_WINDOW_TYPE_DOCK` + `_NET_WM_STRUT_PARTIAL` via libxcb |
| GNOME on Wayland or X11 | no | needs a Shell extension; planned later, shares the same D-Bus service |

## Two modes, one surface

- **Idle** — 260 x 36 px, flush to the top edge, solid black, square top corners and ~18 px
  radius on the bottom two. Shows `HH:MM`, a media dot when something is playing, an unread
  dot when notifications are waiting, and battery. One timer, aligned to the minute.
- **Opened** — 420 x 340 px, grown downward with a ~180 ms animation. Now-playing with
  transport controls, battery/CPU/RAM/network meters, a control grid (volume, brightness,
  do-not-disturb, Wi-Fi, Bluetooth) and unread notifications. Created on first open and
  destroyed 8 s after the last close, so opening a control centre does not raise the cost of
  leaving the notch running.

Toggle with a global shortcut, default `Alt+Super+N`, configurable. `Esc` closes.
Notifications light a dot; they never move the notch on their own.

Modifier-only chords such as plain `Alt+Super` cannot be expressed: both the freedesktop
portal and per-desktop shortcut tables need a real key alongside the modifiers.

## Feature backends are capability-gated

Every feature asks its backend whether it exists and disappears if it does not: MPRIS over
D-Bus for media, Upower for battery and backlight, PipeWire/Pulse for volume,
NetworkManager or iwd for network, `/proc` for CPU and RAM. A machine missing any of them
gets a smaller control grid, not an error dialog.

## Notifications, and the thing to know

To show your desktop's notifications, LiNotch has to *become* the notification server,
because the freedesktop protocol offers no way to watch an existing daemon passively. On
startup it records whatever currently owns `org.freedesktop.Notifications` — PID, command
line, and whether a systemd user unit owns that PID — claims the name, forwards each
notification to the displaced daemon so your desktop's own notification centre keeps
working, and restores the original on every exit path: clean quit, `SIGTERM`, and `kill -9`
via `ExecStopPost=` on the user unit. A `notifications/takeover` switch turns the behaviour
off entirely for anyone who prefers their existing `dunst` or `mako`.

## Development machine

Designed and verified against:

- ASUS Vivobook M7400QE
- Pop!_OS 24.04 LTS, COSMIC on Wayland, `cosmic-comp`
- AMD Cezanne [Radeon Vega Series] iGPU plus NVIDIA GA107M [GeForce RTX 3050 Ti Mobile]
  with the `nvidia` driver
- Qt 6.4.2, GCC 13.3.0, CMake 3.28.3, Ninja 1.11.1, wayland-scanner 1.23.1

## Setting up the build environment

These are the packages for the Qt 6 / QML route, checked against the Pop!_OS 24.04 archive
on the machine above on 2026-11-03.

1. Already installed and working here: `qt6-base-dev`, `qt6-wayland`, `cmake`,
   `ninja-build`, `wayland-scanner`.
2. Have archive candidates but are not installed yet: `qt6-declarative-dev`,
   `qml6-module-qtquick`.
3. `libwayland-dev` — needed for the layer-shell client code, **not yet confirmed available
   in your configured apt components** (the check was interrupted). Confirm it before the
   spike.

```bash
sudo apt install qt6-declarative-dev qml6-module-qtquick libwayland-dev
```

Note what is *not* an option here: `liblayershellqtinterface` in the 24.04 archive depends
on `qtbase5-dev`, so there is no Qt6 layer-shell-qt to install. That is why the binding is
hand-vendored in this project.

## Building

Not yet possible — there is no `CMakeLists.txt` until sub-spec 1 starts. The spike lives in
`spike/` and is explicitly throwaway.

## Command line

Planned as the guaranteed floor for every feature, so that the notch is scriptable and
testable without a display:

```
linotch toggle | open | close
linotch notify -t "Title" -b "Body" -i icon-name
linotch status              # compositor, backend, providers, displaced daemon, RSS, CPU
linotch config get|set ...
linotch restore-notifications
```

## Layout of this repository

- `docs/superpowers/specs/` — the design document
- `docs/feature-board.md` — every feature in the two Windows notch bars, with its Linux
  route and cost, used to decide what LiNotch builds
- `archive/python-prototype/` — the original Python/GTK4 attempt. Kept for reference; it
  does not run (it imports GTK4 and Adwaita typelibs that are not installed, one file is
  truncated mid-statement, and it mixes the GTK3 `GtkLayerShell` binding with GTK4)

## License

MIT. The reference projects are read for ideas only: L4-Windows-NotchBar is
GPL-3.0 and Nusa declares no license, so no code from either is copied, and
nothing here is derived from their sources.
