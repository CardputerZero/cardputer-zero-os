#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
  echo "usage: $0 OUTPUT.bin" >&2
  exit 2
fi

OUTPUT=$1
TMP="${OUTPUT}.tmp.$$"

cleanup() {
  rm -f "$TMP"
}
trap cleanup EXIT HUP INT TERM

append_byte() {
  value=$(( $1 ))
  if [ "$value" -lt 0 ] || [ "$value" -gt 255 ]; then
    echo "byte out of range: $1" >&2
    exit 1
  fi
  octal=$(printf '%03o' "$value")
  printf "\\$octal" >>"$TMP"
}

append_command() {
  command=$1
  shift
  append_byte "$command"
  append_byte "$#"
  for parameter in "$@"; do
    append_byte "$parameter"
  done
}

mkdir -p "$(dirname "$OUTPUT")"
: >"$TMP"

# struct panel_mipi_dbi_config:
#   magic[15] = "MIPI DBI" + seven NUL bytes
#   file_format_version = 1
printf 'MIPI DBI' >>"$TMP"
i=0
while [ "$i" -lt 7 ]; do
  append_byte 0
  i=$((i + 1))
done
append_byte 1

# ST7789V init sequence for the Cardputer Zero internal display, expressed as
# panel-mipi-dbi command bytes.
#
# The user-facing screen is 320x170 in landscape orientation. That is
# implemented on the controller with MADCTL = MV | MY (0xa0), while the
# 35-pixel panel glass offset is applied as a page offset. Keeping this
# controller orientation prevents the DRM scanout image from wrapping on the
# physical landscape screen.
append_command 0x11
append_command 0x00 0x78
append_command 0x3a 0x55
append_command 0x36 0xa0
append_command 0xb2 0x05 0x05 0x00 0x33 0x33
append_command 0xb7 0x75
append_command 0xc2 0x01 0xff
append_command 0xc3 0x13
append_command 0xc4 0x20
append_command 0xbb 0x22
append_command 0xc5 0x20
append_command 0xd0 0xa4 0xa1
append_command 0xe0 0xd0 0x05 0x0a 0x09 0x08 0x05 0x2e 0x44 0x45 0x0f 0x17 0x16 0x2b 0x33
append_command 0xe1 0xd0 0x05 0x0a 0x09 0x08 0x05 0x2e 0x43 0x45 0x0f 0x16 0x16 0x2b 0x33
append_command 0x21
append_command 0x29

install -m 0644 "$TMP" "$OUTPUT"
trap - EXIT HUP INT TERM
cleanup

echo "wrote $OUTPUT"
