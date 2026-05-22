#!/bin/sh
set -eu

ROOT=${1:-}
REPO_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CONF_DIR="$ROOT/etc/lightdm/lightdm.conf.d"
CONF_FILE="$CONF_DIR/99-cardputer-zero-no-autologin.conf"
ENV_SOURCE="$REPO_DIR/files/etc/cardputer-zero/lightdm-labwc-environment"
GREETER_SESSION="cardputer-zero-pi-greeter-labwc"
HDMI_DRM_DEVICE="/dev/dri/cardputer-zero-hdmi"

mkdir -p "$CONF_DIR"
install -m 0644 \
  "$REPO_DIR/files/etc/lightdm/lightdm.conf.d/99-cardputer-zero-no-autologin.conf" \
  "$CONF_FILE"

install -D -m 0644 "$ENV_SOURCE" "$ROOT/etc/cardputer-zero/lightdm-labwc-environment"

append_env_if_missing() {
  env_file=$1
  tmp="${env_file}.tmp.$$"

  mkdir -p "$(dirname "$env_file")"
  touch "$env_file"
  awk '
    $0 == ". /etc/cardputer-zero/lightdm-labwc-environment" { next }
    $0 == "# Cardputer Zero: keep Pi OS LightDM/labwc off the internal SPI display." { next }
    /^[ \t]*WLR_DRM_DEVICES[ \t]*=/ { next }
    { print }
  ' "$env_file" >"$tmp"
  {
    printf '\n'
    printf '# Cardputer Zero: keep Pi OS LightDM/labwc off the internal SPI display.\n'
    printf 'WLR_DRM_DEVICES=%s\n' "$HDMI_DRM_DEVICE"
  } >>"$tmp"
  install -m 0644 "$tmp" "$env_file"
  rm -f "$tmp"
}

append_env_if_missing "$ROOT/etc/xdg/labwc-greeter/environment"
append_env_if_missing "$ROOT/etc/xdg/labwc/environment"

# Raspberry Pi OS may ship autologin in /etc/lightdm/lightdm.conf. Drop those
# keys there as well so this profile has a single authentication rule:
# LightDM is allowed as an HDMI/recovery login surface, but never as autologin.
MAIN_CONF="$ROOT/etc/lightdm/lightdm.conf"
if [ -f "$MAIN_CONF" ]; then
  tmp="${MAIN_CONF}.tmp.$$"
  awk -v greeter_session="$GREETER_SESSION" '
    /^[ \t]*greeter-session[ \t]*=/ {
      value = $0
      sub(/^[ \t]*greeter-session[ \t]*=[ \t]*/, "", value)
      sub(/[ \t]*$/, "", value)
      if (value == greeter_session) {
        if (!greeter_seen) {
          print "greeter-session=" greeter_session
          greeter_seen = 1
        }
      } else {
        print "# cardputer-zero-os replaced: " $0
        if (!greeter_seen) {
          print "greeter-session=" greeter_session
          greeter_seen = 1
        }
      }
      next
    }
    /^[ \t]*autologin-user[ \t]*=/ { print "# cardputer-zero-os disabled: " $0; next }
    /^[ \t]*autologin-session[ \t]*=/ { print "# cardputer-zero-os disabled: " $0; next }
    /^[ \t]*autologin-guest[ \t]*=/ { print "# cardputer-zero-os disabled: " $0; next }
    /^[ \t]*autologin-user-timeout[ \t]*=/ { print "# cardputer-zero-os disabled: " $0; next }
    { print }
  ' "$MAIN_CONF" >"$tmp"
  install -m 0644 "$tmp" "$MAIN_CONF"
  rm -f "$tmp"
fi
