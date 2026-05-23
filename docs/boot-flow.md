# Boot Flow

`cardputer-zero-os` owns the handoff from Raspberry Pi OS boot to the Cardputer
Zero internal-screen login and user session.

```text
systemd boot
  -> zero-hdmi-lightdm-policy.service
  -> zero-greetd.service on VT8
  -> zero-key-policy.service watches the internal keyboard and VT8 policy
  -> greetd starts cardputer-zero-greeter-session as _greetd
  -> cardputer-zero-greeter-session starts labwc on the internal DRM output
  -> zero-greeter-wayland shows the login UI
  -> greetd/PAM authenticates an existing Linux user
  -> cardputer-zero-session
  -> cardputer-zero-labwc-session
  -> labwc starts zero-shell-wayland as the authenticated user
```

## Responsibilities

`cardputer-zero-os` is responsible for:

- internal-screen DRM display setup,
- starting the internal greeter session,
- authenticating existing users through greetd/PAM,
- opening a real logind user session,
- launching the Cardputer Zero labwc session as that user.

It is not responsible for:

- creating users,
- autologin,
- implementing the post-login desktop UI,
- replacing HDMI LightDM.

## systemd Units

`zero-hdmi-lightdm-policy.service` keeps the regular Pi OS HDMI display-manager
path independent from the internal screen.

`zero-greetd.service` starts a dedicated greetd instance on VT8. The service
uses `/etc/greetd/cardputer-zero.toml`, whose default session is
`/usr/local/bin/cardputer-zero-greeter-session` as `_greetd`.

`zero-key-policy.service` is a root-owned OS seat policy. It listens only to
the Cardputer keyboard, implements global short/long Esc behavior, and
reactivates the Zero VT8 session if the kernel console becomes active. It calls
`zero-shell-control` as the authenticated user; it does not run ZeroShell or
apps as root.

The user session launched by greetd must run as the authenticated user, not as
root and not as `_greetd`.
