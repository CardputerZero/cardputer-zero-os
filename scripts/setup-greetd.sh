#!/bin/sh
set -eu

ROOT=${1:-}

if [ -n "$ROOT" ]; then
  exit 0
fi

if ! getent passwd _greetd >/dev/null 2>&1; then
  useradd --system --home-dir /var/lib/cardputer-zero-greeter --create-home --shell /usr/sbin/nologin _greetd
fi

for group in video input render cardputer-zero; do
  if getent group "$group" >/dev/null 2>&1; then
    usermod -a -G "$group" _greetd || true
  fi
done

if command -v systemd-tmpfiles >/dev/null 2>&1; then
  systemd-tmpfiles --create /etc/tmpfiles.d/cardputer-zero-xwayland.conf || true
fi

if command -v systemctl >/dev/null 2>&1; then
  systemctl daemon-reload || true
fi

cat <<'EOF'
Cardputer Zero internal greeter files are installed.

zero-greetd.service is the internal-screen greeter backend. The unit name is
preserved for upgrade compatibility, but the service does not run the greetd
daemon. systemd opens a PAM/logind greeter session as _greetd and then runs
cardputer-zero-greeter-session.

Enable the internal greeter backend with:

  sudo systemctl enable --now zero-greetd.service

Rollback:

  sudo systemctl disable --now zero-greetd.service
  use SSH or HDMI LightDM as the recovery surface
EOF
