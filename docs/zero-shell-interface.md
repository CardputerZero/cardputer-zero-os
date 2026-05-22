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

After PAM authentication, `zero-greeter` execs:

```text
/usr/local/bin/cardputer-zero-session
```

The session script then execs:

```text
/opt/cardputer-zero-shell/bin/zero-shell
```

The shell path can be overridden in:

```text
/etc/cardputer-zero/session.conf
```

using:

```sh
CARDPUTER_ZERO_SHELL=/path/to/shell
```

## Runtime Identity

The shell must run as the authenticated user.

Expected:

```text
pi  1234 /opt/cardputer-zero-shell/bin/zero-shell
```

Not acceptable:

```text
root  1234 /opt/cardputer-zero-shell/bin/zero-shell
```

`zero-greeter` must drop privileges before launching the session.

## Environment

`cardputer-zero-session` exports:

```text
XDG_SESSION_TYPE=tty
XDG_CURRENT_DESKTOP=CardputerZero
XDG_SESSION_DESKTOP=CardputerZero
CARDPUTER_ZERO_SESSION=1
```

## Shell Missing Fallback

If the configured shell is missing or not executable:

```text
/usr/local/bin/cardputer-zero-session
```

starts the user's login shell.

This is a recovery path, not the normal desktop contract.

## What OS Provides To Shell

`cardputer-zero-os` provides:

- authenticated user session,
- device permissions,
- APPLaunch path can be populated by shell/app packages,
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

