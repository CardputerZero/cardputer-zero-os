#!/bin/sh
set -eu

# HITL Wayland/labwc visible-client smoke test for the Zero internal greetd backend.
#
# This temporarily points zero-greetd at a debug greeter wrapper, asks the
# operator to log in on the device screen, starts labwc on the Zero internal DRM
# output, launches a small GUI client, captures a compositor screenshot, then
# restores /etc/greetd/cardputer-zero.toml before exiting.

ROOT=${1:-}
if [ -n "$ROOT" ]; then
  echo "This live test does not support DESTDIR/image roots." >&2
  exit 1
fi

if [ "$(id -u)" -ne 0 ]; then
  echo "Run as root: sudo sh ./scripts/test-greetd-labwc-visible-client.sh" >&2
  exit 1
fi

if ! command -v greetd >/dev/null 2>&1; then
  echo "greetd is not installed." >&2
  exit 1
fi

if ! command -v labwc >/dev/null 2>&1; then
  echo "labwc is not installed." >&2
  exit 1
fi

if ! command -v wlr-randr >/dev/null 2>&1; then
  echo "wlr-randr is not installed." >&2
  exit 1
fi

if ! command -v grim >/dev/null 2>&1; then
  echo "grim is not installed." >&2
  exit 1
fi

if ! command -v zenity >/dev/null 2>&1; then
  echo "zenity is not installed; install it or adjust the visible client." >&2
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

LOG=${CARDPUTER_ZERO_LABWC_VISIBLE_LOG:-/tmp/cardputer-zero-greetd-labwc-visible.log}
CLIENT_LOG=${CARDPUTER_ZERO_LABWC_VISIBLE_CLIENT_LOG:-/tmp/cardputer-zero-labwc-visible-client.log}
SHOT=${CARDPUTER_ZERO_LABWC_VISIBLE_SHOT:-/tmp/cardputer-zero-labwc-visible.png}
WAIT_SECONDS=${CARDPUTER_ZERO_LABWC_VISIBLE_WAIT_SECONDS:-120}
SETTLE_SECONDS=${CARDPUTER_ZERO_LABWC_VISIBLE_SETTLE_SECONDS:-15}
CONFIG=/etc/greetd/cardputer-zero.toml
BACKUP=/tmp/cardputer-zero.greetd.toml.visible-backup
GREETER_WRAPPER=/tmp/zero-greeter-greetd-labwc-visible
SESSION_WRAPPER=/tmp/cardputer-zero-labwc-visible-session
LABWC_BIN=/tmp/cardputer-zero-labwc-visible-debug-bin
VISIBLE_CLIENT=/tmp/cardputer-zero-labwc-visible-client
SMOKE_CONFIG=/tmp/cardputer-zero.greetd.labwc-visible.toml

restore() {
  if [ -r "$BACKUP" ]; then
    install -m 0644 "$BACKUP" "$CONFIG"
  fi
  rm -f "$GREETER_WRAPPER" "$SESSION_WRAPPER" "$LABWC_BIN" "$VISIBLE_CLIENT" "$SMOKE_CONFIG"
  systemctl restart zero-greetd.service >/dev/null 2>&1 || true
}

trap restore EXIT INT TERM

install -m 0644 "$CONFIG" "$BACKUP"
rm -f "$LOG" "$CLIENT_LOG" "$SHOT"

cat >"$VISIBLE_CLIENT" <<'EOF'
#!/bin/sh
{
  echo "=== date ==="
  date
  echo "=== identity ==="
  id
  echo "=== environment ==="
  env | sort
  echo "=== wlr-randr before client ==="
  wlr-randr || true

  export GDK_BACKEND=wayland,x11
  zenity --info \
    --title="Cardputer Zero" \
    --text="labwc visible client test" \
    --width=260 \
    --height=96 &
  client_pid=$!

  sleep 5
  echo "=== grim screenshot ==="
  grim /tmp/cardputer-zero-labwc-visible.png || true
  ls -l /tmp/cardputer-zero-labwc-visible.png 2>/dev/null || true

  echo "=== wlr-randr after client ==="
  wlr-randr || true

  sleep 20
  kill "$client_pid" 2>/dev/null || true
  wait "$client_pid" 2>/dev/null || true
} >/tmp/cardputer-zero-labwc-visible-client.log 2>&1
EOF
chmod 0755 "$VISIBLE_CLIENT"

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

export CARDPUTER_ZERO_WAYLAND_SHELL=$VISIBLE_CLIENT
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
zero-greetd is now in temporary labwc visible-client smoke mode.

Log in on the Cardputer Zero internal screen now. If labwc and the client both
start, the screen should show a small "Cardputer Zero" dialog for about 25
seconds.

Waiting up to $WAIT_SECONDS seconds, then restoring the normal greetd config...
EOF

i=0
while [ "$i" -lt "$WAIT_SECONDS" ]; do
  if [ -s "$SHOT" ]; then
    echo "Screenshot captured; waiting $SETTLE_SECONDS more seconds before restore..."
    sleep "$SETTLE_SECONDS"
    break
  fi
  i=$((i + 1))
  sleep 1
done

echo
echo "=== $LOG ==="
cat "$LOG" 2>/dev/null || true
echo
echo "=== $CLIENT_LOG ==="
cat "$CLIENT_LOG" 2>/dev/null || true
echo
echo "=== screenshot ==="
ls -l "$SHOT" 2>/dev/null || true
echo
echo "=== relevant processes ==="
ps -eo user,pid,ppid,args | grep -E 'greetd|zero-greeter|cardputer-zero-labwc|labwc|zenity|zero-shell' | grep -v grep || true
echo
echo "Normal greetd config will be restored now."
