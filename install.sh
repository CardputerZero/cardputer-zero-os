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

install_file "$REPO_DIR/files/usr/local/bin/zero-splash" /usr/local/bin/zero-splash 0755
install_file "$REPO_DIR/files/usr/local/bin/cardputer-zero-session" /usr/local/bin/cardputer-zero-session 0755
install_file "$REPO_DIR/files/usr/local/sbin/zero-helper" /usr/local/sbin/zero-helper 0755

install_file "$REPO_DIR/files/etc/pam.d/zero-greeter" /etc/pam.d/zero-greeter 0644
install_file "$REPO_DIR/files/etc/systemd/system/zero-splash.service" /etc/systemd/system/zero-splash.service 0644
install_file "$REPO_DIR/files/etc/systemd/system/zero-greeter.service" /etc/systemd/system/zero-greeter.service 0644
install_file "$REPO_DIR/files/etc/systemd/user/zero-polkit-agent.service" /etc/systemd/user/zero-polkit-agent.service 0644
install_file "$REPO_DIR/files/etc/udev/rules.d/99-cardputer-zero.rules" /etc/udev/rules.d/99-cardputer-zero.rules 0644
install_file "$REPO_DIR/files/usr/share/polkit-1/actions/org.cardputerzero.zero-helper.policy" /usr/share/polkit-1/actions/org.cardputerzero.zero-helper.policy 0644

install_tree_files "$REPO_DIR/files/etc/cardputer-zero" /etc/cardputer-zero 0644
install_tree_files "$REPO_DIR/files/usr/share/cardputer-zero" /usr/share/cardputer-zero 0644

need_live_command pkexec "polkitd/policykit-1"
"$REPO_DIR/scripts/setup-greeter.sh" "$ROOT"
"$REPO_DIR/scripts/setup-polkit-agent.sh" "$ROOT"
"$REPO_DIR/scripts/setup-splash.sh" "$ROOT"
"$REPO_DIR/scripts/setup-session.sh" "$ROOT"
"$REPO_DIR/scripts/setup-udev.sh" "$ROOT"
"$REPO_DIR/scripts/setup-quiet-boot.sh" "$ROOT"

# Older cardputer-zero-os builds installed NOPASSWD sudoers entries for
# zero-helper. The polkit-backed model intentionally removes that bypass.
rm -f "$ROOT/etc/sudoers.d/cardputer-zero"

if [ -z "$ROOT" ]; then
  if command -v systemctl >/dev/null 2>&1; then
    systemctl daemon-reload
    systemctl --global disable zero-polkit-agent.service >/dev/null 2>&1 || true
    systemctl enable zero-splash.service
    systemctl enable zero-greeter.service
  fi
else
  rm -f "$ROOT/etc/systemd/user/default.target.wants/zero-polkit-agent.service"
fi

echo "cardputer-zero-os installed."
echo "Installed greeter: /usr/local/bin/zero-greeter"
echo "Installed session: /usr/local/bin/cardputer-zero-session"
echo "Installed polkit:  /usr/local/bin/zero-polkit-agent"
echo "Expected shell:   /opt/cardputer-zero-shell/bin/zero-shell"
