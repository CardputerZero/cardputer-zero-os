# Input Profile

`cardputer-zero-os` owns the Cardputer Zero hardware input profile. This is
separate from ZeroShell and from individual applications: every Wayland client
running in the internal Zero session should see the same keyboard behavior.

## Runtime Path

The internal keyboard path is:

```text
tca8418c keyboard
  -> tca8418_keypad_m5stack kernel driver
  -> evdev input event
  -> libinput
  -> xkbcommon
  -> labwc
  -> Wayland client
```

`Sym` is not a printable key in this path. The tca8418 driver treats it as a
hardware layer selector. Pressing `Sym` by itself produces only a scan event;
pressing `Sym` with another key makes the driver emit the Sym-layer Linux
keycode for that physical key.

## Why The Console Keymap Is Not Enough

The original Cardputer keyboard support includes:

```text
tca8418_keypad_m5stack_keymap.map
```

That file is a Linux console keymap. It is suitable for a virtual terminal
loaded with `loadkeys`, but it is not read by Wayland, labwc, xkbcommon,
ZeroShell, LoFiBox, AppStore, the greeter, or the polkit prompt.

For the DRM/KMS + labwc stack, the same mapping intent has to exist as an XKB
profile. That is why this repository installs:

```text
/usr/share/X11/xkb/rules/cardputerzero
/usr/share/X11/xkb/keycodes/cardputerzero
/usr/share/X11/xkb/symbols/cardputerzero
```

The XKB symbols are the Wayland equivalent of the legacy console keymap. They
map the Sym-layer keycodes to printable characters such as `!`, `@`, `{`, `}`,
`|`, and `?`.

Linux event codes 195-199 overlap XKB's default fake modifier key names:

```text
195 -> <LVL5>
196 -> <ALT>
197 -> <META>
198 -> <SUPR>
199 -> <HYPR>
```

The Cardputer profile inherits the standard evdev table, but within the
Cardputer-only XKB rules it renames those five XKB keycodes to ordinary
`<I203>` through `<I207>` names. That keeps the Sym layer printable without
making `+`, `-`, `/`, `\`, or `{` also behave like Alt/Super/Level5 modifiers.
Pi OS's system `evdev` file is not edited.

## Session Scope

The Cardputer XKB profile is enabled only for the internal Zero labwc sessions:

```text
XKB_DEFAULT_RULES=cardputerzero
XKB_DEFAULT_MODEL=pc105
XKB_DEFAULT_LAYOUT=cardputerzero
```

These variables are exported by:

```text
/usr/local/bin/cardputer-zero-greeter-session
/usr/local/bin/cardputer-zero-labwc-session
```

and are also present in:

```text
/etc/xdg/cardputer-zero-greeter-labwc/environment
/etc/xdg/cardputer-zero-labwc/environment
```

HDMI LightDM/labwc is intentionally not changed by this profile.

## Ownership Boundary

This belongs in `cardputer-zero-os` because it describes how the base system
interprets the physical Cardputer keyboard. It must not be implemented in:

- ZeroShell,
- a terminal application,
- AppStore,
- LoFiBox,
- or any single user application.

Applications should receive normal Wayland text/key events. They should not
know that the symbol came from the Cardputer `Sym` hardware layer.
