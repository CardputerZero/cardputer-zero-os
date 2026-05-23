#!/bin/sh
set -eu

print_section() {
  printf '\n=== %s ===\n' "$1"
}

read_if_exists() {
  path=$1
  if [ -e "$path" ]; then
    printf '%s: ' "$path"
    cat "$path" 2>/dev/null || true
    printf '\n'
  fi
}

print_section "kernel"
uname -a

print_section "boot config"
for path in /boot/firmware/config.txt /boot/config.txt; do
  if [ -f "$path" ]; then
    echo "$path"
    grep -nE 'dtoverlay|dtparam|kms|mipi|dbi|cardputer|disable_fw_kms|display_auto_detect' "$path" || true
  fi
done

print_section "drm"
if [ -d /sys/class/drm ]; then
  find /sys/class/drm -maxdepth 1 -mindepth 1 -printf '%f\n' | sort
  for connector in /sys/class/drm/card*-*; do
    [ -e "$connector" ] || continue
    echo "--- $connector"
    read_if_exists "$connector/status"
    read_if_exists "$connector/enabled"
    read_if_exists "$connector/modes"
  done
else
  echo "/sys/class/drm not found"
fi

print_section "modules"
lsmod | grep -Ei 'st7789|mipi|dbi|tiny|drm|vc4|spi_bcm2835|backlight' || true

print_section "spi"
for dev in /sys/bus/spi/devices/*; do
  [ -e "$dev" ] || continue
  echo "--- $dev"
  read_if_exists "$dev/modalias"
  if [ -e "$dev/driver" ]; then
    printf '%s/driver: ' "$dev"
    readlink -f "$dev/driver" || true
  fi
done

print_section "firmware overlays"
for dir in /boot/firmware/overlays /boot/overlays; do
  [ -d "$dir" ] || continue
  echo "$dir"
  find "$dir" -maxdepth 1 -type f \( -name '*cardputer*' -o -name '*mipi*' -o -name '*dbi*' -o -name '*st77*' \) -printf '%f\n' | sort
done

print_section "labwc/wlroots"
if command -v pgrep >/dev/null 2>&1; then
  pgrep -a labwc || true
fi
for runtime in /run/user/*; do
  [ -d "$runtime" ] || continue
  find "$runtime" -maxdepth 1 -type s \( -name 'wayland-*' -o -name 'X*' \) -printf '%p\n' 2>/dev/null || true
done

print_section "tools"
for tool in dtc dtoverlay modetest drm_info wlr-randr labwc weston-info; do
  if command -v "$tool" >/dev/null 2>&1; then
    printf '%s: %s\n' "$tool" "$(command -v "$tool")"
  else
    printf '%s: missing\n' "$tool"
  fi
done
