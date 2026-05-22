#!/bin/sh
set -eu

ROOT=${1:-}

find_cmdline() {
  for path in "$ROOT/boot/firmware/cmdline.txt" "$ROOT/boot/cmdline.txt"; do
    if [ -f "$path" ]; then
      printf '%s\n' "$path"
      return 0
    fi
  done
  return 1
}

cmdline=$(find_cmdline || true)
if [ -z "$cmdline" ]; then
  echo "No Raspberry Pi cmdline.txt found; skipped quiet boot setup." >&2
  exit 0
fi

if [ ! -f "$cmdline.cardputer-zero.bak" ]; then
  cp "$cmdline" "$cmdline.cardputer-zero.bak"
fi

line=$(tr '\n' ' ' <"$cmdline")

for token in quiet loglevel=3 vt.global_cursor_default=0 logo.nologo consoleblank=0; do
  case " $line " in
    *" $token "*) ;;
    *) line="$line $token" ;;
  esac
done

printf '%s\n' "$line" >"$cmdline"
echo "Updated $cmdline for quieter userspace handoff."
