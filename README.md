# cardputer-zero-os

`cardputer-zero-os` is the Cardputer Zero system profile for Raspberry Pi OS /
Debian.

It is not the desktop, launcher, app store, settings UI, file browser, terminal,
or app UI. It owns the internal-screen login and session plumbing that lets the
real post-login shell run as a normal authenticated Linux user.

## Boundary

```text
Raspberry Pi OS / Debian base
  -> Raspberry Pi Imager creates normal Linux users
  -> cardputer-zero-os owns internal-screen login/session/display policy
  -> cardputer-zero-shell owns the post-login launcher/task UI
  -> applications own their own windows and domain UI
```

`cardputer-zero-os` does not create a fixed `zero` user, does not store
passwords, does not enable autologin, and does not replace the normal HDMI Pi OS
desktop path.

## Current UI

![Cardputer Zero OS greeter](docs/assets/zero-os-current-greeter-320x170.png)

The greeter is the pre-login system entry for the internal screen. It is not the
post-login shell.

## Internal-Screen Architecture

The internal screen uses one graphics path:

```text
internal ST7789 display
  -> panel-mipi-dbi-spi
  -> DRM/KMS connector
  -> labwc compositor
  -> Wayland greeter or Wayland/Xwayland user apps
```

The profile intentionally puts login, session, windows, and privilege prompts
back on standard Linux mechanisms:

- `greetd` creates the login flow and user session.
- PAM authenticates existing Linux users.
- logind owns session activation.
- labwc owns the internal screen compositor session.
- ZeroShell runs as a Wayland client after login.
- Apps create Wayland or Xwayland windows.
- polkit handles privileged authorization prompts.

There is no alternate internal display backend in this repository. If the
standard path fails, it must fail visibly and be fixed through SSH, HDMI, or
service logs.

## Boot And Login Timing

```text
systemd boot
  -> zero-hdmi-lightdm-policy.service keeps HDMI LightDM separate
  -> zero-greetd.service starts greetd on VT8
  -> greetd runs cardputer-zero-greeter-session as _greetd
  -> cardputer-zero-greeter-session starts labwc on /dev/dri/cardputer-zero-internal
  -> zero-greeter-wayland shows the 320x170 login UI
  -> user selects an existing Linux account and enters a password
  -> zero-greeter-wayland talks to greetd through GREETD_SOCK
  -> greetd performs PAM authentication and opens the user session
  -> greetd starts cardputer-zero-session as that user
  -> cardputer-zero-session starts cardputer-zero-labwc-session
  -> labwc starts /opt/cardputer-zero-shell/bin/zero-shell-wayland
```

Expected process shape:

```text
_greetd  labwc -C /etc/xdg/cardputer-zero-greeter-labwc
_greetd  /usr/local/bin/zero-greeter-wayland
pi       labwc -C /etc/xdg/cardputer-zero-labwc -S /opt/cardputer-zero-shell/bin/zero-shell-wayland
pi       /opt/cardputer-zero-shell/bin/zero-shell-wayland
root     python3 /usr/local/bin/zero-key-policy
```

Not acceptable:

```text
root     /opt/cardputer-zero-shell/bin/zero-shell-wayland
root     user applications
```

## Linux Mechanisms Used

### Raspberry Pi Imager And Users

Users are owned by the base OS. Raspberry Pi Imager or normal Linux tools create
the account, password, WiFi, SSH settings, and groups.

The greeter discovers ordinary users from `/etc/passwd`:

- normal user UID range,
- home directory under `/home`,
- shell is not `nologin` or `false`.

### greetd, PAM, And logind

The greeter is a small Wayland UI. It does not authenticate passwords itself.
It sends login requests to greetd. greetd runs PAM and creates the logind
session, which gives the user compositor the correct active session and device
access.

```text
zero-greeter-wayland
  -> greetd IPC
  -> PAM
  -> logind user session
  -> cardputer-zero-session
```

### Internal DRM Display Overlay

The base `cardputerzero-overlay.dtbo` remains responsible for non-display
hardware such as keyboard, audio, M5 IO expander, sensors, and backlight.

The added display overlay is:

