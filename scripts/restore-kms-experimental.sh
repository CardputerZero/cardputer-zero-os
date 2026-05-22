#!/bin/sh
set -eu

if [ "$(id -u)" -ne 0 ]; then
  echo "restore-kms-experimental.sh must run as root" >&2
  exit 1
fi

BOOT_DIR=${BOOT_DIR:-/boot/firmware}
if [ ! -d "$BOOT_DIR" ] && [ -d /boot ]; then
  BOOT_DIR=/boot
fi

CONFIG_TXT="$BOOT_DIR/config.txt"
TARGET_OVERLAY="$BOOT_DIR/overlays/cardputerzero-kms-display.dtbo"
OLD_TARGET_OVERLAY="$BOOT_DIR/overlays/cardputerzero-kms-experimental.dtbo"
FIRMWARE="/lib/firmware/cardputerzero,st7789v.bin"
MODULES_LOAD_CONF=/etc/modules-load.d/cardputer-zero-kms.conf
BACKUP_ROOT=/var/backups/cardputer-zero-os/kms-experimental

if [ ! -d "$BACKUP_ROOT" ]; then
  echo "no KMS experiment backup directory found: $BACKUP_ROOT" >&2
  exit 1
fi

BACKUP_DIR=
for candidate in $(find "$BACKUP_ROOT" -mindepth 1 -maxdepth 1 -type d | sort -r); do
  [ -f "$candidate/config.txt" ] || continue
  if ! grep -q '^# BEGIN cardputer-zero-kms-experimental$' "$candidate/config.txt"; then
    BACKUP_DIR=$candidate
    break
  fi
done

if [ -z "$BACKUP_DIR" ]; then
  echo "no non-experimental KMS backup found under $BACKUP_ROOT" >&2
  exit 1
fi

install -m 0644 "$BACKUP_DIR/config.txt" "$CONFIG_TXT"

if [ -f "$BACKUP_DIR/cardputerzero-kms-display.dtbo" ]; then
  install -m 0644 "$BACKUP_DIR/cardputerzero-kms-display.dtbo" "$TARGET_OVERLAY"
else
  rm -f "$TARGET_OVERLAY"
fi

if [ -f "$BACKUP_DIR/cardputerzero-kms-experimental.dtbo" ]; then
  install -m 0644 "$BACKUP_DIR/cardputerzero-kms-experimental.dtbo" "$OLD_TARGET_OVERLAY"
else
  rm -f "$OLD_TARGET_OVERLAY"
fi

if [ -f "$BACKUP_DIR/$(basename "$FIRMWARE")" ]; then
  install -m 0644 "$BACKUP_DIR/$(basename "$FIRMWARE")" "$FIRMWARE"
else
  rm -f "$FIRMWARE"
fi

if [ -f "$BACKUP_DIR/$(basename "$MODULES_LOAD_CONF")" ]; then
  install -m 0644 "$BACKUP_DIR/$(basename "$MODULES_LOAD_CONF")" "$MODULES_LOAD_CONF"
else
  rm -f "$MODULES_LOAD_CONF"
fi

cat <<EOF
Cardputer Zero KMS experiment restored from:
  $BACKUP_DIR

Reboot is required to return to the previous display path.
EOF
