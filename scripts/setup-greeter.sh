#!/bin/sh
set -eu

ROOT=${1:-}
REPO_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT_DIR="$ROOT/usr/local/bin"
OUT="$OUT_DIR/zero-greeter-wayland"
HELPER_DIR="$ROOT/usr/local/libexec/cardputer-zero"
HELPER_OUT="$HELPER_DIR/zero-greeter-auth"
BUILD_DIR="${TMPDIR:-/tmp}/cardputer-zero-greeter-build"
CC_BIN=${CC:-cc}
CXX_BIN=${CXX:-c++}

mkdir -p "$OUT_DIR"
mkdir -p "$HELPER_DIR"
mkdir -p "$BUILD_DIR"

if ! command -v "$CC_BIN" >/dev/null 2>&1; then
  echo "C compiler '$CC_BIN' not found. Install build-essential, or set CC." >&2
  exit 1
fi

if ! command -v "$CXX_BIN" >/dev/null 2>&1; then
  echo "C++ compiler '$CXX_BIN' not found. Install build-essential, or set CXX." >&2
  exit 1
fi

if ! command -v pkg-config >/dev/null 2>&1; then
  echo "pkg-config not found. Install pkg-config, libwayland-dev, and libxkbcommon-dev." >&2
  exit 1
fi

if ! command -v wayland-scanner >/dev/null 2>&1; then
  echo "wayland-scanner not found. Install wayland-protocols and libwayland-dev." >&2
  exit 1
fi

if ! pkg-config --exists wayland-client xkbcommon; then
  echo "Wayland greeter dependencies not found. Install libwayland-dev and libxkbcommon-dev." >&2
  exit 1
fi

XDG_SHELL_XML=${XDG_SHELL_XML:-}
if [ -z "$XDG_SHELL_XML" ]; then
  for candidate in \
    /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml \
    /usr/share/wayland/xdg-shell.xml; do
    if [ -r "$candidate" ]; then
      XDG_SHELL_XML=$candidate
      break
    fi
  done
fi

if [ -z "$XDG_SHELL_XML" ] || [ ! -r "$XDG_SHELL_XML" ]; then
  echo "xdg-shell.xml not found. Install wayland-protocols." >&2
  exit 1
fi

wayland-scanner client-header "$XDG_SHELL_XML" "$BUILD_DIR/xdg-shell-client-protocol.h"
wayland-scanner private-code "$XDG_SHELL_XML" "$BUILD_DIR/xdg-shell-protocol.c"
"$CC_BIN" -c "$BUILD_DIR/xdg-shell-protocol.c" \
  $(pkg-config --cflags wayland-client) \
  -o "$BUILD_DIR/xdg-shell-protocol.o"

"$CXX_BIN" -Wall -Wextra -O2 -std=c++17 \
  -I"$BUILD_DIR" \
  $(pkg-config --cflags wayland-client xkbcommon) \
  -o "$OUT" \
  "$REPO_DIR/greeter/zero-greeter-wayland.cpp" \
  "$BUILD_DIR/xdg-shell-protocol.o" \
  $(pkg-config --libs wayland-client xkbcommon)

chmod 0755 "$OUT"

if [ ! -r /usr/include/security/pam_appl.h ]; then
  echo "PAM headers not found. Install libpam0g-dev." >&2
  exit 1
fi

"$CXX_BIN" -Wall -Wextra -O2 -std=c++17 \
  -o "$HELPER_OUT" \
  "$REPO_DIR/greeter/zero-greeter-auth.cpp" \
  -lpam

if [ -z "$ROOT" ] && getent group _greetd >/dev/null 2>&1; then
  chown root:_greetd "$HELPER_OUT"
  chmod 4750 "$HELPER_OUT"
else
  chmod 4755 "$HELPER_OUT"
fi
