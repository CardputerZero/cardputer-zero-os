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
  systemctl --global disable zero-polkit-agent.service >/dev/null 2>&1 || true
  systemctl disable --now zero-greetd.service >/dev/null 2>&1 || true
  systemctl disable --now zero-key-policy.service >/dev/null 2>&1 || true
  systemctl disable --now zero-hdmi-lightdm-policy.service >/dev/null 2>&1 || true
fi

rm -f /etc/systemd/system/zero-greetd.service
rm -f /etc/systemd/system/zero-key-policy.service
rm -f /etc/systemd/system/zero-hdmi-lightdm-policy.service
rm -f /etc/systemd/system/lightdm.service.d/10-cardputer-zero-hdmi.conf
rm -f /etc/systemd/user/zero-polkit-agent.service
rm -f /etc/systemd/user/default.target.wants/zero-polkit-agent.service
rm -f /etc/tmpfiles.d/cardputer-zero-xwayland.conf
rm -f /etc/sudoers.d/cardputer-zero
rm -f /etc/udev/rules.d/99-cardputer-zero.rules
rm -f /usr/share/polkit-1/actions/org.cardputerzero.zero-helper.policy
rm -f /usr/local/bin/zero-greeter
rm -f /usr/local/bin/zero-greeter.pre-greetd
rm -f /usr/local/bin/zero-greeter-wayland
rm -f /usr/local/bin/zero-polkit-agent
rm -f /usr/local/bin/zero-polkit-prompt-wayland
rm -f /usr/local/bin/cardputer-zero-lightdm-labwc
rm -f /usr/local/bin/cardputer-zero-greeter-session
rm -f /usr/local/bin/cardputer-zero-idle
rm -f /usr/local/bin/cardputer-zero-labwc-session
rm -f /usr/local/bin/cardputer-zero-shell-session
rm -f /usr/local/bin/cardputer-zero-session
rm -f /usr/local/bin/zero-window-agent
rm -f /usr/local/bin/zero-key-policy
rm -f /usr/local/sbin/zero-helper
rm -f /usr/local/sbin/zero-hdmi-lightdm-policy
rm -f /usr/share/X11/xkb/keycodes/cardputerzero
rm -f /usr/share/X11/xkb/symbols/cardputerzero
rm -f /usr/share/X11/xkb/rules/cardputerzero

rm -rf /etc/cardputer-zero
rm -f /etc/greetd/cardputer-zero.toml
rm -rf /etc/xdg/cardputer-zero-greeter-labwc
rm -rf /etc/xdg/cardputer-zero-labwc
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
