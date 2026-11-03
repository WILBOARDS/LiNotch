# LiNotch feature board

Every function that exists in the two shipped Windows notch bars, with the Linux route for
each one, a feasibility verdict, and a cost. Use it to pick what LiNotch v1+ should do:
refer to rows by number.

Companion to [the design doc](superpowers/specs/2026-11-03-linotch-design.md), which already
settles the shell, the notification story and the packaging. This document only answers
*which features*.

## Where the two feature lists came from

| | L4-Windows-NotchBar | Nusa |
| --- | --- | --- |
| Identity | `Linux5Real/L4-Windows-NotchBar`, Tauri 2 + React 19 + TypeScript, v1.6.0 | `rama2555/nusa`, v1.1.0, landing page `rama2555.github.io/nusa` |
| Read how | full source (`/tmp/rea-targets/l4`, commit `6fba897`) | `NusaSetup.zip` from the v1.1.0 release asset |
| Artifact | n/a | `NusaSetup.exe`, SHA-256 `e1995b7f9b9ebe9f9d72b977da2ff6db59947e93ae3b58fca6435f6a0b0c8021`, PE32 GUI x86, 2 865 772 bytes |
| Installer | NSIS (`bundle.targets: ["nsis"]`) | Inno Setup — evidenced by `DVCLAL` and `PACKAGEINFO` `RT_RCDATA` resources plus `oleaut32` / `comctl32` / `advapi32` imports (a Delphi stub). At 2.8 MB it carries no bundled Electron runtime, so the "Electron" label on the landing page describes the app, not the installer. |
| License | **GPL-3.0** | **none declared** — all rights reserved |

Consequences for this project:

- L4's *ideas and behaviour* are fair game; its *code* is not, unless LiNotch accepts a
  GPL-3.0 license of its own.
- Nusa's code is not available to read at all. Its feature list here comes from its own
  release page and the installer artifact, not from its source.
- Both lists below are therefore written as specifications to implement against, not as
  things to port line by line.

## Verdict legend

- **CLEAN** — ports directly, frequently better than the Windows route.
- **MECHANISM** — the feature is possible, but through a different Wayland or Linux API.
- **HARD** — no honest equivalent on this stack today.
- **MOOT** — Windows-specific; nothing to port.
- Cost: **S** under a day, **M** 1-3 days, **L** more than 3 days (roughly its own sub-project).
- Character: **A** live-activity pill, **B** glance panel, **C** tool drawer.

## Shell — how the notch opens, closes and behaves

Shared by all three characters. This is the spine, not a feature selection.

