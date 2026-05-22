# Recovery

Recovery is a first-class boundary for `cardputer-zero-os`.

The profile should never trap the device behind a broken shell, broken greeter,
or root-only launcher.

## Disable the Greeter

From SSH or another Pi OS recovery path:

```sh
sudo systemctl disable --now zero-greeter.service
```

## Restart the Greeter

```sh
/usr/local/sbin/zero-helper restart-greeter
```

or:

```sh
sudo systemctl restart zero-greeter.service
```

## Missing Shell

If `/opt/cardputer-zero-shell/bin/zero-shell` is missing, the session script
starts the user's login shell. This gives the authenticated user a local
recovery path without running the desktop as root.

## Restore Boot Command Line

If quiet boot changes cause trouble, restore the backup:

```sh
sudo cp /boot/firmware/cmdline.txt.cardputer-zero.bak /boot/firmware/cmdline.txt
```

or, on systems using the older path:

```sh
sudo cp /boot/cmdline.txt.cardputer-zero.bak /boot/cmdline.txt
```

## Full Uninstall

```sh
sudo ./uninstall.sh
```

The uninstall script disables Zero services, removes installed profile files,
and restores a backed-up `cmdline.txt` when present.

It does not delete user accounts or remove groups.
