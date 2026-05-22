# Permissions

`cardputer-zero-os` configures device access for real logged-in users.

The profile does not solve permissions by running the desktop as root.

## Groups

The live installer creates the `cardputer-zero` group when missing and adds
existing normal users to relevant groups when those groups exist:

```text
cardputer-zero,input,video,audio,render,gpio,spi,i2c
```

When installing into an image root with `DESTDIR`, live group membership changes
are skipped because the target users may not exist yet.

## udev

The udev rules assign common devices to the expected groups and add `uaccess`
tags where logind can use them:

```text
/etc/udev/rules.d/99-cardputer-zero.rules
```

## zero-helper

`zero-helper` is installed at:

```text
/usr/local/sbin/zero-helper
```

The sudoers file allows members of `cardputer-zero` to run only the whitelisted
helper commands. The helper does not accept arbitrary commands or arbitrary
systemd service names.

Allowed actions:

- `reboot`
- `shutdown`
- `poweroff`
- `restart-greeter`
- `network-restart`
- `display internal`
- `display mirror`
- `display extended`
