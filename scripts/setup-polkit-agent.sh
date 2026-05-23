#!/bin/sh
set -eu

ROOT=${1:-}
REPO_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT_DIR="$ROOT/usr/local/bin"
OUT="$OUT_DIR/zero-polkit-agent"
PROMPT_OUT="$OUT_DIR/zero-polkit-prompt-wayland"
BUILD_DIR="${TMPDIR:-/tmp}/cardputer-zero-polkit-agent-build"
CC_BIN=${CC:-cc}
CXX_BIN=${CXX:-c++}

mkdir -p "$OUT_DIR"
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
  echo "pkg-config not found. Install pkg-config and libpolkit-agent-1-dev." >&2
  exit 1
fi

if ! command -v wayland-scanner >/dev/null 2>&1; then
  echo "wayland-scanner not found. Install wayland-protocols and libwayland-dev." >&2
  exit 1
fi

if ! pkg-config --exists polkit-agent-1 polkit-gobject-1 gio-2.0 glib-2.0; then
  echo "polkit development headers not found. Install libpolkit-agent-1-dev and libglib2.0-dev." >&2
  exit 1
fi

if ! pkg-config --exists wayland-client xkbcommon; then
  echo "Wayland prompt dependencies not found. Install libwayland-dev and libxkbcommon-dev." >&2
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
  -o "$PROMPT_OUT" \
  "$REPO_DIR/polkit-agent/zero-polkit-prompt-wayland.cpp" \
  "$BUILD_DIR/xdg-shell-protocol.o" \
  $(pkg-config --libs wayland-client xkbcommon)

"$CC_BIN" -Wall -Wextra -O2 \
  $(pkg-config --cflags polkit-agent-1 polkit-gobject-1 gio-2.0 glib-2.0) \
  -o "$OUT" \
  "$REPO_DIR/polkit-agent/zero-polkit-agent.c" \
  $(pkg-config --libs polkit-agent-1 polkit-gobject-1 gio-2.0 glib-2.0)

chmod 0755 "$OUT"
chmod 0755 "$PROMPT_OUT"
