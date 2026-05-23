# Session

`cardputer-zero-session` defines the post-login Cardputer Zero user session.

It belongs to `cardputer-zero-os` because the OS profile owns login handoff. It
does not implement the desktop itself.

## Environment

The session exports:

```text
XDG_SESSION_TYPE=wayland
XDG_CURRENT_DESKTOP=CardputerZero
XDG_SESSION_DESKTOP=CardputerZero
CARDPUTER_ZERO_SESSION=1
```

Then it reads:

```text
/etc/cardputer-zero/session.conf
```

## User Session Chain

```text
cardputer-zero-session
  -> cardputer-zero-labwc-session
  -> labwc on /dev/dri/cardputer-zero-internal
  -> /opt/cardputer-zero-shell/bin/zero-shell-wayland
```

`cardputer-zero-labwc-session` constrains wlroots/labwc with:

```text
WLR_DRM_DEVICES=/dev/dri/cardputer-zero-internal
WLR_BACKENDS=drm,libinput
WLR_RENDERER=pixman
```

## Boundary

`cardputer-zero-session` may choose the session wrapper and set session-level
environment. It must not become:

- launcher,
- settings UI,
- terminal UI,
- file manager,
- app store,
- root service for user apps.

Those belong to `cardputer-zero-shell` or separate user applications.

## Failure Behavior

If the labwc wrapper or ZeroShell binary is missing, the session exits and
greetd returns to the internal login flow. Recovery uses SSH, HDMI LightDM, and
system logs.
