# ZeroShell Interface

This document defines the interface between `cardputer-zero-os` and
`cardputer-zero-shell`.

## Summary

```text
cardputer-zero-os
  -> owns login, session, compositor startup, privilege policy

cardputer-zero-shell
  -> owns post-login launcher and task UI
```

## Launch Path

After greetd/PAM authentication:

```text
/usr/local/bin/cardputer-zero-session
  -> /usr/local/bin/cardputer-zero-labwc-session
  -> labwc -S /opt/cardputer-zero-shell/bin/zero-shell-wayland
```

The shell path can be overridden in:

```text
/etc/cardputer-zero/session.conf
```

using:

```sh
CARDPUTER_ZERO_WAYLAND_SHELL=/path/to/zero-shell-wayland
```

## Runtime Identity

The shell must run as the authenticated user:

```text
pi  1234 /opt/cardputer-zero-shell/bin/zero-shell-wayland
```

## Environment

`cardputer-zero-session` exports:

```text
XDG_SESSION_TYPE=wayland
XDG_CURRENT_DESKTOP=CardputerZero
XDG_SESSION_DESKTOP=CardputerZero
CARDPUTER_ZERO_SESSION=1
```

The labwc wrapper exports:

```text
WLR_DRM_DEVICES=/dev/dri/cardputer-zero-internal
WLR_BACKENDS=drm,libinput
WLR_RENDERER=pixman
```

## OS Provides

- authenticated user session,
- Wayland compositor on the internal DRM output,
- device permissions,
- APPLaunch data directories,
- `zero-shell-control`,
- root-owned `zero-key-policy.service` for global Esc and VT8 policy,
- `zero-helper`,
- polkit agent.

## OS Does Not Provide

- launcher home UI,
- application scanner,
- app store UI,
- file manager UI,
- terminal UI,
- settings UI,
- system monitor UI.

Those are shell or application responsibilities.
