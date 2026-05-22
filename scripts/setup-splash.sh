#!/bin/sh
set -eu

ROOT=${1:-}

chmod 0755 "$ROOT/usr/local/bin/zero-splash"
mkdir -p "$ROOT/usr/share/cardputer-zero/assets"
mkdir -p "$ROOT/usr/share/cardputer-zero/themes"
