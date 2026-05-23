#!/bin/sh
set -eu

ROOT=${1:-}

chmod 0644 "$ROOT/etc/udev/rules.d/99-cardputer-zero.rules"
rm -f "$ROOT/etc/udev/rules.d/99-ignore-i2c-key.rules"

if [ -n "$ROOT" ]; then
  echo "DESTDIR install: skipped live group creation and user membership updates." >&2
  exit 0
fi

for group in cardputer-zero input video audio render gpio spi i2c; do
  if ! getent group "$group" >/dev/null 2>&1; then
    groupadd --system "$group" >/dev/null 2>&1 || true
  fi
done

groups_to_add=
for group in cardputer-zero input video audio render gpio spi i2c; do
  if getent group "$group" >/dev/null 2>&1; then
    if [ -z "$groups_to_add" ]; then
      groups_to_add=$group
    else
      groups_to_add=$groups_to_add,$group
    fi
  fi
done

awk -F: '$3 >= 1000 && $3 < 60000 && $6 ~ /^\/home\// && $7 !~ /(nologin|false)$/ { print $1 }' /etc/passwd |
while IFS= read -r user; do
  if [ -n "$user" ] && [ -n "$groups_to_add" ]; then
    usermod -aG "$groups_to_add" "$user" || true
  fi
done

if command -v udevadm >/dev/null 2>&1; then
  udevadm control --reload-rules || true
  udevadm trigger || true
fi
