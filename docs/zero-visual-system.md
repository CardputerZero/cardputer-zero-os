# Zero Visual System

`cardputer-zero-os` and `cardputer-zero-shell` share one small-screen visual
language. The OS greeter uses it as a login surface; the shell uses it as the
post-login desktop. Shared style must not blur those responsibilities.

## Palette

| Token | Hex | Use |
| --- | --- | --- |
| Zero Paper | `#E9E4D5` | screen background |
| Panel Cream | `#F4F0E6` | bars, panels, popups |
| Task Button | `#EFE8D9` | bottom-left action segment |
| Icon Well | `#F8F4EA` | input fields and inset wells |
| Ink Black | `#171717` | primary text and hard outlines |
| Line Black | `#2A2A2A` | panel borders |
| Muted Text | `#6E6A61` | labels and secondary hints |
| Soft Line | `#BBB19E` | inactive borders |
| Grid Dot | `#C9C1AE` | background device-grid texture |
| Accent Orange | `#E66A2C` | focus, current user, active affordance |
| OK Green | `#3A7D44` | healthy battery or success |
| Warn Red | `#B94A2C` | authentication failure and destructive action |
| Hard Shadow | `#BDB5A4` | 3px to 4px hard offset shadow |

## Geometry

The internal display target is `320x170`.

Greeter layout:

```text
TopBar:     y=0   h=20
Center:     y=20  h=130
BottomBar:  y=150 h=20
```

The greeter keeps the BottomBar the same height as the TopBar so OS login and
ZeroShell share the same chrome rhythm on the internal display.

## Greeter Semantics

TopBar:

- left: current time,
- center accent: login-stage indicator,
- right: `LOGIN`, battery percentage, battery glyph.

Center:

- one hard-edged login panel,
- `CARDPUTER ZERO` title,
- `GUI LOGIN` or `SELECT USER` state label,
- existing system user field,
- password field,
- PAM status line.

BottomBar:

- left segment: `POWER`,
- right hints: `TAB USER`, `ENTER LOGIN`, `ESC`.

Popups:

- user selector and power menu use a hard shadow,
- popup header is Ink Black with Panel Cream text,
- selected row uses Accent Orange except destructive shutdown, which uses Warn Red.

## Boundary

The OS greeter does not show:

- application cards,
- task counts,
- app scanner state,
- file manager, terminal, settings, or app store UI.

Those belong to `cardputer-zero-shell` after PAM login and session handoff.
