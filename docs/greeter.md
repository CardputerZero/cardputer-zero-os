# Greeter

`zero-greeter-wayland` is the internal-screen login UI.

It is a greetd frontend, not a launcher and not a standalone display manager.
Its job ends when greetd starts the authenticated user session.

## Runtime Chain

```text
zero-greetd.service
  -> greetd --vt 8
  -> cardputer-zero-greeter-session as _greetd
  -> labwc on /dev/dri/cardputer-zero-internal
  -> zero-greeter-wayland
  -> greetd PAM authentication
  -> cardputer-zero-session as authenticated user
```

## User Discovery

The greeter reads users from the base OS account database and displays normal
interactive users:

- UID is at least `1000`,
- UID is below `60000`,
- home directory is under `/home/`,
- shell is not `nologin` or `false`.

It does not create users.

## Authentication

The greeter communicates with greetd through `GREETD_SOCK`.

greetd owns:

- PAM authentication,
- PAM account checks,
- session creation,
- logind session activation,
- starting `/usr/local/bin/cardputer-zero-session`.

The greeter does not store passwords and does not implement account policy.

## Rendering

The greeter is a Wayland client running inside a small labwc greeter session.
It receives keyboard input through the compositor and draws the 320x170 login
surface through Wayland buffers.

## Visual System

The greeter follows the shared Zero visual system documented in
[`zero-visual-system.md`](zero-visual-system.md). UI behavior is described in
[`greeter-ui.md`](greeter-ui.md).
