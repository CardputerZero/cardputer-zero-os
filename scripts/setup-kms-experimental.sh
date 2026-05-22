#!/bin/sh
set -eu

if [ "$(id -u)" -ne 0 ]; then
  echo "setup-kms-experimental.sh must run as root" >&2
  exit 1
fi

REPO_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BOOT_DIR=${BOOT_DIR:-/boot/firmware}
if [ ! -d "$BOOT_DIR" ] && [ -d /boot ]; then
  BOOT_DIR=/boot
fi

CONFIG_TXT="$BOOT_DIR/config.txt"
OVERLAY_DIR="$BOOT_DIR/overlays"
BASE_OVERLAY="$OVERLAY_DIR/cardputerzero-overlay.dtbo"
TARGET_OVERLAY="$OVERLAY_DIR/cardputerzero-kms-display.dtbo"
OLD_TARGET_OVERLAY="$OVERLAY_DIR/cardputerzero-kms-experimental.dtbo"
FIRMWARE="/lib/firmware/cardputerzero,st7789v.bin"
MODULES_LOAD_CONF=/etc/modules-load.d/cardputer-zero-kms.conf
BACKUP_ROOT=/var/backups/cardputer-zero-os/kms-experimental
STAMP=$(date +%Y%m%d-%H%M%S)
BACKUP_DIR="$BACKUP_ROOT/$STAMP"

need_command() {
  command=$1
  package_hint=$2
  if ! command -v "$command" >/dev/null 2>&1; then
    echo "$command is required. Install $package_hint first." >&2
    exit 1
  fi
}

need_file() {
  path=$1
  if [ ! -f "$path" ]; then
    echo "required file not found: $path" >&2
    exit 1
  fi
}

need_command dtc "device-tree-compiler"
need_file "$CONFIG_TXT"
need_file "$BASE_OVERLAY"

mkdir -p "$BACKUP_DIR"
cp -a "$CONFIG_TXT" "$BACKUP_DIR/config.txt"
cp -a "$BASE_OVERLAY" "$BACKUP_DIR/cardputerzero-overlay.dtbo"
[ ! -f "$TARGET_OVERLAY" ] || cp -a "$TARGET_OVERLAY" "$BACKUP_DIR/cardputerzero-kms-display.dtbo"
[ ! -f "$OLD_TARGET_OVERLAY" ] || cp -a "$OLD_TARGET_OVERLAY" "$BACKUP_DIR/cardputerzero-kms-experimental.dtbo"
[ ! -f "$FIRMWARE" ] || cp -a "$FIRMWARE" "$BACKUP_DIR/$(basename "$FIRMWARE")"
[ ! -f "$MODULES_LOAD_CONF" ] || cp -a "$MODULES_LOAD_CONF" "$BACKUP_DIR/$(basename "$MODULES_LOAD_CONF")"

TMPDIR=$(mktemp -d)
cleanup() {
  rm -rf "$TMPDIR"
}
trap cleanup EXIT HUP INT TERM

TARGET_DTS="$TMPDIR/cardputerzero-kms-display.dts"

cat >"$TARGET_DTS" <<'EOF'
/dts-v1/;
/plugin/;

/ {
    compatible = "brcm,bcm2835";

    fragment@0 {
        target = <&spi0>;
        __overlay__ {
            status = "okay";
            #address-cells = <1>;
            #size-cells = <0>;

            st7789v@0 {
                status = "disabled";
            };

            display@0 {
                compatible = "cardputerzero,st7789v", "panel-mipi-dbi-spi";
                reg = <0>;
                status = "okay";

                spi-max-frequency = <40000000>;
                dc-gpios = <&gpio 25 0>;

                write-only;
                format = "r5g6b5";

                width-mm = <26>;
                height-mm = <14>;

                panel-timing {
                    clock-frequency = <0>;
                    hactive = <320>;
                    vactive = <170>;
                    hfront-porch = <0>;
                    hsync-len = <0>;
                    hback-porch = <0>;
                    vfront-porch = <0>;
                    vsync-len = <0>;
                    vback-porch = <35>;
                };
            };
        };
    };
};
EOF

if ! grep -q 'panel-mipi-dbi-spi' "$TARGET_DTS"; then
  echo "failed to generate panel-mipi-dbi-spi overlay source" >&2
  exit 1
fi

dtc -@ -I dts -O dtb -o "$TARGET_OVERLAY" "$TARGET_DTS" 2>"$TMPDIR/dtc-compile.log" || {
  cat "$TMPDIR/dtc-compile.log" >&2
  exit 1
}
chmod 0644 "$TARGET_OVERLAY"
rm -f "$OLD_TARGET_OVERLAY"

"$REPO_DIR/scripts/build-st7789v-panel-firmware.sh" "$FIRMWARE"

cat >"$MODULES_LOAD_CONF" <<'EOF'
# Load the DRM tiny panel driver for the Cardputer Zero internal ST7789 panel.
#
# The experimental overlay keeps "cardputerzero,st7789v" as the first
# compatible string so panel-mipi-dbi requests /lib/firmware/cardputerzero,st7789v.bin.
# That makes the SPI modalias "spi:st7789v", which does not auto-load the
# generic panel_mipi_dbi module on this Pi OS kernel. Load it explicitly.
panel_mipi_dbi
EOF
chmod 0644 "$MODULES_LOAD_CONF"

CONFIG_TMP="$TMPDIR/config.txt"
awk '
  BEGIN { in_block = 0 }
  /^# BEGIN cardputer-zero-kms-experimental$/ { in_block = 1; next }
  /^# END cardputer-zero-kms-experimental$/ { in_block = 0; next }
  in_block { next }
  /^# cardputer-zero-kms-disabled: dtoverlay=cardputerzero-overlay([ \t]*|,.*)$/ {
    print "dtoverlay=cardputerzero-overlay"
    next
  }
  /^[ \t]*dtoverlay=fbtft,.*st7789v.*$/ {
    print "# cardputer-zero-kms-disabled: " $0
    next
  }
  /^[ \t]*dtoverlay=cardputerzero-kms-experimental([ \t]*|,.*)$/ { next }
  /^[ \t]*dtoverlay=cardputerzero-kms-display([ \t]*|,.*)$/ { next }
  { print }
' "$CONFIG_TXT" >"$CONFIG_TMP"

cat >>"$CONFIG_TMP" <<'EOF'
# BEGIN cardputer-zero-kms-experimental
# Experimental route 2: expose the Cardputer Zero internal ST7789 display as DRM/KMS.
# The base overlay still owns keyboard, audio, m5ioe1, sensors, and backlight.
# This display overlay only disables the fbdev ST7789 node and adds a
# panel-mipi-dbi-spi node on SPI0 CE0.
dtoverlay=cardputerzero-kms-display
# END cardputer-zero-kms-experimental
EOF

if ! grep -q '^dtoverlay=cardputerzero-overlay' "$CONFIG_TMP"; then
  cat >>"$CONFIG_TMP" <<'EOF'
# cardputer-zero-kms-base-overlay
dtoverlay=cardputerzero-overlay
EOF
fi

install -m 0644 "$CONFIG_TMP" "$CONFIG_TXT"

cat <<EOF
Cardputer Zero KMS experiment installed.

Backup:
  $BACKUP_DIR

Installed:
  $TARGET_OVERLAY
  $FIRMWARE
  $MODULES_LOAD_CONF

Config updated:
  $CONFIG_TXT

Reboot is required to test the DRM/KMS display path.
If the internal screen does not come up, restore with:
  sudo sh $REPO_DIR/scripts/restore-kms-experimental.sh
EOF
