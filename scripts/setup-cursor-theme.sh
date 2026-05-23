#!/bin/sh
set -eu

ROOT=${1:-}
THEME_DIR="$ROOT/usr/share/icons/cardputer-zero-empty"
CURSOR_DIR="$THEME_DIR/cursors"
CURSOR_FILE="$CURSOR_DIR/left_ptr"

install -d -m 0755 "$CURSOR_DIR"

cat >"$THEME_DIR/index.theme" <<'EOF'
[Icon Theme]
Name=Cardputer Zero Empty Cursor
Comment=Transparent cursor theme for the Cardputer Zero internal screen
Inherits=PiXtrix
EOF

if [ ! -s "$CURSOR_FILE" ]; then
  if command -v base64 >/dev/null 2>&1; then
    base64 -d >"$CURSOR_FILE" <<'EOF'
WGN1chAAAAAAAAEAAQAAAAIA/f8BAAAAHAAAACQAAAACAP3/AQAAAAEAAAABAAAAAQAAAAAAAAAAAAAAAAAAAAAAAAA=
EOF
  else
    printf 'base64 is required to install the empty cursor theme\n' >&2
    exit 1
  fi
fi

chmod 0644 "$CURSOR_FILE" "$THEME_DIR/index.theme"

for name in \
  default pointer X_cursor arrow top_left_arrow center_ptr right_ptr \
  hand1 hand2 watch left_ptr_watch xterm cross crosshair \
  plus question_arrow fleur sizing all-scroll col-resize row-resize \
  openhand closedhand grabbing grab sb_h_double_arrow sb_v_double_arrow \
  sb_left_arrow sb_right_arrow sb_up_arrow sb_down_arrow \
  left_side right_side top_side bottom_side \
  top_left_corner top_right_corner bottom_left_corner bottom_right_corner \
  dnd-copy dnd-move dnd-none dot dotbox; do
  ln -sf left_ptr "$CURSOR_DIR/$name"
done
