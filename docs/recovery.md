# Recovery

Recovery is a first-class boundary for `cardputer-zero-os`.

The profile should never trap the device behind a broken shell, broken greeter,
or root-only launcher.

## Disable the Internal Greeter Backend

From SSH or another Pi OS recovery surface:

```sh
sudo systemctl disable --now zero-greetd.service
```

HDMI LightDM is kept as the normal recovery login surface.

## Restart the Internal Greeter Backend

```sh
/usr/local/sbin/zero-helper restart-greeter
```

or:

```sh
sudo systemctl restart zero-greetd.service
```

## Missing Wayland Shell

If `/opt/cardputer-zero-shell/bin/zero-shell-wayland` is missing, the session
must not silently fall back to the legacy direct-framebuffer shell. It should
remain in the labwc bring-up session or return to the internal greetd login.

Use SSH or HDMI LightDM for recovery and diagnostics.

## Restore Boot Command Line

If quiet boot changes cause trouble, restore the backup:

```sh
sudo cp /boot/firmware/cmdline.txt.cardputer-zero.bak /boot/firmware/cmdline.txt
```

or, on systems using the older boot layout:

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
