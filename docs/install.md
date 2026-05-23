# Install Guide

本文档说明如何安装、卸载和验证 `cardputer-zero-os`。

## Install

On the target Raspberry Pi OS / Debian system:

```sh
sudo ./install.sh
```

The installer requires:

- a C compiler,
- PAM development headers for building `zero-greeter`,
- polkit development headers for building `zero-polkit-agent`,
- greetd for the internal-screen login backend,
- systemd for service enablement on live installs.

On Raspberry Pi OS / Debian:

```sh
sudo apt-get install build-essential pkg-config greetd libpam0g-dev libglib2.0-dev libpolkit-agent-1-dev libpolkit-gobject-1-dev
```

The installer does not install packages by itself.

## Installed Files

The install script installs:

```text
/usr/local/bin/zero-splash
/usr/local/bin/zero-greeter
/usr/local/bin/cardputer-zero-session
/usr/local/bin/cardputer-zero-labwc-session
/usr/local/bin/zero-polkit-agent
/usr/local/sbin/zero-helper
/etc/pam.d/zero-greeter
/etc/systemd/system/zero-splash.service
/etc/systemd/system/zero-greetd.service
/etc/systemd/user/zero-polkit-agent.service
/etc/tmpfiles.d/cardputer-zero-xwayland.conf
/etc/lightdm/lightdm.conf.d/99-cardputer-zero-no-autologin.conf
/etc/cardputer-zero/lightdm-labwc-environment
/etc/greetd/cardputer-zero.toml
/usr/share/polkit-1/actions/org.cardputerzero.zero-helper.policy
/etc/udev/rules.d/99-cardputer-zero.rules
/etc/cardputer-zero/os.conf
/etc/cardputer-zero/session.conf
/etc/cardputer-zero/display.conf
/etc/xdg/cardputer-zero-labwc/*
/usr/share/cardputer-zero/*
```

It enables:

```text
zero-splash.service
zero-greetd.service
```

`zero-polkit-agent` is installed as a user service file for inspection/manual
use, but the normal Zero session starts it directly from
`cardputer-zero-session` so it registers against the actual Zero login session.

`zero-greeter` is installed as a greeter program, not as a standalone
`zero-greeter.service`. `zero-greetd.service` is the single internal-screen
login backend.

## What Install Does Not Do

The install script does not:

- create a fixed `zero` user,
- disable LightDM,
- disable `display-manager`,
- disable `getty@tty1`,
- install `cardputer-zero-shell`,
- install app launcher pages,
- install app store UI,
- install packages through apt.

It also removes the legacy `/etc/sudoers.d/cardputer-zero` file when present,
because helper authorization now belongs to polkit.

## LightDM Policy

`cardputer-zero-os` keeps LightDM available as an HDMI/recovery login surface,
but it forbids autologin.

The installer writes:

```text
/etc/lightdm/lightdm.conf.d/99-cardputer-zero-no-autologin.conf
```

and comments out existing `autologin-*` keys in:

```text
/etc/lightdm/lightdm.conf
```

This preserves Pi OS user creation and normal HDMI login while preventing the
base desktop from booting straight into a user session and stealing the Zero
internal display.

For Raspberry Pi OS labwc greeter sessions, the installer switches LightDM to
the `cardputer-zero-pi-greeter-labwc` greeter entry:

```text
/usr/share/xgreeters/cardputer-zero-pi-greeter-labwc.desktop
/usr/local/bin/cardputer-zero-lightdm-labwc
```

The wrapper exports:

```text
WLR_DRM_DEVICES=/dev/dri/cardputer-zero-hdmi
LABWC_FALLBACK_OUTPUT=NOOP-1
```

before starting `labwc`. This is intentional: `WLR_DRM_DEVICES` must be in the
process environment before wlroots opens DRM devices, otherwise the Pi OS
greeter may still claim the Zero internal `SPI-1` card. The stable
`/dev/dri/cardputer-zero-hdmi` symlink is used instead of the Raspberry Pi
`by-path` name because `WLR_DRM_DEVICES` uses `:` as its list separator.

The installer also writes the same HDMI-only device hint into:

```text
/etc/xdg/labwc-greeter/environment
/etc/xdg/labwc/environment
```

so Pi OS LightDM/labwc uses the HDMI/vc4 DRM card and does not claim the Zero
internal `SPI-1` card.

## Internal Labwc Session

The installer also installs the internal labwc session wrapper:

```text
/usr/local/bin/cardputer-zero-labwc-session
/etc/xdg/cardputer-zero-labwc/environment
/etc/xdg/cardputer-zero-labwc/rc.xml
/etc/xdg/cardputer-zero-labwc/autostart
```

This is the Wayland/labwc post-login session. The default
`/etc/cardputer-zero/session.conf` is:

```text
CARDPUTER_ZERO_SESSION_MODE="labwc"
```

After ZeroShell has a Wayland client backend, set:

```text
CARDPUTER_ZERO_WAYLAND_SHELL="/opt/cardputer-zero-shell/bin/zero-shell-wayland"
```

The wrapper constrains wlroots/labwc to:

```text
WLR_DRM_DEVICES=/dev/dri/cardputer-zero-internal
WLR_BACKENDS=drm
WLR_RENDERER=pixman
```

