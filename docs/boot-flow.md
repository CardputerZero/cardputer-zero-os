# Boot Flow

`cardputer-zero-os` owns the handoff from Raspberry Pi OS boot to the Cardputer
Zero internal-screen login and user session.

```text
systemd boot
  -> zero-hdmi-lightdm-policy.service
  -> zero-greetd.service opens a _greetd greeter session on seat-cardputer-zero
  -> zero-key-policy.service watches the internal keyboard seat policy
  -> cardputer-zero-greeter-session starts labwc on the internal DRM output
  -> zero-greeter-wayland shows the login UI
  -> zero-greeter-auth verifies the password through PAM
  -> cardputer-zero-session
  -> cardputer-zero-labwc-session
  -> labwc starts zero-shell-wayland as the authenticated user
```

## Responsibilities

`cardputer-zero-os` is responsible for:

- internal-screen DRM display setup,
- starting the internal greeter session,
- authenticating existing users through PAM,
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

The display paths are also separated at the logind seat layer:

```text
seat0
  -> HDMI vc4 DRM device
  -> LightDM / Pi OS desktop

seat-cardputer-zero
  -> internal ST7789 DRM device
  -> tca8418c keyboard
  -> Zero greeter / ZeroShell
```

This is required because only one session can be active on a logind seat at a
time. Without the split, the inactive compositor may expose only a wlroots
`NOOP-1` headless output.

`zero-greetd.service` is the internal-screen greeter backend. The name is kept
for upgrade compatibility, but the unit does not run greetd. systemd opens a
PAM/logind greeter session as `_greetd` on `seat-cardputer-zero`, then runs
`/usr/local/bin/cardputer-zero-greeter-session`.

`zero-greeter-wayland` is only the 320x170 UI. It calls
`/usr/local/libexec/cardputer-zero/zero-greeter-auth`, a restricted
`root:_greetd` helper, to perform PAM authentication and start the fixed
`cardputer-zero-session` as the authenticated user.

`zero-key-policy.service` is a root-owned OS seat policy. It listens only to
the Cardputer keyboard, implements global short/long Esc behavior, and
reactivates the Zero session through logind if needed. It calls
`zero-shell-control` as the authenticated user; it does not run ZeroShell or
apps as root.

The user session launched by `zero-greeter-auth` must run as the authenticated
user, not as root and not as `_greetd`.
