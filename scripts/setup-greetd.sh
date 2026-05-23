#!/bin/sh
set -eu

ROOT=${1:-}

if [ -n "$ROOT" ]; then
  exit 0
fi

if ! command -v greetd >/dev/null 2>&1; then
  echo "greetd is required for the internal-screen login backend." >&2
  echo "Install it explicitly with: sudo apt-get install greetd" >&2
  exit 1
fi

if getent passwd _greetd >/dev/null 2>&1; then
  for group in video input render cardputer-zero; do
    if getent group "$group" >/dev/null 2>&1; then
      usermod -a -G "$group" _greetd || true
    fi
  done
fi

if command -v systemd-tmpfiles >/dev/null 2>&1; then
  systemd-tmpfiles --create /etc/tmpfiles.d/cardputer-zero-xwayland.conf || true
fi

if command -v systemctl >/dev/null 2>&1; then
  systemctl daemon-reload || true
fi

cat <<'EOF'
Cardputer Zero greetd files are installed.

zero-greetd.service is the login backend for the internal Wayland/labwc session.
It keeps the regular Pi OS / HDMI LightDM path independent while giving the
Zero internal session a real greetd/PAM/logind user session.

Enable the internal greetd backend with:

  sudo systemctl enable --now zero-greetd.service

Rollback:

  sudo systemctl disable --now zero-greetd.service
  use SSH or HDMI LightDM as the recovery surface
EOF
