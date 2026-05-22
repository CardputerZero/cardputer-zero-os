# zero-helper

`zero-helper` is the restricted privileged helper for Cardputer Zero.

It exists so login-session UI can request a small set of root operations without becoming root and without gaining arbitrary sudo.

## Path

```text
/usr/local/sbin/zero-helper
```

## Sudoers Policy

Installed sudoers file:

```text
/etc/sudoers.d/cardputer-zero
```

Members of group `cardputer-zero` may run only explicitly whitelisted helper invocations.

## Allowed Actions

Current allowed actions:

```sh
sudo /usr/local/sbin/zero-helper reboot
sudo /usr/local/sbin/zero-helper shutdown
sudo /usr/local/sbin/zero-helper poweroff
sudo /usr/local/sbin/zero-helper restart-greeter
sudo /usr/local/sbin/zero-helper network-restart
sudo /usr/local/sbin/zero-helper display internal
sudo /usr/local/sbin/zero-helper display mirror
sudo /usr/local/sbin/zero-helper display extended
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

