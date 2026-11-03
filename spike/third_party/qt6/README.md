# Vendored Qt private header

`qpa/qplatformnativeinterface.h` is a verbatim copy of Qt 6.4.2's QPA header.

- Source package: `qt6-base-private-dev` 6.4.2+dfsg-21.1build5
  (Ubuntu noble / Pop!_OS 24.04 archive)
- Installed path on a machine that has it:
  `/usr/include/x86_64-linux-gnu/qt6/QtGui/6.4.2/QtGui/qpa/qplatformnativeinterface.h`
- SHA-256 of the vendored copy:
  `6a1ac4a6b0cdc99e7082b25774f5971ef69f815f6f4eb1437e1e17a33efcb103`
- License: LGPL-3.0-only / GPL-2.0-only / commercial, per the header SPDX line.

Why it is vendored: this machine does not have `qt6-base-private-dev` installed,
and the task forbids package installs. The header is only needed for the
`QPlatformNativeInterface` class declaration; `QGuiApplication::platformNativeInterface()`
itself is a public, exported QtGui symbol. The real LiNotch build should add
`qt6-base-private-dev` as a build dependency instead of vendoring this header.

This is explicitly QPA private API: source/binary compatibility is not guaranteed
across Qt minor versions.
