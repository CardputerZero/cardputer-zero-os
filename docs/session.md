# Session

`cardputer-zero-session` defines the post-login Cardputer Zero session.

It belongs to `cardputer-zero-os` because it defines what the OS profile starts
after login. It does not implement the desktop itself.

## Default Environment

The session script exports:

```text
XDG_SESSION_TYPE=tty
XDG_CURRENT_DESKTOP=CardputerZero
XDG_SESSION_DESKTOP=CardputerZero
CARDPUTER_ZERO_SESSION=1
```

It then reads:

```text
/etc/cardputer-zero/session.conf
```

The legacy framebuffer shell target is:

```text
/opt/cardputer-zero-shell/bin/zero-shell
```

The default session mode is:

```text
CARDPUTER_ZERO_SESSION_MODE="labwc"
```

This starts the internal Wayland/labwc session. The Zero internal display is
owned by labwc/wlroots, and post-login applications must become
compositor-managed Wayland/Xwayland clients.

## Labwc Mode

Wayland/labwc is selected through:

```text
CARDPUTER_ZERO_SESSION_MODE="labwc"
CARDPUTER_ZERO_LABWC_SESSION="/usr/local/bin/cardputer-zero-labwc-session"
```

This mode starts:

```text
/usr/local/bin/cardputer-zero-labwc-session
```

which constrains labwc to the Zero internal DRM device:

```text
WLR_DRM_DEVICES=/dev/dri/cardputer-zero-internal
WLR_BACKENDS=drm
WLR_RENDERER=pixman
```

This mode is the current intended direction. It expects
`/opt/cardputer-zero-shell/bin/zero-shell-wayland` and graphical apps that create
compositor-managed Wayland/Xwayland windows.

The direct-framebuffer shell remains an explicit legacy compatibility mode:

```text
CARDPUTER_ZERO_SESSION_MODE="framebuffer"
```

It is not used as an automatic fallback. If the labwc wrapper is missing or an
unknown session mode is configured, the session exits and greetd returns to the
login screen.

## Boundary

`cardputer-zero-session` may choose the shell binary and set session-level
environment. It must not grow into:

- a launcher,
- a settings UI,
- a terminal UI,
- a file manager,
- an app store,
- or a root service that starts user applications.

Those belong to `cardputer-zero-shell` or other user-space applications.

## Recovery Fallback

The current Wayland/labwc policy does not fall back into the framebuffer shell or a
login shell after authentication. Recovery should use SSH or HDMI LightDM. This
keeps failures visible instead of silently entering the wrong graphics model.
