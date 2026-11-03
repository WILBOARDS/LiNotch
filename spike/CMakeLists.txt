cmake_minimum_required(VERSION 3.16)
project(linotch_layer_shell_spike LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Gui)
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)

find_program(WAYLAND_SCANNER_EXECUTABLE NAMES wayland-scanner REQUIRED)

set(PROTOCOL_XML "${CMAKE_CURRENT_SOURCE_DIR}/protocol/wlr-layer-shell-unstable-v1.xml")
set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/protocol/generated")
set(PROTOCOL_HEADER "${GENERATED_DIR}/wlr-layer-shell-unstable-v1-client-protocol.h")
set(PROTOCOL_SOURCE "${GENERATED_DIR}/wlr-layer-shell-unstable-v1-protocol.c")

set(XDG_PROTOCOL_XML "${CMAKE_CURRENT_SOURCE_DIR}/protocol/xdg-shell.xml")
set(XDG_PROTOCOL_HEADER "${GENERATED_DIR}/xdg-shell-client-protocol.h")
set(XDG_PROTOCOL_SOURCE "${GENERATED_DIR}/xdg-shell-protocol.c")

add_custom_command(
    OUTPUT "${PROTOCOL_HEADER}" "${PROTOCOL_SOURCE}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${GENERATED_DIR}"
    COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" client-header "${PROTOCOL_XML}" "${PROTOCOL_HEADER}"
    COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" private-code "${PROTOCOL_XML}" "${PROTOCOL_SOURCE}"
    # The upstream XML names the get_layer_surface argument "namespace", which is a
    # C++ keyword. The generated header is C; rename the parameter in the build
    # artifact only (ABI is unaffected). The vendored XML stays pristine.
    COMMAND sed -i -E "s/\\bnamespace\\b/namespace_/g" "${PROTOCOL_HEADER}"
    DEPENDS "${PROTOCOL_XML}"
    COMMENT "Generating wlr-layer-shell-unstable-v1 client protocol"
    VERBATIM)

# wlr-layer-shell's optional get_popup request references xdg_popup_interface,
# so the generated code needs the xdg-shell interface symbols at link time.
add_custom_command(
    OUTPUT "${XDG_PROTOCOL_HEADER}" "${XDG_PROTOCOL_SOURCE}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${GENERATED_DIR}"
    COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" client-header "${XDG_PROTOCOL_XML}" "${XDG_PROTOCOL_HEADER}"
    COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" private-code "${XDG_PROTOCOL_XML}" "${XDG_PROTOCOL_SOURCE}"
    DEPENDS "${XDG_PROTOCOL_XML}"
    COMMENT "Generating xdg-shell client protocol"
    VERBATIM)

add_executable(linotch-layer-shell-spike
    src/main.cpp
    "${PROTOCOL_SOURCE}"
    "${PROTOCOL_HEADER}"
    "${XDG_PROTOCOL_SOURCE}"
    "${XDG_PROTOCOL_HEADER}")

target_compile_definitions(linotch-layer-shell-spike PRIVATE _GNU_SOURCE)
target_compile_options(linotch-layer-shell-spike PRIVATE -Wall -Wextra)

# third_party/qt6 carries a verbatim copy of Qt 6.4.2's
# qpa/qplatformnativeinterface.h, needed for the documented
# QPlatformNativeInterface route. See third_party/qt6/README.md.
target_include_directories(linotch-layer-shell-spike PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/qt6"
    "${GENERATED_DIR}")

target_link_libraries(linotch-layer-shell-spike PRIVATE
    Qt6::Gui
    PkgConfig::WAYLAND_CLIENT)
