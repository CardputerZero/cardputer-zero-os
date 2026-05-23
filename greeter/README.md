# zero-greeter-wayland

`zero-greeter-wayland` is the Cardputer Zero login UI for the internal
Wayland/labwc greeter session.

It is intentionally small:

- read existing normal users from the base OS,
- show a 320x170 Wayland GUI login surface,
- accept password input through the compositor keyboard focus,
- talk to greetd over `GREETD_SOCK`,
- let greetd own PAM, logind, seat, VT, and user-session creation,
- ask greetd to start `/usr/local/bin/cardputer-zero-session` after successful
  authentication.

It does not create users, store passwords, implement account policy, launch the
desktop as root, own display devices, or read input devices directly.

## Runtime Chain

```text
zero-greetd.service
  -> greetd --vt 8
  -> /usr/local/bin/cardputer-zero-greeter-session as _greetd
  -> labwc on /dev/dri/cardputer-zero-internal
  -> /usr/local/bin/zero-greeter-wayland
  -> greetd PAM authentication
  -> /usr/local/bin/cardputer-zero-session as authenticated user
```

## Build

The installer builds this binary with `wayland-scanner`, `libwayland-client`,
and `libxkbcommon`, then installs it to:

```text
/usr/local/bin/zero-greeter-wayland
```

## Runtime Keys

- `Tab`: user menu
- `Enter`: authenticate selected user
- `Esc`: power menu
