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
- systemd for service enablement on live installs.

On Raspberry Pi OS / Debian:

```sh
sudo apt-get install build-essential pkg-config libpam0g-dev libglib2.0-dev libpolkit-agent-1-dev libpolkit-gobject-1-dev
```

The installer does not install packages by itself.

## Installed Files

The install script installs:

```text
/usr/local/bin/zero-splash
/usr/local/bin/zero-greeter
/usr/local/bin/cardputer-zero-session
/usr/local/bin/zero-polkit-agent
/usr/local/sbin/zero-helper
/etc/pam.d/zero-greeter
/etc/systemd/system/zero-splash.service
/etc/systemd/system/zero-greeter.service
/etc/systemd/user/zero-polkit-agent.service
/usr/share/polkit-1/actions/org.cardputerzero.zero-helper.policy
/etc/udev/rules.d/99-cardputer-zero.rules
/etc/cardputer-zero/os.conf
/etc/cardputer-zero/session.conf
/etc/cardputer-zero/display.conf
/usr/share/cardputer-zero/*
```

It enables:

```text
zero-splash.service
zero-greeter.service
```

`zero-polkit-agent` is installed as a user service file for inspection/manual
use, but the normal Zero path starts it directly from
`cardputer-zero-session` so it registers against the actual Zero login session.

## What Install Does Not Do

The install script does not:

- create a fixed `zero` user,
- configure autologin,
- disable LightDM,
- disable `display-manager`,
- disable `getty@tty1`,
- install `cardputer-zero-shell`,
- install app launcher pages,
- install app store UI,
- install packages through apt.

It also removes the legacy `/etc/sudoers.d/cardputer-zero` file when present,
because helper authorization now belongs to polkit.

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

- disables `zero-greeter.service`,
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
systemctl is-enabled zero-greeter.service
systemctl status zero-greeter.service
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
ls -l /opt/cardputer-zero-shell/bin/zero-shell
```

If shell is missing, the session fallback should start a login shell after successful authentication.