```text
/boot/firmware/overlays/cardputerzero-kms-display.dtbo
```

It disables the original `st7789v@0` display node and adds a
`panel-mipi-dbi-spi` `display@0` node on SPI0 CE0.

The panel firmware is:

```text
/lib/firmware/cardputerzero,st7789v.bin
```

It contains the ST7789 command stream used by Linux `panel-mipi-dbi`. The user
view is 320x170 in landscape orientation with `MADCTL = MV | MY = 0xa0` and the
35-pixel glass offset.

The generic panel module is loaded explicitly through:

```text
/etc/modules-load.d/cardputer-zero-kms.conf
```

### HDMI Separation

HDMI remains a normal Pi OS / LightDM / recovery surface. The Zero greeter is
only for the internal screen.

Stable DRM symlinks:

```text
/dev/dri/cardputer-zero-internal
/dev/dri/cardputer-zero-hdmi
```

The internal sessions set:

```text
WLR_DRM_DEVICES=/dev/dri/cardputer-zero-internal
WLR_BACKENDS=drm,libinput
WLR_RENDERER=pixman
```

The HDMI LightDM policy constrains Pi OS greeter/desktop work to the HDMI DRM
device when HDMI is present.

### labwc Window Ownership

labwc is the compositor/window manager for the internal screen. Task identity is
a compositor window, not a process id. That is why launchable apps must be
Wayland or Xwayland clients.

### Global Tab/Esc Policy

When a foreground app has keyboard focus, ZeroShell cannot receive global key
events. `cardputer-zero-os` therefore owns the device-level key policy.

`zero-key-policy.service` runs as root because it may need to reactivate the
Zero logind session on VT8. That is a Linux seat/console operation, not a
launcher operation. When it needs to tell ZeroShell what to do, it drops back
to the authenticated user and calls `zero-shell-control` inside that user's
Wayland runtime.

```text
Tab
  -> zero-shell-control tasks
  -> ZeroShell toggles running tasks

short Esc
  -> zero-shell-control minimize-active
  -> ZeroShell is focused fullscreen and the foreground app remains a task

long Esc
  -> zero-shell-control close-active
  -> labwc asks active window to close and focuses ZeroShell
```

If the active virtual terminal ever slips away from VT8, `zero-key-policy`
reactivates the Zero session through `loginctl activate <session>` and falls
back to `chvt 8` only for that fixed internal-screen VT. It does not expose a
general VT switcher or arbitrary root command path.

### polkit

Privileged operations go through polkit:

```text
user app or shell
  -> pkexec /usr/local/sbin/zero-helper <allowed action>
  -> polkit checks org.cardputerzero.zero-helper.*
  -> zero-polkit-agent opens the Zero-sized Wayland password prompt if needed
```

`zero-helper` is restricted. It does not provide arbitrary shell, arbitrary
`systemctl`, arbitrary package-manager, or arbitrary process-kill access.

## File Map

