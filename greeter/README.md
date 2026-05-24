# zero-greeter-wayland

`zero-greeter-wayland` is the Cardputer Zero login UI for the internal
Wayland/labwc greeter session.

It is intentionally small:

- read existing normal users from the base OS,
- show a 320x170 Wayland GUI login surface,
- accept password input through the compositor keyboard focus,
- call the restricted `zero-greeter-auth` helper for PAM authentication,
- exit after the helper has started the authenticated Zero user session.

It does not create users, store passwords, implement account policy, launch the
desktop as root, own display devices, or read input devices directly.

## Runtime Chain

```text
zero-greetd.service
  -> systemd/PAM opens a _greetd logind greeter session
  -> /usr/local/bin/cardputer-zero-greeter-session
  -> labwc on /dev/dri/cardputer-zero-internal
  -> /usr/local/bin/zero-greeter-wayland
  -> /usr/local/libexec/cardputer-zero/zero-greeter-auth
  -> PAM service cardputer-zero-login
  -> /usr/local/bin/cardputer-zero-session as authenticated user
```

The `zero-greetd.service` unit name is historical. The unit does not run the
`greetd` daemon. It is kept so older installs and operator muscle memory still
refer to the same internal-screen greeter service.

## Authentication Helper

`zero-greeter-auth` is installed as `root:_greetd` with mode `4750`. Only the
greeter session can execute it. It accepts the selected username and password
over stdin, validates that the selected account is a normal Linux user,
authenticates with PAM, and starts the fixed `cardputer-zero-session` through
systemd.

It does not accept arbitrary commands and must not become a root command bridge.

## Build

The installer builds `zero-greeter-wayland` with `wayland-scanner`,
`libwayland-client`, and `libxkbcommon`, then installs it to:

```text
/usr/local/bin/zero-greeter-wayland
```

The installer also builds the authentication helper and installs it to:

```text
/usr/local/libexec/cardputer-zero/zero-greeter-auth
```

## Runtime Keys

- `Tab`: user menu
- `Enter`: authenticate selected user
- `Esc`: power menu
