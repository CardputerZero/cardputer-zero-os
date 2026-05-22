# zero-helper

`zero-helper` is the restricted privileged helper for Cardputer Zero.

It exists so login-session UI can request a small set of root operations without becoming root and without gaining arbitrary sudo.

Shells and apps should call `zero-helper` directly. They should not prefix it
with `sudo`; `zero-helper` owns the `pkexec`/polkit transition.

For that reason, `zero-helper` rejects direct `sudo zero-helper ...` execution
when it detects a `SUDO_USER` environment.

## Path

```text
/usr/local/sbin/zero-helper
```

## Polkit Policy

Installed policy file:

```text
/usr/share/polkit-1/actions/org.cardputerzero.zero-helper.policy
```

Normal user processes call `zero-helper` directly. If it is not already running
as root, it re-execs itself through:

```text
pkexec /usr/local/sbin/zero-helper ...
```

Polkit then authorizes the request through the current user session's
authentication agent. `cardputer-zero-os` installs `zero-polkit-agent` for the
internal Zero screen.

Older `NOPASSWD` sudoers entries are intentionally removed during install.

## Allowed Actions

Current allowed actions:

```sh
/usr/local/sbin/zero-helper reboot
/usr/local/sbin/zero-helper shutdown
/usr/local/sbin/zero-helper poweroff
/usr/local/sbin/zero-helper restart-greeter
/usr/local/sbin/zero-helper network-restart
/usr/local/sbin/zero-helper display internal
/usr/local/sbin/zero-helper display mirror
/usr/local/sbin/zero-helper display extended
/usr/local/sbin/zero-helper appstore install-deb DEB_PATH PACKAGE
/usr/local/sbin/zero-helper appstore remove PACKAGE
/usr/local/sbin/zero-helper appstore repair-dpkg
```

## Forbidden Shape

`zero-helper` must not allow:

- arbitrary shell commands,
- arbitrary `sudo`,
- arbitrary `systemctl`,
- arbitrary package installation,
- arbitrary process killing,
- arbitrary file writes,
- arbitrary app launching.

## Display Actions

Current display actions record the desired mode in:

```text
/run/cardputer-zero/display-mode
```

Device-specific display switching is not implemented yet.

This is intentional: the helper exposes a stable controlled action before the hardware-specific implementation is finalized.

## Network Restart

`network-restart` tries supported services in order:

- `NetworkManager.service`
- `networking.service`
- `dhcpcd.service`

If none exists, it exits with an error.

## AppStore Actions

AppStore package operations are deliberately narrow:

- `appstore install-deb DEB_PATH PACKAGE`
- `appstore remove PACKAGE`
- `appstore repair-dpkg`

`install-deb` only accepts `.deb` files from the AppStore cache:

```text
/home/*/.local/share/cardputerzero-appstore/cache/downloads/*.deb
/var/cache/cardputerzero-appstore/downloads/*.deb
```

It validates that the `.deb` package name matches the requested package before
calling the base OS package tools.

This is not a general apt or dpkg wrapper.

## Relationship To ZeroShell

`cardputer-zero-shell` may call `zero-helper` for power or display actions.

ZeroShell should not call:

```sh
sudo systemctl ...
sudo apt ...
sudo sh -c ...
sudo reboot
sudo shutdown
```

The helper is the privilege boundary.
