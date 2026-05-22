#!/bin/sh
set -eu

ROOT=${1:-}
REPO_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT_DIR="$ROOT/usr/local/bin"
OUT="$OUT_DIR/zero-polkit-agent"
CC_BIN=${CC:-cc}

mkdir -p "$OUT_DIR"

if ! command -v "$CC_BIN" >/dev/null 2>&1; then
  echo "C compiler '$CC_BIN' not found. Install build-essential, or set CC." >&2
  exit 1
fi

if ! command -v pkg-config >/dev/null 2>&1; then
  echo "pkg-config not found. Install pkg-config and libpolkit-agent-1-dev." >&2
  exit 1
fi

if ! pkg-config --exists polkit-agent-1 polkit-gobject-1 gio-2.0 glib-2.0; then
  echo "polkit development headers not found. Install libpolkit-agent-1-dev and libglib2.0-dev." >&2
  exit 1
fi

"$CC_BIN" -Wall -Wextra -O2 \
  $(pkg-config --cflags polkit-agent-1 polkit-gobject-1 gio-2.0 glib-2.0) \
  -o "$OUT" \
  "$REPO_DIR/polkit-agent/zero-polkit-agent.c" \
  $(pkg-config --libs polkit-agent-1 polkit-gobject-1 gio-2.0 glib-2.0)

chmod 0755 "$OUT"
