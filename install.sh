#!/bin/sh
set -eu

if [ "$(id -u)" -ne 0 ]; then
  echo "install.sh must run as root" >&2
  exit 1
fi

ROOT=${DESTDIR:-}
REPO_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

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
install_file "$REPO_DIR/files/etc/sudoers.d/cardputer-zero" /etc/sudoers.d/cardputer-zero 0440
install_file "$REPO_DIR/files/etc/udev/rules.d/99-cardputer-zero.rules" /etc/udev/rules.d/99-cardputer-zero.rules 0644

install_tree_files "$REPO_DIR/files/etc/cardputer-zero" /etc/cardputer-zero 0644
install_tree_files "$REPO_DIR/files/usr/share/cardputer-zero" /usr/share/cardputer-zero 0644

"$REPO_DIR/scripts/setup-greeter.sh" "$ROOT"
"$REPO_DIR/scripts/setup-splash.sh" "$ROOT"
"$REPO_DIR/scripts/setup-session.sh" "$ROOT"
"$REPO_DIR/scripts/setup-udev.sh" "$ROOT"
"$REPO_DIR/scripts/setup-quiet-boot.sh" "$ROOT"

if [ -z "$ROOT" ]; then
  if command -v systemctl >/dev/null 2>&1; then
    systemctl daemon-reload
    systemctl enable zero-splash.service
    systemctl enable zero-greeter.service
  fi
fi

echo "cardputer-zero-os installed."
echo "Installed greeter: /usr/local/bin/zero-greeter"
echo "Installed session: /usr/local/bin/cardputer-zero-session"
echo "Expected shell:   /opt/cardputer-zero-shell/bin/zero-shell"