| # | Function | What it does | Linux path | Verdict | Cost | Char. |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | Idle pill | 260x36 black pill flush to the top edge: HH:MM, hairline, media dot, unread dot, battery percent. | layer-shell TOP, top anchor, `exclusive_zone` 0, ARGB surface | CLEAN | M | A B C |
| 2 | Hover to open | Cursor enters a hit zone a little larger than the shape, then peek, then open. Delays fast 90 / normal 220 / patient 480 ms. | `wl_surface.set_input_region` plus pointer enter and leave — no global cursor poll | CLEAN | M | A B C |
| 3 | Click to open | Alternative mode: hover only nudges, click opens. | same surface, mouse button | CLEAN | S | A B C |
| 4 | Peek state | Intermediate 204x34 bump so the reveal has two beats. | QML state plus size animation | CLEAN | S | A B C |
| 5 | Pin open | Pushpin keeps it open on leave and on focus loss. | state flag, no focus grab needed | CLEAN | S | B C |
| 6 | Global shortcut | L4 uses Ctrl+Alt+Space; opens without peek and hands keyboard focus back on close. | GlobalShortcuts portal, then CosmicSettingsDaemon, then the CLI floor — unresolved open questions 4 and 5 | MECHANISM | M | A B C |
| 7 | Close paths | Esc, click outside, blur, or leaving the notch after a 180 ms grace. | layer-shell keyboard interactivity plus pointer leave; click-outside through the region | MECHANISM | M | B C |
| 8 | Focus mode | Notch goes fully click-through and dims to 50 percent opacity or a thin line. Triple-click, header button, tray, or settings. | empty input region equals native passthrough | CLEAN | S | A B |
| 9 | Thin-line idle look | 150x5 everyday strip instead of the pill; hover grows it, and only an expired timer pops the real notch. | same surface, different rect | CLEAN | S | A |
| 10 | Drag along the edge | Drag the closed notch, or the header while open; snaps back to centre within 24 px and clamps to the monitor. | `set_margin` / `set_anchor` updates on a live layer surface | MECHANISM | M | A B C |
| 11 | Monitor picker | Choose which monitor carries the notch; the picker only appears with more than one. | `wl_output` plus the layer-surface output binding | CLEAN | M | A B C |
| 12 | Hide on fullscreen | L4 polls every 400 ms and compares the foreground window rect to the monitor rect; maximised does not count, borderless does. | `cosmic_toplevel_manager_v1` window states — signal driven, no poll at all | MECHANISM | M | A B C |
| 13 | Tray / status item | Left click opens; menu is Open, Settings, Focus mode, autostart, Quit. | StatusNotifierItem over D-Bus with DBusMenu | CLEAN | M | A B C |
| 14 | Live-activity priority | Collapsed pill shows exactly one thing, priority gaming then timer then call then music; reopening after 60 s jumps to that tool. | same rule, minus gaming unless FPS lands | CLEAN | M | A |
| 15 | Privacy dots | 6 px dots: green for mic or camera in use, red for screen recording. | PipeWire client nodes for audio, ScreenCast portal sessions, camera via v4l2 busy state | HARD | M | A |
| 16 | Keys 1 to 9 for tools | While open, a digit switches to the Nth visible tool. | Qt key event on our own surface | CLEAN | S | C |
| 17 | Resume last tool | Reopening returns where you were; a long absence jumps to the live activity. | QSettings | CLEAN | S | C |

Two things L4 does that LiNotch should *not* copy: it emulates click-through with a
16 ms global-cursor poll, because Win32 only offers whole-window passthrough, and it never
resizes its 760x560 window so the springs stay smooth. On Wayland the input region makes
both moot — declaratively shaping the region gives partial passthrough and hover detection
in one move.

## Media and time

Both Windows apps lead with these.

| # | Function | What it does | Linux path | Verdict | Cost | Char. |
| --- | --- | --- | --- | --- | --- | --- |
| 18 | Now Playing | Cover, title, artist, seekable progress, prev / play-pause / next; click the cover to raise the player. | MPRIS 2 over the session bus — signal driven, richer than GSMTC polled at 400 ms | CLEAN | M | A B C |
| 19 | Cover-art accent colour | Accent derived from the thumbnail, crossfaded over 500 ms, and it drives the equaliser colour. | QImage sampling plus a QML colour animation | CLEAN | S | A B C |
| 20 | 4-band equaliser | Live bars from the actual audio, only running while visible, 40 to 60 fps. | PipeWire monitor stream plus an FFT — Qt has no built-in capture path | MECHANISM | L | A |
| 21 | Per-app volume mixer | One row per application, up to 5, and the notch grows 34 px per row. | PipeWire nodes via `wpctl` or WPKit — simpler than WASAPI session enumeration | CLEAN | M | B C |
| 22 | Synced lyrics | Nusa's headline feature: a karaoke line while playing, for Spotify, Apple Music and YouTube. | no OS API exists — LRCLIB or `.lrc` files matched on artist, title and duration | MECHANISM | M | A |
| 23 | Master volume and mute | Slider, wheel plus or minus 2 percent, arrows plus or minus 5, click the icon to mute, and external changes appear within a second. | PipeWire / PulseAudio D-Bus signals, so no polling | CLEAN | S | B |
| 24 | Focus timer / Pomodoro | Focus 15/25/45, break 5/10/15, wheel over the big clock to nudge by a minute (Shift by five), countdown in the closed pill, survives restart. | app state plus QSettings | CLEAN | M | A B C |
| 25 | Alarm on expiry | 6 sounds, its own volume, ring duration 1x / 5 / 15 / 30 s or until stopped (120 s cap), and the expired state persists until dismissed. | freedesktop sound theme, or a GStreamer sink | CLEAN | S | A B |
| 26 | Water tracker | Nusa only: periodic drink nudges with progress shown in the pill. | timer plus state | CLEAN | S | A |

## Files and clipboard

