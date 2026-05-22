#!/bin/sh
set -eu

ROOT=${1:-}
REPO_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT_DIR="$ROOT/usr/local/bin"
OUT="$OUT_DIR/zero-greeter"
CC_BIN=${CC:-cc}

mkdir -p "$OUT_DIR"

if ! command -v "$CC_BIN" >/dev/null 2>&1; then
  echo "C compiler '$CC_BIN' not found. Install build-essential and libpam0g-dev, or set CC." >&2
  exit 1
fi

if [ ! -r /usr/include/security/pam_appl.h ]; then
  echo "PAM development headers not found. Install libpam0g-dev." >&2
  exit 1
fi

"$CC_BIN" -Wall -Wextra -O2 \
  -o "$OUT" \
  "$REPO_DIR/greeter/zero-greeter.c" \
  "$REPO_DIR/greeter/pam_auth.c" \
  -lpam

chmod 0755 "$OUT"