| File | Role |
| --- | --- |
| `install.sh` | Installs the OS profile, builds greeter/polkit tools, installs internal DRM display setup, enables Zero services. |
| `uninstall.sh` | Removes installed profile files while leaving Linux users intact. |
| `files/etc/systemd/system/zero-greetd.service` | Internal-screen greetd service on VT8. |
| `files/etc/systemd/system/zero-key-policy.service` | Root-owned internal keyboard and VT policy service. |
| `files/etc/systemd/system/zero-hdmi-lightdm-policy.service` | Keeps HDMI LightDM independent from the internal screen. |
| `files/etc/greetd/cardputer-zero.toml` | greetd config that runs `cardputer-zero-greeter-session` as `_greetd`. |
| `files/usr/local/bin/cardputer-zero-greeter-session` | Starts a small labwc greeter session and runs `zero-greeter-wayland`. |
| `greeter/zero-greeter-wayland.cpp` | 320x170 Wayland greeter UI and greetd IPC client. |
| `files/usr/local/bin/cardputer-zero-session` | Post-auth session handoff script. |
| `files/usr/local/bin/cardputer-zero-labwc-session` | Starts user labwc on the internal DRM output and then starts ZeroShell. |
| `files/etc/cardputer-zero/session.conf` | Paths and session policy for the internal Wayland session. |
| `files/etc/xdg/cardputer-zero-greeter-labwc/*` | labwc config for the pre-login greeter session. |
| `files/etc/xdg/cardputer-zero-labwc/*` | labwc config and user-session autostart. |
| `files/usr/local/bin/zero-shell-control` | Command bridge used by labwc and key policy to control tasks. |
| `files/usr/local/bin/zero-key-policy` | Narrow Cardputer keyboard policy for short/long Esc and VT8 reactivation. |
| `files/usr/local/sbin/zero-helper` | Restricted privileged helper. |
| `polkit-agent/zero-polkit-agent.c` | Wayland-only polkit authentication agent. |
| `polkit-agent/zero-polkit-prompt-wayland.cpp` | Zero-sized Wayland password prompt. |
| `files/etc/udev/rules.d/99-cardputer-zero.rules` | Device groups, permissions, and DRM symlinks. |
| `scripts/setup-internal-drm-display.sh` | Installs the internal DRM display overlay and panel firmware. |
| `scripts/build-st7789v-panel-firmware.sh` | Generates the ST7789 `panel-mipi-dbi` firmware file. |
| `scripts/probe-graphics-stack.sh` | Prints DRM, SPI, overlay, module, and labwc facts. |
| `scripts/probe-labwc-session.sh` | Inspects labwc/logind/session state. |
| `scripts/test-labwc-internal-session.sh` | One-shot internal labwc smoke test. |

## Install

```sh
sudo apt-get install \
  build-essential pkg-config greetd wlrctl labwc wayland-protocols \
  device-tree-compiler libglib2.0-dev libpolkit-agent-1-dev \
  libpolkit-gobject-1-dev libwayland-dev libxkbcommon-dev

sudo ./install.sh
sudo reboot
```

The installer writes a backup of boot display files under:

```text
/var/backups/cardputer-zero-os/internal-drm-display/
```

## Verify

```sh
systemctl is-enabled zero-greetd.service
systemctl status zero-greetd.service
ls -l /dev/dri/cardputer-zero-internal /dev/dri/cardputer-zero-hdmi
ps -eo user,pid,args | grep -E 'zero-greeter-wayland|zero-shell-wayland|labwc|zero-key-policy'
XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0 wlrctl toplevel list
```

Expected examples:

```text
cardputer-zero-shell: Cardputer Zero Shell
lofibox: LoFiBox Zero
```

## Recovery

Use SSH or HDMI LightDM as the recovery surface. Check:

```sh
journalctl -b -u zero-greetd.service --no-pager
cat /tmp/cardputer-zero-greeter-session.log
```

Disable the internal greeter service if needed:

```sh
sudo systemctl disable --now zero-greetd.service
```

Recovery is explicit. The internal-screen session does not silently switch to
another graphics backend.

## Relationship To ZeroShell

`cardputer-zero-os` starts ZeroShell; it does not implement the launcher.

```text
cardputer-zero-os
  -> creates authenticated user session
  -> starts labwc on /dev/dri/cardputer-zero-internal
  -> starts /opt/cardputer-zero-shell/bin/zero-shell-wayland

cardputer-zero-shell
  -> scans /usr/share/APPLaunch/applications/*.desktop
  -> draws launcher/task UI as a Wayland client
  -> launches Wayland/Xwayland applications
```

## Documentation Index

- [docs/intent.md](docs/intent.md)
- [docs/boot-flow.md](docs/boot-flow.md)
- [docs/greeter.md](docs/greeter.md)
- [docs/session.md](docs/session.md)
- [docs/permissions.md](docs/permissions.md)
- [docs/zero-helper.md](docs/zero-helper.md)
- [docs/polkit.md](docs/polkit.md)
- [docs/recovery.md](docs/recovery.md)
- [docs/install.md](docs/install.md)
- [docs/kms-labwc.md](docs/kms-labwc.md)
- [docs/zero-shell-interface.md](docs/zero-shell-interface.md)
- [docs/spec.md](docs/spec.md)
