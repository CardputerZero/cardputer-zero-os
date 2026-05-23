# Install Guide

## Dependencies

```sh
sudo apt-get install \
  build-essential pkg-config greetd wlrctl labwc wayland-protocols \
  device-tree-compiler libglib2.0-dev libpolkit-agent-1-dev \
  libpolkit-gobject-1-dev libwayland-dev libxkbcommon-dev
```

## Install

```sh
sudo ./install.sh
sudo reboot
```

The installer:

- installs the internal DRM display overlay and panel firmware,
- builds and installs `zero-greeter-wayland`,
- builds and installs `zero-polkit-agent` and `zero-polkit-prompt-wayland`,
- installs greetd, labwc, udev, polkit, helper, and session files,
- disables LightDM autologin,
- enables `zero-hdmi-lightdm-policy.service`,
- enables `zero-greetd.service`,
- enables `zero-key-policy.service`,
- keeps users and passwords owned by Raspberry Pi OS / Debian.

## Installed Files

```text
/usr/local/bin/zero-greeter-wayland
/usr/local/bin/cardputer-zero-greeter-session
/usr/local/bin/cardputer-zero-session
/usr/local/bin/cardputer-zero-labwc-session
/usr/local/bin/zero-key-policy
/usr/local/bin/zero-shell-control
/usr/local/bin/zero-polkit-agent
/usr/local/bin/zero-polkit-prompt-wayland
/usr/local/sbin/zero-helper
/usr/local/sbin/zero-hdmi-lightdm-policy
/etc/systemd/system/zero-greetd.service
/etc/systemd/system/zero-key-policy.service
/etc/systemd/system/zero-hdmi-lightdm-policy.service
/etc/greetd/cardputer-zero.toml
/etc/cardputer-zero/session.conf
/etc/xdg/cardputer-zero-greeter-labwc/*
/etc/xdg/cardputer-zero-labwc/*
/etc/udev/rules.d/99-cardputer-zero.rules
/usr/share/polkit-1/actions/org.cardputerzero.zero-helper.policy
/boot/firmware/overlays/cardputerzero-kms-display.dtbo
/lib/firmware/cardputerzero,st7789v.bin
/etc/modules-load.d/cardputer-zero-kms.conf
```

## What Install Does Not Do

The installer does not:

- create a fixed user,
- enable autologin,
- install `cardputer-zero-shell`,
- install app entries,
- implement launcher pages,
- remove HDMI LightDM.

## Internal greetd Backend

`zero-greetd.service` runs a dedicated greetd instance on VT8:

```text
/usr/sbin/greetd -c /etc/greetd/cardputer-zero.toml --vt 8
```

greetd starts:

```text
/usr/local/bin/cardputer-zero-greeter-session
```

as `_greetd`. That wrapper starts the Wayland greeter session and then
`zero-greeter-wayland`.

## Internal Labwc Session

After login:

```text
cardputer-zero-session
  -> cardputer-zero-labwc-session
  -> labwc on /dev/dri/cardputer-zero-internal
  -> /opt/cardputer-zero-shell/bin/zero-shell-wayland
```

## Verify

```sh
systemctl is-enabled zero-greetd.service
systemctl status zero-greetd.service
systemctl is-enabled zero-key-policy.service
systemctl status zero-key-policy.service
ls -l /dev/dri/cardputer-zero-internal /dev/dri/cardputer-zero-hdmi
ls -l /opt/cardputer-zero-shell/bin/zero-shell-wayland
ps -eo user,pid,args | grep -E 'zero-greeter-wayland|zero-shell-wayland|labwc|zero-key-policy'
pkaction --verbose --action-id org.cardputerzero.zero-helper
```

## Uninstall

```sh
sudo ./uninstall.sh
```

Uninstall disables Zero services, removes installed profile files, restores a
backed-up `cmdline.txt` when present, and leaves users intact.
