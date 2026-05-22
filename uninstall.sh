#!/bin/sh
set -eu

if [ "$(id -u)" -ne 0 ]; then
  echo "uninstall.sh must run as root" >&2
  exit 1
fi

restore_cmdline() {
  for path in /boot/firmware/cmdline.txt /boot/cmdline.txt; do
    if [ -f "$path.cardputer-zero.bak" ]; then
      cp "$path.cardputer-zero.bak" "$path"
      echo "Restored $path from $path.cardputer-zero.bak"
    fi
  done
}

if command -v systemctl >/dev/null 2>&1; then
  systemctl disable --now zero-greeter.service >/dev/null 2>&1 || true
  systemctl disable --now zero-splash.service >/dev/null 2>&1 || true
fi

rm -f /etc/systemd/system/zero-greeter.service
rm -f /etc/systemd/system/zero-splash.service
rm -f /etc/pam.d/zero-greeter
rm -f /etc/sudoers.d/cardputer-zero
rm -f /etc/udev/rules.d/99-cardputer-zero.rules
rm -f /usr/local/bin/zero-greeter
rm -f /usr/local/bin/zero-splash
rm -f /usr/local/bin/cardputer-zero-session
rm -f /usr/local/sbin/zero-helper

rm -rf /etc/cardputer-zero
rm -rf /usr/share/cardputer-zero

restore_cmdline

if command -v systemctl >/dev/null 2>&1; then
  systemctl daemon-reload || true
fi

if command -v udevadm >/dev/null 2>&1; then
  udevadm control --reload-rules || true
fi

echo "cardputer-zero-os uninstalled."
echo "User accounts and groups were left intact."

