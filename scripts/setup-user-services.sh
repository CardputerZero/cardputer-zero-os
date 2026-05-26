#!/bin/sh
set -eu

ROOT=${1:-}

# These services are useful in the authenticated Zero graphical session, but
# they are too expensive to start for every user manager, especially the
# greeter and plain SSH sessions on a 320x170 handheld.
ZERO_USER_UNITS="
pipewire.service
pipewire.socket
pipewire-pulse.service
pipewire-pulse.socket
wireplumber.service
wireplumber@.service
filter-chain.service
xdg-permission-store.service
"

mask_greeter_user_units() {
  greeter_home=$1
  user_unit_dir="$greeter_home/.config/systemd/user"

  install -d -m 0700 "$ROOT$greeter_home" "$ROOT$greeter_home/.config" "$ROOT$greeter_home/.config/systemd" "$ROOT$user_unit_dir"
  for unit in $ZERO_USER_UNITS; do
    ln -sfn /dev/null "$ROOT$user_unit_dir/$unit"
  done

  if [ -z "$ROOT" ] && getent passwd _greetd >/dev/null 2>&1; then
    chown -R _greetd:_greetd "$greeter_home/.config" || true
  fi
}

disable_global_user_units() {
  [ -z "$ROOT" ] || return 0
  command -v systemctl >/dev/null 2>&1 || return 0

  systemctl --global disable $ZERO_USER_UNITS >/dev/null 2>&1 || true

  for wants_dir in /etc/systemd/user/default.target.wants /etc/systemd/user/sockets.target.wants; do
    [ -d "$wants_dir" ] || continue
    for unit in $ZERO_USER_UNITS; do
      rm -f "$wants_dir/$unit"
    done
  done
}

reload_and_stop_greeter_user_units() {
  [ -z "$ROOT" ] || return 0
  command -v runuser >/dev/null 2>&1 || return 0
  getent passwd _greetd >/dev/null 2>&1 || return 0

  greeter_uid=$(id -u _greetd)
  greeter_runtime=/run/user/$greeter_uid
  [ -S "$greeter_runtime/bus" ] || return 0

  runuser -u _greetd -- env \
    XDG_RUNTIME_DIR="$greeter_runtime" \
    DBUS_SESSION_BUS_ADDRESS="unix:path=$greeter_runtime/bus" \
    systemctl --user daemon-reload >/dev/null 2>&1 || true

  runuser -u _greetd -- env \
    XDG_RUNTIME_DIR="$greeter_runtime" \
    DBUS_SESSION_BUS_ADDRESS="unix:path=$greeter_runtime/bus" \
    systemctl --user stop $ZERO_USER_UNITS >/dev/null 2>&1 || true
}

if [ -z "$ROOT" ] && getent passwd _greetd >/dev/null 2>&1; then
  greeter_home=$(getent passwd _greetd | awk -F: '{print $6}')
else
  greeter_home=/var/lib/cardputer-zero-greeter
fi

mask_greeter_user_units "$greeter_home"
disable_global_user_units
reload_and_stop_greeter_user_units

cat <<'EOF'
Cardputer Zero user service policy installed.

- Heavy audio/portal user services are no longer globally auto-started for
  every user manager.
- The _greetd greeter user masks those services completely.
- The authenticated Zero graphical session starts PipeWire/WirePlumber
  explicitly from cardputer-zero-shell-session.
EOF
