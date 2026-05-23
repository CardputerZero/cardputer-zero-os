# Boot Flow

`cardputer-zero-os` owns the userspace handoff from Raspberry Pi OS boot into
the Cardputer Zero login boundary.

```text
systemd boot
  -> zero-splash.service
  -> zero-greetd.service
  -> zero-greeter UI as greetd frontend
  -> greetd/PAM authenticate existing user
  -> cardputer-zero-session
  -> cardputer-zero-shell
```

## Responsibilities

`cardputer-zero-os` is responsible for:

- reducing visible default boot noise where practical,
- showing a userspace Zero splash on the internal screen,
- starting the Zero greeter on the internal screen,
- authenticating existing users through PAM,
- opening a real user session,
- and launching the Cardputer Zero shell as that user.

It is not responsible for:

- creating OS users,
- replacing Raspberry Pi firmware output,
- replacing all early kernel output,
- autologin,
- or implementing the post-login desktop UI.

## systemd Units

`zero-splash.service` runs early in userspace and leaves a simple branded
startup frame on the Cardputer Zero internal framebuffer.

`zero-greetd.service` starts a separate internal-screen greetd instance on the
Zero login VT. Its greeter command is `/usr/local/bin/zero-greeter`, which
renders the internal-screen GUI and talks to greetd over `GREETD_SOCK`.

The legacy self-managed `zero-greeter.service` unit is not part of the current
architecture. Keeping only `zero-greetd.service` avoids two competing login
backends for the same internal-screen UI.

It does not replace Pi OS `lightdm` on HDMI. The session launched by greetd
must run as the authenticated user.
