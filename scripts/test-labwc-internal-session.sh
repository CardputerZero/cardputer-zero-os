#!/bin/sh
set -eu

# One-shot Wayland/labwc session smoke test.
#
# Run as the authenticated Zero user, not through sudo:
#
#   sh ./scripts/test-labwc-internal-session.sh
#
# The test starts labwc on the internal DRM device with /bin/true as the session
# command, so labwc should exit immediately after proving whether it can open
# the output/session. It does not change /etc/cardputer-zero/session.conf.

LOG=${1:-/tmp/cardputer-zero-labwc-smoke.log}
WRAPPER=${CARDPUTER_ZERO_LABWC_SESSION:-/usr/local/bin/cardputer-zero-labwc-session}
CONFIG_DIR=${CARDPUTER_ZERO_LABWC_CONFIG_DIR:-/etc/xdg/cardputer-zero-labwc}

if [ "$(id -u)" -eq 0 ]; then
  echo "Do not run this test as root. Run it as the logged-in Zero user." >&2
  exit 1
fi

if [ ! -x "$WRAPPER" ]; then
  echo "Missing labwc session wrapper: $WRAPPER" >&2
  exit 1
fi

if [ ! -d "$CONFIG_DIR" ]; then
  echo "Missing labwc config dir: $CONFIG_DIR" >&2
  exit 1
fi

if [ -z "${XDG_RUNTIME_DIR:-}" ]; then
  export XDG_RUNTIME_DIR="/run/user/$(id -u)"
fi

if [ ! -d "$XDG_RUNTIME_DIR" ]; then
  echo "Missing XDG_RUNTIME_DIR: $XDG_RUNTIME_DIR" >&2
  exit 1
fi

echo "Writing labwc smoke log to $LOG"
{
  echo "=== user ==="
  id
  echo
  echo "=== runtime ==="
  echo "XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR"
  echo "XDG_SESSION_ID=${XDG_SESSION_ID:-}"
  echo "XDG_SEAT=${XDG_SEAT:-}"
  echo
  echo "=== devices ==="
  ls -l /dev/dri/cardputer-zero-internal /dev/dri/cardputer-zero-hdmi 2>/dev/null || true
  echo
  echo "=== labwc ==="
  env \
    CARDPUTER_ZERO_WAYLAND_SHELL=/bin/true \
    CARDPUTER_ZERO_LABWC_CONFIG_DIR="$CONFIG_DIR" \
    "$WRAPPER"
} >"$LOG" 2>&1

cat "$LOG"
