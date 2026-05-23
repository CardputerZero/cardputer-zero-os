#!/bin/sh
set -eu

# HITL Wayland/labwc session smoke test for the Zero internal greetd backend.
#
# This script temporarily points zero-greetd at a debug greeter wrapper, asks the
# operator to log in on the device screen, captures labwc diagnostics, then
# restores /etc/greetd/cardputer-zero.toml before exiting.

ROOT=${1:-}
if [ -n "$ROOT" ]; then
  echo "This live test does not support DESTDIR/image roots." >&2
  exit 1
fi

if [ "$(id -u)" -ne 0 ]; then
  echo "Run as root: sudo sh ./scripts/test-greetd-labwc-session.sh" >&2
  exit 1
fi

if ! command -v greetd >/dev/null 2>&1; then
  echo "greetd is not installed." >&2
  exit 1
fi

if [ ! -x /usr/local/bin/zero-greeter ]; then
  echo "Missing /usr/local/bin/zero-greeter. Run install.sh first." >&2
  exit 1
fi

if [ ! -x /usr/local/bin/cardputer-zero-labwc-session ]; then
  echo "Missing /usr/local/bin/cardputer-zero-labwc-session. Run install.sh first." >&2
  exit 1
fi

LOG=${CARDPUTER_ZERO_LABWC_SMOKE_LOG:-/tmp/cardputer-zero-greetd-labwc-smoke.log}
WLR_LOG=${CARDPUTER_ZERO_WLR_RANDR_LOG:-/tmp/cardputer-zero-wlr-randr.log}
CONFIG=/etc/greetd/cardputer-zero.toml
BACKUP=/tmp/cardputer-zero.greetd.toml.backup
GREETER_WRAPPER=/tmp/zero-greeter-greetd-labwc-smoke
SESSION_WRAPPER=/tmp/cardputer-zero-labwc-smoke-session
LABWC_BIN=/tmp/cardputer-zero-labwc-debug-bin
WAYLAND_PROBE=/tmp/cardputer-zero-labwc-wayland-probe
SMOKE_CONFIG=/tmp/cardputer-zero.greetd.labwc-smoke.toml

restore() {
  if [ -r "$BACKUP" ]; then
    install -m 0644 "$BACKUP" "$CONFIG"
  fi
  rm -f "$GREETER_WRAPPER" "$SESSION_WRAPPER" "$LABWC_BIN" "$WAYLAND_PROBE" "$SMOKE_CONFIG"
  systemctl restart zero-greetd.service >/dev/null 2>&1 || true
}

trap restore EXIT INT TERM

install -m 0644 "$CONFIG" "$BACKUP"
rm -f "$LOG" "$WLR_LOG"

cat >"$WAYLAND_PROBE" <<'EOF'
#!/bin/sh
{
  echo "=== date ==="
  date
  echo "=== identity ==="
  id
  echo "=== environment ==="
  env | sort
  echo "=== wlr-randr ==="
  wlr-randr || true
  echo "=== keepalive ==="
  sleep 20
} >/tmp/cardputer-zero-wlr-randr.log 2>&1
EOF
chmod 0755 "$WAYLAND_PROBE"

cat >"$LABWC_BIN" <<'EOF'
#!/bin/sh
exec /usr/bin/labwc -d -V "$@"
EOF
chmod 0755 "$LABWC_BIN"

cat >"$SESSION_WRAPPER" <<EOF
#!/bin/sh
LOG=$LOG
{
  echo "=== debug runner start ==="
  date
  echo "=== identity ==="
  id
  echo "=== tty ==="
  tty || true
  echo "=== loginctl sessions ==="
  loginctl list-sessions --no-legend || true
  echo "=== environment ==="
  env | sort
  echo "=== exec cardputer-zero-labwc-session ==="
} >"\$LOG" 2>&1

export CARDPUTER_ZERO_WAYLAND_SHELL=$WAYLAND_PROBE
export CARDPUTER_ZERO_LABWC_BIN=$LABWC_BIN
exec /usr/local/bin/cardputer-zero-labwc-session >>"\$LOG" 2>&1
EOF
chmod 0755 "$SESSION_WRAPPER"

cat >"$GREETER_WRAPPER" <<EOF
#!/bin/sh
export CARDPUTER_ZERO_GREETD_SESSION=$SESSION_WRAPPER
exec /usr/local/bin/zero-greeter
EOF
chmod 0755 "$GREETER_WRAPPER"

cat >"$SMOKE_CONFIG" <<EOF
[terminal]
vt = 8

[default_session]
command = "$GREETER_WRAPPER"
user = "_greetd"
EOF

install -m 0644 "$SMOKE_CONFIG" "$CONFIG"
systemctl restart zero-greetd.service

cat <<EOF
zero-greetd is now in temporary labwc smoke mode.

Log in on the Cardputer Zero internal screen now. The labwc probe keeps the
session alive for about 20 seconds if the compositor starts.

Waiting 35 seconds, then restoring the normal greetd config...
EOF

sleep 35

echo
echo "=== $LOG ==="
cat "$LOG" 2>/dev/null || true
echo
echo "=== $WLR_LOG ==="
cat "$WLR_LOG" 2>/dev/null || true
echo
echo "=== relevant processes ==="
ps -eo user,pid,ppid,args | grep -E 'greetd|zero-greeter|cardputer-zero-labwc|labwc|wlr-randr|zero-shell' | grep -v grep || true
echo
echo "Normal greetd config will be restored now."
