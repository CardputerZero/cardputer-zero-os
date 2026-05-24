# cardputer-zero-os Specification

## Scope

`cardputer-zero-os` is the Cardputer Zero system profile for Raspberry Pi OS /
Debian.

In scope:

- internal-screen DRM/KMS display setup,
- internal-screen Wayland greeter,
- greetd/PAM authentication for existing users,
- real logind user session launch,
- labwc session configuration,
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

- runs a dedicated greetd instance on VT8,
- keeps the internal screen login independent from HDMI LightDM,
- restarts the internal login backend when needed.

### cardputer-zero-greeter-session

Path:

```text
/usr/local/bin/cardputer-zero-greeter-session
```

Role:

- starts labwc on `/dev/dri/cardputer-zero-internal` as `_greetd`,
- starts `zero-greeter-wayland`,
- exits when the greeter exits so greetd can proceed.

### zero-greeter-wayland

Path:

```text
/usr/local/bin/zero-greeter-wayland
```

Role:

- discover existing normal Linux users,
- render the 320x170 login UI as a Wayland client,
- accept password input,
- use greetd IPC through `GREETD_SOCK`,
- ask greetd to authenticate with PAM,
- ask greetd to start `cardputer-zero-session`.

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
- start `/opt/cardputer-zero-shell/bin/zero-shell-wayland`.

### zero-key-policy.service

Path:

```text
/etc/systemd/system/zero-key-policy.service
```

Role:

- run `/usr/local/bin/zero-key-policy` as a root-owned OS seat policy,
- listen to the Cardputer internal keyboard,
- implement global short/long Esc behavior when a foreground app has focus,
- keep the Zero internal session active on VT8.

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

- find the active Zero user session on `seat0` and `tty8`,
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
- create stable DRM symlinks for internal and HDMI cards.

## User Specification

The greeter shows existing interactive users:

- UID >= 1000,
- UID < 60000,
- home under `/home`,
- shell not matching `nologin` or `false`.

The profile does not create users.

## Security Specification

The profile must:

- authenticate through greetd/PAM,
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
