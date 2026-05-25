#!/bin/sh
set -eu

ROOT=${1:-}
REPO_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT_DIR="$ROOT/usr/local/bin"
OUT="$OUT_DIR/zero-window-agent"
BUILD_DIR="${TMPDIR:-/tmp}/cardputer-zero-window-agent-build"
CC_BIN=${CC:-cc}
CXX_BIN=${CXX:-c++}
PROTOCOL_XML="$REPO_DIR/protocols/wlr-foreign-toplevel-management-unstable-v1.xml"

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
  echo "pkg-config not found. Install pkg-config and libwayland-dev." >&2
  exit 1
fi

if ! command -v wayland-scanner >/dev/null 2>&1; then
  echo "wayland-scanner not found. Install wayland-protocols and libwayland-dev." >&2
  exit 1
fi

if ! pkg-config --exists wayland-client; then
  echo "Wayland client dependencies not found. Install libwayland-dev." >&2
  exit 1
fi

if [ ! -r "$PROTOCOL_XML" ]; then
  echo "Missing vendored protocol: $PROTOCOL_XML" >&2
  exit 1
fi

wayland-scanner client-header "$PROTOCOL_XML" "$BUILD_DIR/wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"
wayland-scanner private-code "$PROTOCOL_XML" "$BUILD_DIR/wlr-foreign-toplevel-management-unstable-v1-protocol.c"

"$CC_BIN" -c "$BUILD_DIR/wlr-foreign-toplevel-management-unstable-v1-protocol.c" \
  $(pkg-config --cflags wayland-client) \
  -o "$BUILD_DIR/wlr-foreign-toplevel-management-unstable-v1-protocol.o"

"$CXX_BIN" -Wall -Wextra -O2 -std=c++17 \
  -I"$BUILD_DIR" \
  $(pkg-config --cflags wayland-client) \
  -o "$OUT" \
  "$REPO_DIR/window-agent/zero-window-agent.cpp" \
  "$BUILD_DIR/wlr-foreign-toplevel-management-unstable-v1-protocol.o" \
  $(pkg-config --libs wayland-client)

chmod 0755 "$OUT"