The Zero internal labwc session must not steal HDMI keyboard/mouse input.

The installer also keeps `/tmp/.X11-unix` in the standard `root:root 1777`
state through:

```text
/etc/tmpfiles.d/cardputer-zero-xwayland.conf
```

`zero-greetd.service` also repairs the directory before it starts. This keeps
the fix active even if HDMI LightDM/labwc recreated the directory as
`lightdm:lightdm 0755` after boot.

This matters when HDMI LightDM/labwc and the Zero internal labwc session coexist:
Xwayland refuses to start if `/tmp/.X11-unix` is owned by `lightdm` instead of
`root` or the current user.

For diagnostics:

```sh
sudo sh ./scripts/probe-labwc-session.sh
```

For a one-shot compositor smoke test, run as the logged-in Zero user:

```sh
sh ./scripts/test-labwc-internal-session.sh
```

This starts the internal labwc wrapper with `/bin/true` as the session command
and then exits. It does not edit `/etc/cardputer-zero/session.conf`.

For a greetd-backed labwc smoke test, run from SSH or HDMI:

```sh
sudo sh ./scripts/test-greetd-labwc-session.sh
```

This temporarily switches `/etc/greetd/cardputer-zero.toml` to a debug wrapper,
asks you to log in on the Zero internal screen, captures labwc diagnostics, and
then restores the normal greetd config automatically. Use this script instead
of hand-editing the greetd config for labwc tests.

Legacy direct-framebuffer mode exists only as an explicit compatibility mode:

```text
CARDPUTER_ZERO_SESSION_MODE="framebuffer"
```

## Internal greetd Backend

For the internal Wayland/labwc session, `cardputer-zero-os` uses greetd as the
standard login/session backend while keeping the existing 320x170 Zero greeter
UI.

Installed files:

```text
/etc/greetd/cardputer-zero.toml
/etc/systemd/system/zero-greetd.service
```

The system service is independent from the package's default
`greetd.service/display-manager.service` alias. This is deliberate: HDMI keeps
using Pi OS LightDM, while the Zero internal screen has its own login backend.

The config runs:

```text
/usr/local/bin/zero-greeter
```

as the greetd greeter user:

```text
_greetd
```

When `zero-greeter` sees `GREETD_SOCK`, it keeps the same small-screen UI but
delegates authentication and session creation to greetd.

`zero-greeter` asks greetd to start:

```text
/usr/local/bin/cardputer-zero-session
```

That script remains the single post-login handoff point. It reads
`/etc/cardputer-zero/session.conf`; the default is the labwc session mode.

Install and enable sequence:

```sh
sudo ./install.sh
```

Expected successful Wayland/labwc result:

```text
zero-greetd.service
  -> zero-greeter UI
  -> greetd/PAM
  -> cardputer-zero-session
  -> cardputer-zero-labwc-session
  -> labwc on SPI-1
  -> /opt/cardputer-zero-shell/bin/zero-shell-wayland, when available
```

The resulting `loginctl` session should be active on `seat0` and `tty8`, and
the labwc session should run as the authenticated user, not as root.

There is no `zero-greeter.service` fallback in the current architecture. Use
SSH or HDMI LightDM as the recovery surface if the internal greetd service fails.

## DESTDIR / Image Root

The installer supports:

```sh
DESTDIR=/path/to/root sudo -E ./install.sh
```

When `DESTDIR` is set:

- files are installed under the target root,
- live group creation is skipped,
- live user membership updates are skipped,
- systemd enablement is skipped.

This is for image-root preparation, not a full distribution build system.

## Quiet Boot

`install.sh` calls:

```text
scripts/setup-quiet-boot.sh
```

That script updates `cmdline.txt` only if it can find it:

```text
/boot/firmware/cmdline.txt
/boot/cmdline.txt
```

It creates a backup:

```text
cmdline.txt.cardputer-zero.bak
```

## Uninstall

```sh
sudo ./uninstall.sh
```

Uninstall:

- disables `zero-greetd.service`,
- disables `zero-splash.service`,
- removes installed OS profile files,
- restores backed-up `cmdline.txt` when a backup exists,
- reloads systemd and udev where available.

Uninstall does not:

- delete users,
- remove groups,
- uninstall `cardputer-zero-shell`,
- remove custom user data.

## Verify

Check services:

```sh
systemctl is-enabled zero-splash.service
systemctl is-enabled zero-greetd.service
systemctl status zero-greetd.service
test ! -e /etc/systemd/system/zero-greeter.service
```

Check LightDM remains independent:

```sh
systemctl is-enabled lightdm.service || true
systemctl is-enabled display-manager.service || true
```

Check greeter binary:

```sh
ls -l /usr/local/bin/zero-greeter
```

Check session target:

```sh
sed -n '1,120p' /usr/local/bin/cardputer-zero-session
```

Check helper policy:

```sh
pkaction --verbose --action-id org.cardputerzero.zero-helper
test ! -e /etc/sudoers.d/cardputer-zero
```

Check shell handoff:

```sh
ls -l /opt/cardputer-zero-shell/bin/zero-shell-wayland
```

If the Wayland shell is missing, the session should remain in the labwc
bring-up session or exit back to greetd. It must not silently start the legacy
framebuffer shell.