The work tools, and the most Wayland-shaped part of the list.

| # | Function | What it does | Linux path | Verdict | Cost | Char. |
| --- | --- | --- | --- | --- | --- | --- |
| 27 | Clipboard history | Text, links, images and file lists; 40 items with 8 images, filter control, click to restore, nothing written to disk, password managers excluded. | `zwp_data_control_device_v1` — cosmic-comp advertises it, so it needs a second vendored protocol. No Linux equivalent of the Windows password-manager exclusion formats. | MECHANISM | L | C |
| 28 | Shelf (file parking) | Drop files on the notch or press Ctrl+V, up to 30 reference entries; open, copy back as files, reveal, thumbnails. | Wayland drag and drop (`text/uri-list`) into a layer surface plus a Qt drop handler | MECHANISM | L | C |
| 29 | Screenshot paste | A clipboard image pasted in is written as a PNG under `Pictures`. | portal Screenshot API, or `grim` / `cosmic-screenshot` | MECHANISM | M | C |
| 30 | Drop-zone growth | The hit zone grows 48 px only during a real drag that started elsewhere, so drops land without swallowing clicks on browser tabs. | input-region swap while a drag is in flight | MECHANISM | M | C |
| 31 | File converter | Image, audio, video, document and spreadsheet targets, quality slider, max image size, per-file progress, reveal result. | `ffmpeg` and ImageMagick through QProcess; HTML to PDF natively with Qt Print instead of L4's headless-Edge trick | CLEAN | L | C |
| 32 | Unit converter | Length, mass, temperature, volume, speed. | pure QML | CLEAN | S | C |

## Glance content

What the panel and the tabs show.

| # | Function | What it does | Linux path | Verdict | Cost | Char. |
| --- | --- | --- | --- | --- | --- | --- |
| 33 | Overview tab | Clock, date, weather chip, one "what is running" card, DND tile, master volume. | composes rows already chosen above | CLEAN | S | B C |
| 34 | Hardware meters | CPU, RAM, GPU load, VRAM and ping, each with a 60 s scrolling sparkline, traffic-light colouring above 70 and 90 percent. | `/proc/stat`, `/proc/meminfo`, `gpu_busy_percent` on amdgpu or NVML, `/sys/class/net` | CLEAN | M | B C |
| 35 | Network latency | ICMP ping to 1.1.1.1 every 2 s, paused when nobody is watching. | raw ICMP needs `CAP_NET_RAW`, or use TCP connect timing. Not worth a setuid binary. | MECHANISM | S | B C |
| 36 | Battery and backlight | Percentage and charging state in the pill, brightness tile in the panel. Nusa shows battery; L4 has no battery indicator at all. | Upower signals plus `/sys/class/backlight` | CLEAN | S | A B |
| 37 | Weather | Open-Meteo current plus 7-day, IP auto-location with a second fallback service, city search, Celsius or Fahrenheit. | unchanged — it is plain HTTPS from the UI | CLEAN | M | B C |
| 38 | Notes and to-dos | One autosaving note with a word count; a local task list with stars and an "n open" counter. | local JSON in app data | CLEAN | M | C |
| 39 | Quick toggles | Wi-Fi, Bluetooth, DND, brightness, theme. L4 only ships volume and DND; the design doc already promises more. | NetworkManager, BlueZ, Upower, the color-scheme portal | CLEAN | L | B |

## Connected services

The heavier tabs.

| # | Function | What it does | Linux path | Verdict | Cost | Char. |
| --- | --- | --- | --- | --- | --- | --- |
| 40 | AI chat ("Ask") | Streaming chat against OpenRouter, OpenAI, Anthropic or a custom base URL, reasoning effort, stop, copy reply, plus Summarise / Rewrite / Translate against clipboard content. | unchanged: HTTPS plus SSE. Keep the key in the C++ layer behind libsecret, never in QML. | CLEAN | L | C |
| 41 | AI usage meters | Remaining limit windows for Claude Code, Codex, Gemini CLI and Cursor, with reset countdowns and a 60 s poll. | unusually portable — L4 already reads `~/.claude`, `~/.codex` and `~/.gemini`; only Cursor shifts to `~/.config/Cursor` | MECHANISM | M | C |
| 42 | Discord call | Channel, guild, member avatars with speaking rings, mute / deafen / hang up, and a live indicator in the pill. | same RPC over `$XDG_RUNTIME_DIR/discord-ipc-N` unix sockets instead of Windows named pipes | CLEAN | M | A C |
| 43 | Vault (passwords and TOTP) | List, search, generator, reveal with 15 s auto-hide, TOTP codes with countdown rings, add by pasting a QR screenshot, 4-digit PIN gate with lockout doubling from 30 s to 15 min. | TOTP and AES-GCM port as-is; DPAPI and Windows Hello become libsecret and the Secret Service, so the OS-unlock tier is different | MECHANISM | L | C |

