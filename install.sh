#!/bin/sh
set -eu

if [ "$(id -u)" -ne 0 ]; then
  echo "install.sh must run as root" >&2
  exit 1
fi

ROOT=${DESTDIR:-}
REPO_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

need_live_command() {
  command=$1
  package_hint=$2

  if [ -n "$ROOT" ]; then
    return
  fi

  if ! command -v "$command" >/dev/null 2>&1; then
    echo "$command is required. Install $package_hint first." >&2
    exit 1
  fi
}

install_file() {
  src=$1
  dst=$2
  mode=$3
  install -D -m "$mode" "$src" "$ROOT$dst"
}

install_tree_files() {
  src_dir=$1
  dst_dir=$2
  mode=$3
  if [ ! -d "$src_dir" ]; then
    return
  fi
  find "$src_dir" -type f | while IFS= read -r src; do
    rel=${src#"$src_dir"/}
    install_file "$src" "$dst_dir/$rel" "$mode"
  done
}

install_file "$REPO_DIR/files/usr/local/bin/cardputer-zero-lightdm-labwc" /usr/local/bin/cardputer-zero-lightdm-labwc 0755
install_file "$REPO_DIR/files/usr/local/bin/cardputer-zero-greeter-session" /usr/local/bin/cardputer-zero-greeter-session 0755
install_file "$REPO_DIR/files/usr/local/bin/cardputer-zero-labwc-session" /usr/local/bin/cardputer-zero-labwc-session 0755
install_file "$REPO_DIR/files/usr/local/bin/cardputer-zero-session" /usr/local/bin/cardputer-zero-session 0755
install_file "$REPO_DIR/files/usr/local/bin/zero-key-policy" /usr/local/bin/zero-key-policy 0755
install_file "$REPO_DIR/files/usr/local/bin/zero-shell-control" /usr/local/bin/zero-shell-control 0755
install_file "$REPO_DIR/files/usr/local/sbin/zero-helper" /usr/local/sbin/zero-helper 0755
install_file "$REPO_DIR/files/usr/local/sbin/zero-hdmi-lightdm-policy" /usr/local/sbin/zero-hdmi-lightdm-policy 0755

install_file "$REPO_DIR/files/usr/share/xgreeters/cardputer-zero-pi-greeter-labwc.desktop" /usr/share/xgreeters/cardputer-zero-pi-greeter-labwc.desktop 0644
install_file "$REPO_DIR/files/etc/systemd/system/zero-greetd.service" /etc/systemd/system/zero-greetd.service 0644
install_file "$REPO_DIR/files/etc/systemd/system/zero-key-policy.service" /etc/systemd/system/zero-key-policy.service 0644
install_file "$REPO_DIR/files/etc/systemd/system/zero-hdmi-lightdm-policy.service" /etc/systemd/system/zero-hdmi-lightdm-policy.service 0644
install_file "$REPO_DIR/files/etc/systemd/system/lightdm.service.d/10-cardputer-zero-hdmi.conf" /etc/systemd/system/lightdm.service.d/10-cardputer-zero-hdmi.conf 0644
install_file "$REPO_DIR/files/etc/systemd/user/zero-polkit-agent.service" /etc/systemd/user/zero-polkit-agent.service 0644
install_file "$REPO_DIR/files/etc/tmpfiles.d/cardputer-zero-xwayland.conf" /etc/tmpfiles.d/cardputer-zero-xwayland.conf 0644
install_file "$REPO_DIR/files/etc/udev/rules.d/99-cardputer-zero.rules" /etc/udev/rules.d/99-cardputer-zero.rules 0644
install_file "$REPO_DIR/files/usr/share/polkit-1/actions/org.cardputerzero.zero-helper.policy" /usr/share/polkit-1/actions/org.cardputerzero.zero-helper.policy 0644

install_tree_files "$REPO_DIR/files/etc/cardputer-zero" /etc/cardputer-zero 0644
install_tree_files "$REPO_DIR/files/etc/greetd" /etc/greetd 0644
install_tree_files "$REPO_DIR/files/etc/xdg" /etc/xdg 0644
install_tree_files "$REPO_DIR/files/usr/share/cardputer-zero" /usr/share/cardputer-zero 0644

need_live_command pkexec "polkitd/policykit-1"
need_live_command greetd "greetd"
need_live_command wlrctl "wlrctl"
need_live_command dtc "device-tree-compiler"
if [ -z "$ROOT" ]; then
  sh "$REPO_DIR/scripts/setup-internal-drm-display.sh"
fi
sh "$REPO_DIR/scripts/setup-greeter.sh" "$ROOT"
sh "$REPO_DIR/scripts/setup-polkit-agent.sh" "$ROOT"
sh "$REPO_DIR/scripts/setup-cursor-theme.sh" "$ROOT"
sh "$REPO_DIR/scripts/setup-session.sh" "$ROOT"
sh "$REPO_DIR/scripts/setup-udev.sh" "$ROOT"
sh "$REPO_DIR/scripts/setup-lightdm-policy.sh" "$ROOT"
sh "$REPO_DIR/scripts/setup-greetd.sh" "$ROOT"
sh "$REPO_DIR/scripts/setup-quiet-boot.sh" "$ROOT"

# Older builds installed NOPASSWD sudoers entries for zero-helper. The
# polkit-backed model intentionally removes that bypass.
rm -f "$ROOT/etc/sudoers.d/cardputer-zero"

# Remove old direct greeter leftovers. The only login UI path is now the
# greetd-managed Wayland greeter.
rm -f "$ROOT/usr/local/bin/zero-greeter"
rm -f "$ROOT/usr/local/bin/zero-greeter.pre-greetd"
rm -f "$ROOT/etc/pam.d/zero-greeter"
rm -f "$ROOT/etc/systemd/system/zero-greeter.service"
rm -f "$ROOT/etc/systemd/system/multi-user.target.wants/zero-greeter.service"

if [ -z "$ROOT" ]; then
  if command -v systemctl >/dev/null 2>&1; then
    systemctl daemon-reload
    systemctl --global disable zero-polkit-agent.service >/dev/null 2>&1 || true
    systemctl daemon-reload
    systemctl enable zero-hdmi-lightdm-policy.service
    systemctl enable zero-greetd.service
    systemctl enable zero-key-policy.service
  fi
else
  rm -f "$ROOT/etc/systemd/user/default.target.wants/zero-polkit-agent.service"
fi

echo "cardputer-zero-os installed."
echo "Installed greeter: /usr/local/bin/zero-greeter-wayland"
echo "Installed session: /usr/local/bin/cardputer-zero-session"
echo "Installed polkit:  /usr/local/bin/zero-polkit-agent"
echo "Expected shell:   /opt/cardputer-zero-shell/bin/zero-shell-wayland"
