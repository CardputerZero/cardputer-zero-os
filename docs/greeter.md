# Greeter

`zero-greeter-wayland` is the internal-screen login UI.

It is not a launcher and not a standalone display manager. Its job ends when the
authenticated user session has been started.

## Runtime Chain

```text
zero-greetd.service
  -> systemd/PAM opens a _greetd greeter session on seat-cardputer-zero
  -> cardputer-zero-greeter-session
  -> labwc on /dev/dri/cardputer-zero-internal
  -> zero-greeter-wayland
  -> zero-greeter-auth
  -> PAM authentication
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

The greeter communicates with `/usr/local/libexec/cardputer-zero/zero-greeter-auth`.

The helper owns:

- PAM authentication,
- PAM account checks,
- session creation,
- logind session activation,
- starting `/usr/local/bin/cardputer-zero-session`.

The helper is installed as `root:_greetd` with mode `4750`. The greeter can
execute it, but ordinary users cannot. It does not accept arbitrary commands.

The greeter does not store passwords, does not read `/etc/shadow`, and does not
implement account policy.

## Rendering

The greeter is a Wayland client running inside a small labwc greeter session.
It receives keyboard input through the compositor and draws the 320x170 login
surface through Wayland buffers.

## Visual System

The greeter follows the shared Zero visual system documented in
[`zero-visual-system.md`](zero-visual-system.md). UI behavior is described in
[`greeter-ui.md`](greeter-ui.md).