## Skipped, honestly

| Function | Why not |
| --- | --- |
| FPS and gaming HUD | L4 counts presents in a real-time ETW session over DXGI, D3D9 and DxgKrnl, and elevates to unlock it. Wayland has no client-side present counter; it needs a MangoHud-style implicit layer or Mesa hooks. Out of scope for a lightweight notch. |
| Screen-recording half of the privacy dots | Detectable through the ScreenCast portal on some desktops, not all; do it manually instead of guessing. |
| Presentation mode, automatic | Depends on the recording detection above. Keep the manual eye toggle if you want the redaction of money, thumbnails and vault rows. |
| Trading 212 portfolio | Portable HTTPS, but it is one person's broker. Say the word and it is a normal tab. |
| WebView memory target level | L4 drops WebView2 to Low 20 s after close (300 MB to 80 MB). The Qt scene graph plus destroying the panel on close already gets the same result. |
| Dark tray menu via uxtheme ordinals | Windows-only by definition. |

## Preset selections

If you want a starting point rather than 43 individual answers:

- **A - live-activity pill**: 1-14, 16-19, 22, 24, 25, 26, 36, 42. Smallest footprint, no
  panel UI at all. Rows 20 and 22 are the expensive and least certain ones.
- **B - glance panel** (this is the current design doc, roughly as written): spine 1-13, plus
  18, 21, 23, 24, 33, 34, 35, 36, 37, 39.
- **C - tool drawer**: spine plus a tool registry (rows 16, 17) and whichever of 27, 28, 29,
  30, 31, 32, 38, 40, 41, 42, 43 you pick. C needs one seam the design doc does not have
  yet: a registry with per-tab geometry and enable ordering, so adding a tab never touches
  the shell.

Seven rows are L-sized (20, 27, 28, 31, 39, 40, 43) and each is roughly its own sub-project.
L4 spent an entire 1.6.0 release only making the idle state cheap, which is the honest
measure of how much the drawer ambition fights the lightness goal.

## Environment facts these verdicts rest on

Observed on the development machine (ASUS Vivobook M7400QE, Pop!_OS 24.04 LTS, COSMIC on
Wayland, `cosmic-comp` 1.0.0 at `8cb1802`) on 2026-11-03:

- Advertised globals that matter here: `zwlr_layer_shell_v1` including
  `set_anchor` / `set_exclusive_zone` / `set_keyboard_interactivity`,
  `cosmic_corner_radius_layer_v1` and `cosmic_corner_radius_manager_v1`,
  `cosmic_toplevel_manager_v1`, `cosmic_overlap_notify_v1`,
  `zwlr_data_control_manager_v1`, `zwp_pointer_constraints_v1`, `zwp_idle_inhibitor_v1`,
  `ext_session_lock_manager_v1`, `xdg_output_manager_v1`, `wp_fractional_scale_manager_v1`,
  `wp_viewporter`, `wp_cursor_shape_manager_v1`, `xdg_wm_base`.
- Not found in the compositor binary: any global-shortcut protocol, and
  `ext_transient_pointer_v1`.
- Toolchain present: `qt6-base-dev` 6.4.2, `qt6-wayland`, CMake 3.28.3, Ninja,
  `wayland-scanner` 1.23.1. Node v26.13 is on PATH; `cargo` and `rustc` are not.
- Archive candidates not yet installed: `qt6-declarative-dev`, `qml6-module-qtquick`,
  `libwayland-dev`, `libgtk-layer-shell0`.
- `liblayershellqtinterface` in this archive depends on `qtbase5-dev`, so there is no Qt 6
  layer-shell-qt to install. That is why the binding is hand-vendored.
