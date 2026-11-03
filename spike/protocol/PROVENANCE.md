# Vendored protocol provenance

`wlr-layer-shell-unstable-v1.xml` is vendored byte-for-byte.

- Upstream project: wlroots / wlr-protocols
- Upstream path: `unstable/wlr-layer-shell-unstable-v1.xml`
- Pinned commit: `1106a4a1c57562ddd438c547da4c0d6dc150d624`
  (`refs/heads/master` as fetched on 2026-11-03)
- Source URL:
  https://gitlab.freedesktop.org/wlroots/wlr-protocols/-/raw/1106a4a1c57562ddd438c547da4c0d6dc150d624/unstable/wlr-layer-shell-unstable-v1.xml
- SHA-256 of the vendored file:
  `87e0b9c837aecd6977f76f3c47d73088b7159871f5d979dc1840f6cadb5e2ed8`
- Interfaces and versions in this copy:
  `zwlr_layer_shell_v1` v5, `zwlr_layer_surface_v1` v5
- License: the MIT-style permission notice embedded in the XML copyright block.

The file is kept pristine (no edits) so the checksum above is reproducible.
Client code is generated at build time with the system `wayland-scanner`
(1.23.1 on the target machine), never committed.

## xdg-shell.xml (transitive link dependency)

`wlr-layer-shell-unstable-v1.xml` has an optional `get_popup` request whose
argument is an `xdg_popup`; the generated client code therefore references
`xdg_popup_interface`. The xdg-shell protocol is vendored for that symbol.

- Upstream project: wayland / wayland-protocols
- Upstream path: `stable/xdg-shell/xdg-shell.xml`
- Pinned commit: `3eab2dccf4bfdd85c16d312a6c8c8fbc2a6e73a8`
  (`refs/heads/main` as fetched on 2026-11-03)
- Source URL:
  https://gitlab.freedesktop.org/wayland/wayland-protocols/-/raw/3eab2dccf4bfdd85c16d312a6c8c8fbc2a6e73a8/stable/xdg-shell/xdg-shell.xml
- SHA-256 of the vendored file:
  `8cd3980d0783e1f55d12901ac51533204af71487cc4dc69ee00bb7096647d9de`
- Interfaces: `xdg_wm_base` v7, `xdg_positioner` v7, `xdg_surface` v7,
  `xdg_toplevel` v7, `xdg_popup` v7
- License: MIT (the XML copyright block).

