#!/bin/sh
set -eu

if [ "$(id -u)" -ne 0 ]; then
  echo "setup-internal-drm-display.sh must run as root" >&2
  exit 1
fi

REPO_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BOOT_DIR=${BOOT_DIR:-/boot/firmware}
if [ ! -d "$BOOT_DIR" ] && [ -d /boot ]; then
  BOOT_DIR=/boot
fi

CONFIG_TXT="$BOOT_DIR/config.txt"
CMDLINE_TXT="$BOOT_DIR/cmdline.txt"
OVERLAY_DIR="$BOOT_DIR/overlays"
BASE_OVERLAY="$OVERLAY_DIR/cardputerzero-overlay.dtbo"
TARGET_OVERLAY="$OVERLAY_DIR/cardputerzero-kms-display.dtbo"
FIRMWARE="/lib/firmware/cardputerzero,st7789v.bin"
MODULES_LOAD_CONF=/etc/modules-load.d/cardputer-zero-kms.conf
BACKUP_ROOT=/var/backups/cardputer-zero-os/internal-drm-display
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
need_file "$CMDLINE_TXT"
need_file "$BASE_OVERLAY"

mkdir -p "$BACKUP_DIR"
cp -a "$CONFIG_TXT" "$BACKUP_DIR/config.txt"
cp -a "$CMDLINE_TXT" "$BACKUP_DIR/cmdline.txt"
cp -a "$BASE_OVERLAY" "$BACKUP_DIR/cardputerzero-overlay.dtbo"
[ ! -f "$TARGET_OVERLAY" ] || cp -a "$TARGET_OVERLAY" "$BACKUP_DIR/cardputerzero-kms-display.dtbo"
[ ! -f "$FIRMWARE" ] || cp -a "$FIRMWARE" "$BACKUP_DIR/$(basename "$FIRMWARE")"
[ ! -f "$MODULES_LOAD_CONF" ] || cp -a "$MODULES_LOAD_CONF" "$BACKUP_DIR/$(basename "$MODULES_LOAD_CONF")"

TMPDIR=$(mktemp -d)
cleanup() {
  rm -rf "$TMPDIR"
}
trap cleanup EXIT HUP INT TERM

TARGET_DTS="$TMPDIR/cardputerzero-kms-display.dts"
BASE_DTS="$TMPDIR/cardputerzero-overlay.dts"
BASE_DTS_PATCHED="$TMPDIR/cardputerzero-overlay-no-display.dts"

dtc -I dtb -O dts -o "$BASE_DTS" "$BASE_OVERLAY" 2>"$TMPDIR/dtc-base-decompile.log" || {
  cat "$TMPDIR/dtc-base-decompile.log" >&2
  exit 1
}

perl -0e '
  my $path = shift @ARGV;
  open my $fh, "<", $path or die "open $path: $!";
  local $/;
  my $dts = <$fh>;
  close $fh;

  my $changed = ($dts =~ s{(st7789v\@0\s*\{\n)(.*?)(\n\s*\};)}{
    my ($head, $body, $tail) = ($1, $2, $3);
    $body =~ s/^\s*status\s*=\s*"[^"]*";\n//mg;
    $head . "\t\t\t\tstatus = \"disabled\";\n" . $body . $tail;
  }se);

  die "cardputerzero-overlay.dtbo does not contain st7789v\@0\n" unless $changed;
  print $dts;
' "$BASE_DTS" >"$BASE_DTS_PATCHED"

dtc -@ -I dts -O dtb -o "$BASE_OVERLAY" "$BASE_DTS_PATCHED" 2>"$TMPDIR/dtc-base-compile.log" || {
  cat "$TMPDIR/dtc-base-compile.log" >&2
  exit 1
}
chmod 0644 "$BASE_OVERLAY"

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

dtc -@ -I dts -O dtb -o "$TARGET_OVERLAY" "$TARGET_DTS" 2>"$TMPDIR/dtc-compile.log" || {
  cat "$TMPDIR/dtc-compile.log" >&2
  exit 1
}
chmod 0644 "$TARGET_OVERLAY"

sh "$REPO_DIR/scripts/build-st7789v-panel-firmware.sh" "$FIRMWARE"

cat >"$MODULES_LOAD_CONF" <<'EOF'
# Load the DRM tiny panel driver for the Cardputer Zero internal ST7789 panel.
panel_mipi_dbi
EOF
chmod 0644 "$MODULES_LOAD_CONF"

CONFIG_TMP="$TMPDIR/config.txt"
awk '
  BEGIN { in_block = 0 }
  /^# BEGIN cardputer-zero-internal-drm-display$/ { in_block = 1; next }
  /^# END cardputer-zero-internal-drm-display$/ { in_block = 0; next }
  /^# BEGIN cardputer-zero-kms-experimental$/ { in_block = 1; next }
  /^# END cardputer-zero-kms-experimental$/ { in_block = 0; next }
  in_block { next }
  /^[ \t]*dtoverlay=cardputerzero-kms-display([ \t]*|,.*)$/ { next }
  /^[ \t]*dtoverlay=cardputerzero-kms-experimental([ \t]*|,.*)$/ { next }
  { print }
' "$CONFIG_TXT" >"$CONFIG_TMP"

cat >>"$CONFIG_TMP" <<'EOF'
# BEGIN cardputer-zero-internal-drm-display
# Expose the Cardputer Zero internal ST7789 display as a DRM/KMS output.
# The base overlay remains responsible for keyboard, audio, m5ioe1, sensors,
# and backlight.
dtoverlay=cardputerzero-kms-display
# END cardputer-zero-internal-drm-display
EOF

if ! grep -q '^dtoverlay=cardputerzero-overlay' "$CONFIG_TMP"; then
  cat >>"$CONFIG_TMP" <<'EOF'
# cardputer-zero-base-overlay
dtoverlay=cardputerzero-overlay
EOF
fi

install -m 0644 "$CONFIG_TMP" "$CONFIG_TXT"

CMDLINE_TMP="$TMPDIR/cmdline.txt"
tr '\n' ' ' <"$CMDLINE_TXT" | sed -E 's/[[:space:]]+/ /g; s/^ //; s/ $//' >"$CMDLINE_TMP"
if ! grep -qw 'drm_kms_helper.fbdev_emulation=0' "$CMDLINE_TMP"; then
  printf ' drm_kms_helper.fbdev_emulation=0' >>"$CMDLINE_TMP"
fi
printf '\n' >>"$CMDLINE_TMP"
install -m 0644 "$CMDLINE_TMP" "$CMDLINE_TXT"

cat <<EOF
Cardputer Zero internal DRM display setup installed.

Backup:
  $BACKUP_DIR

Installed:
  $BASE_OVERLAY
  $TARGET_OVERLAY
  $FIRMWARE
  $MODULES_LOAD_CONF

Config updated:
  $CONFIG_TXT
  $CMDLINE_TXT

Reboot is required for the internal DRM display path.
EOF
