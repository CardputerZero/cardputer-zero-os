# ZeroShell Interface

本文档定义 `cardputer-zero-os` 与 `cardputer-zero-shell` 的接口。

## Summary

```text
cardputer-zero-os
  owns boot/login/session handoff

cardputer-zero-shell
  owns post-login GUI desktop
```

## Launch Path

After greetd/PAM authentication, greetd starts:

```text
/usr/local/bin/cardputer-zero-session
```

The session script then starts the Wayland/labwc session:

```text
/usr/local/bin/cardputer-zero-labwc-session
```

When ZeroShell has a Wayland backend, the labwc session starts:

```text
/opt/cardputer-zero-shell/bin/zero-shell-wayland
```

The Wayland shell executable can be overridden in:

```text
/etc/cardputer-zero/session.conf
```

using:

```sh
CARDPUTER_ZERO_WAYLAND_SHELL=/path/to/zero-shell-wayland
```

## Runtime Identity

The shell must run as the authenticated user.

Expected for the Wayland/labwc shell:

```text
pi  1234 /opt/cardputer-zero-shell/bin/zero-shell-wayland
```

Not acceptable:

```text
root  1234 /opt/cardputer-zero-shell/bin/zero-shell
```

greetd must launch the session as the authenticated user.

## Environment

`cardputer-zero-session` exports:

```text
XDG_SESSION_TYPE=wayland
XDG_CURRENT_DESKTOP=CardputerZero
XDG_SESSION_DESKTOP=CardputerZero
CARDPUTER_ZERO_SESSION=1
```

## No Framebuffer Fallback

If the Wayland shell is missing or not executable:

```text
/usr/local/bin/cardputer-zero-session
```

must not silently start the legacy direct-framebuffer shell or a login shell.
Recovery uses SSH or HDMI LightDM. This keeps Wayland/labwc failures visible instead
of entering the wrong graphics model.

## What OS Provides To Shell

`cardputer-zero-os` provides:

- authenticated user session,
- device permissions,
- APPLaunch directories can be populated by shell/app packages,
- `zero-helper`,
- internal-screen login handoff.

## What OS Does Not Provide

`cardputer-zero-os` does not provide:

- launcher home UI,
- application scanner,
- app store UI,
- file manager UI,
- terminal UI,
- settings UI,
- system monitor UI.

Those are shell/application responsibilities.
