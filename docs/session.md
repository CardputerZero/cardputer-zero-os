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

The default shell target is:

```text
/opt/cardputer-zero-shell/bin/zero-shell
```

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

If the configured shell binary is missing, the session script starts the user's
login shell instead. This is a recovery path, not the normal desktop contract.
