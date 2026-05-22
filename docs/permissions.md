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

`zero-helper` is polkit-backed. Normal user processes call it directly; when
root is required it re-execs through `pkexec`, and polkit asks the active user
session to authorize the request.

Policy file:

```text
/usr/share/polkit-1/actions/org.cardputerzero.zero-helper.policy
```

Authentication agent:

```text
/usr/local/bin/zero-polkit-agent
/etc/systemd/user/zero-polkit-agent.service
```

In the normal Zero path, `cardputer-zero-session` starts the agent directly so
it registers against the same login session as ZeroShell.

The helper does not accept arbitrary commands, arbitrary systemd service names,
or arbitrary package-manager arguments. The old `NOPASSWD` sudoers path is not
part of the current permission model.

Allowed actions:

- `reboot`
- `shutdown`
- `poweroff`
- `restart-greeter`
- `network-restart`
- `display internal`
- `display mirror`
- `display extended`
- `appstore install-deb DEB_PATH PACKAGE`
- `appstore remove PACKAGE`
- `appstore repair-dpkg`
