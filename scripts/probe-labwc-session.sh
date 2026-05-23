#!/bin/sh
set -eu

print_section() {
  printf '\n=== %s ===\n' "$1"
}

print_section "tools"
for tool in labwc wlr-randr loginctl systemd-run dbus-run-session; do
  if command -v "$tool" >/dev/null 2>&1; then
    printf '%s: %s\n' "$tool" "$(command -v "$tool")"
  else
    printf '%s: missing\n' "$tool"
  fi
done

print_section "cardputer zero config"
for path in /etc/cardputer-zero/session.conf /etc/xdg/cardputer-zero-labwc/environment; do
  echo "--- $path"
  if [ -r "$path" ]; then
    sed -n '1,160p' "$path"
  else
    echo "missing"
  fi
done

print_section "drm devices"
ls -l /dev/dri 2>/dev/null || true
ls -l /dev/dri/by-path 2>/dev/null || true
for dev in /dev/dri/cardputer-zero-internal /dev/dri/cardputer-zero-hdmi; do
  if [ -e "$dev" ]; then
    printf '%s -> ' "$dev"
    readlink -f "$dev" || true
  else
    printf '%s missing\n' "$dev"
  fi
done

print_section "drm connectors"
if [ -d /sys/class/drm ]; then
  for connector in /sys/class/drm/card*-*; do
    [ -e "$connector" ] || continue
    echo "--- $connector"
    for file in status enabled modes; do
      if [ -r "$connector/$file" ]; then
        printf '%s: ' "$file"
        cat "$connector/$file" 2>/dev/null || true
        printf '\n'
      fi
    done
  done
fi

print_section "sessions"
loginctl list-sessions 2>/dev/null || true
if [ -n "${USER:-}" ]; then
  loginctl show-user "$USER" -p RuntimePath -p State -p Sessions -p Linger 2>/dev/null || true
fi

print_section "running display processes"
ps -eo user,pid,ppid,args | grep -E 'zero-greeter|zero-shell|labwc|lightdm|pi-greeter' | grep -v grep || true

print_section "wayland sockets"
for runtime in /run/user/*; do
  [ -d "$runtime" ] || continue
  find "$runtime" -maxdepth 1 -type s -name 'wayland-*' -printf '%p\n' 2>/dev/null || true
done

print_section "input devices"
for event in /sys/class/input/event*; do
  [ -e "$event" ] || continue
  name=$(cat "$event/device/name" 2>/dev/null || true)
  phys=$(cat "$event/device/phys" 2>/dev/null || true)
  printf '%s name=%s phys=%s\n' "$event" "$name" "$phys"
done
