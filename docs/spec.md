# cardputer-zero-os Specification

## Scope

`cardputer-zero-os` is the Cardputer Zero system profile for Raspberry Pi OS /
Debian.

In scope:

- internal-screen DRM/KMS display setup,
- internal-screen Wayland greeter,
- PAM authentication for existing users,
- real logind user session launch,
- labwc session configuration,
- internal-screen idle policy wiring through standard Wayland tools,
- internal keyboard XKB profile,
- global Zero keyboard policy,
- device permission setup,
- restricted privileged helper,
- Wayland polkit authorization prompt,
- SSH/HDMI recovery surface preservation,
- quiet boot tuning where practical.

Out of scope:

- creating users,
- autologin,
- replacing Raspberry Pi Imager,
- replacing HDMI login,
- post-login launcher UI,
- app scanner,
- app store UI,
- file manager UI,
- terminal UI,
- settings UI,
- system monitor UI.

## Components

### zero-greetd.service

Path:

```text
/etc/systemd/system/zero-greetd.service
```

Role:

- opens a `_greetd` PAM/logind greeter session on `seat-cardputer-zero`,
- keeps the internal screen login independent from HDMI LightDM,
- starts `cardputer-zero-greeter-session`,
- exits after successful login so the authenticated user session owns the
  internal screen.

### cardputer-zero-greeter-session

Path:

```text
/usr/local/bin/cardputer-zero-greeter-session
```

Role:

- starts labwc on `/dev/dri/cardputer-zero-internal` as `_greetd`,
- starts `zero-greeter-wayland`,
- exits when the greeter exits so systemd can finish the greeter service.

### zero-greeter-wayland

Path:

```text
/usr/local/bin/zero-greeter-wayland
```

Role:

- discover existing normal Linux users,
- render the 320x170 login UI as a Wayland client,
- accept password input,
- call the restricted `zero-greeter-auth` helper,
- exit after `zero-greeter-auth` successfully starts the user session.

### zero-greeter-auth

Path:

```text
/usr/local/libexec/cardputer-zero/zero-greeter-auth
```

Role:

- run as `root:_greetd` with mode `4750`,
- accept only a username/password request from the greeter over stdin,
- validate that the selected account is an existing normal Linux user,
- authenticate through PAM service `cardputer-zero-login`,
- start the fixed `cardputer-zero-session` with
  `systemd-run --property=User=<user> --property=PAMName=cardputer-zero-session`.

Non-role:

- arbitrary command execution,
- arbitrary root shell,
- app launching,
- password storage.

Non-role:

- user database,
- password storage,
- root desktop launcher,
- post-login shell,
- HDMI display manager.

### cardputer-zero-session

Path:

```text
/usr/local/bin/cardputer-zero-session
```

Role:

- set Cardputer Zero session identity,
- run `cardputer-zero-labwc-session`,
- fail clearly if the labwc session wrapper is missing.

### cardputer-zero-labwc-session

Path:

```text
/usr/local/bin/cardputer-zero-labwc-session
```

Role:

- constrain wlroots/labwc to `/dev/dri/cardputer-zero-internal`,
- set `XDG_SESSION_TYPE=wayland`,
- set `XDG_SEAT=seat-cardputer-zero`,
- start `/opt/cardputer-zero-shell/bin/zero-shell-wayland`.

### cardputer-zero-idle

Path:

```text
/usr/local/bin/cardputer-zero-idle
```

Role:

- run inside the authenticated internal Wayland session,
- read `~/.config/cardputer-zero/session/display-power.json`,
- start the standard `swayidle` daemon with `wlopm` off/on commands,
- keep the screen on when the user selects `never`,
- turn the output back on when the helper exits.

Non-role:

- custom power-manager implementation,
- direct input-device ownership,
- root-only display policy,
- HDMI desktop power policy.

### zero-key-policy.service

Path:

```text
/etc/systemd/system/zero-key-policy.service
```

Role:

- run `/usr/local/bin/zero-key-policy` as a root-owned OS seat policy,
- listen to the Cardputer internal keyboard,
- implement global short/long Esc behavior when a foreground app has focus,
- keep the Zero internal seat session active through logind.

Non-role:

- app launcher,
- arbitrary key remapper,
- arbitrary VT switcher,
- root shell command bridge.

### zero-key-policy

Path:

```text
/usr/local/bin/zero-key-policy
```

Role:

- find the active Zero user session on `seat-cardputer-zero`,
- reactivate that session through logind if the visible VT slips away,
- call `zero-shell-control` as the authenticated user, not as root.

### Cardputer XKB Profile

Paths:

```text
/usr/share/X11/xkb/rules/cardputerzero
/usr/share/X11/xkb/keycodes/cardputerzero
/usr/share/X11/xkb/symbols/cardputerzero
```

Role:

- provide the global Wayland keyboard profile for the internal tca8418c
  keyboard,
- preserve the legacy `tca8418_keypad_m5stack_keymap.map` Sym-layer character
  mapping in XKB form,
- make `Sym` combinations work for every internal Wayland client.

Non-role:

- per-application key handling,
- shell-specific terminal input,
- Linux console keymap loading,
- HDMI keyboard policy.

### zero-helper

Path:

```text
/usr/local/sbin/zero-helper
```

Role:

- provide narrow root actions authorized through polkit.

Non-role:

- arbitrary shell,
- arbitrary `sudo`,
- arbitrary `systemctl`,
- arbitrary package-manager wrapper,
- arbitrary process killer.

### zero-polkit-agent

Path:

```text
/usr/local/bin/zero-polkit-agent
```

Role:

- register as the polkit authentication agent for the active Zero user session,
- open `zero-polkit-prompt-wayland` when authorization is needed.

### udev Rules

Path:

```text
/etc/udev/rules.d/99-cardputer-zero.rules
```

Role:

- assign device groups,
- tag input/render/video/audio/GPIO/SPI/I2C access,
- create stable DRM symlinks for internal and HDMI cards,
- assign the internal ST7789 DRM card and tca8418c keyboard to
  `seat-cardputer-zero`,
- leave HDMI on `seat0` for normal Pi OS LightDM.

## User Specification

The greeter shows existing interactive users:

- UID >= 1000,
- UID < 60000,
- home under `/home`,
- shell not matching `nologin` or `false`.

The profile does not create users.

## Security Specification

The profile must:

- authenticate through PAM,
- open a real logind session,
- run ZeroShell as the authenticated user,
- run user apps as that user,
- route privileged actions through `pkexec` and polkit,
- restrict helper actions to explicit subcommands.

The profile must not:

- store passwords,
- implement password policy,
- autologin,
- run ZeroShell as root,
- grant arbitrary sudo,
- install passwordless helper sudoers entries,
- launch arbitrary root commands from UI.

## Display Specification

The internal screen is exposed as a DRM/KMS output and owned by labwc.

Required runtime environment:

```text
WLR_DRM_DEVICES=/dev/dri/cardputer-zero-internal
WLR_BACKENDS=drm,libinput
WLR_RENDERER=pixman
XKB_DEFAULT_RULES=cardputerzero
XKB_DEFAULT_MODEL=pc105
XKB_DEFAULT_LAYOUT=cardputerzero
```

HDMI remains separate and available for Pi OS login/recovery.

## Failure Specification

Internal-screen failures must be visible through service logs and recovery
surfaces. The profile does not silently switch to another internal display
backend or start a different shell.

Recovery surfaces:

- SSH,
- HDMI LightDM,
- systemd service control,
- boot file backups under `/var/backups/cardputer-zero-os`.
