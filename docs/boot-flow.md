# Boot Flow

`cardputer-zero-os` owns the userspace handoff from Raspberry Pi OS boot into
the Cardputer Zero login boundary.

```text
systemd boot
  -> zero-splash.service
  -> zero-greeter.service
  -> PAM authenticate existing user
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

`zero-greeter.service` renders the internal-screen GUI greeter and restarts if
the greeter exits. It does not replace Pi OS `lightdm` on HDMI.

The greeter itself may run as root because it is a login component. The session
it launches must run as the authenticated user.
