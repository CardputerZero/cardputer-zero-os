# Recovery

Recovery is explicit. `cardputer-zero-os` should fail visibly through logs and
standard recovery surfaces instead of silently starting a different internal
graphics path.

## Recovery Surfaces

- SSH,
- HDMI LightDM,
- systemd service control,
- boot backups under `/var/backups/cardputer-zero-os`.

## Disable Internal Greeter

```sh
sudo systemctl disable --now zero-greetd.service
```

HDMI LightDM remains the normal graphical recovery login surface.

## Restart Internal Greeter

```sh
sudo systemctl restart zero-greetd.service
```

or through the helper when already logged in:

```sh
pkexec /usr/local/sbin/zero-helper restart-greeter
```

## Logs

```sh
journalctl -b -u zero-greetd.service --no-pager
cat /tmp/cardputer-zero-greeter-session.log
```

## Restore Boot Command Line

If quiet boot changes cause trouble:

```sh
sudo cp /boot/firmware/cmdline.txt.cardputer-zero.bak /boot/firmware/cmdline.txt
```

or:

```sh
sudo cp /boot/cmdline.txt.cardputer-zero.bak /boot/cmdline.txt
```

## Full Uninstall

```sh
sudo ./uninstall.sh
```

Uninstall disables Zero services, removes installed profile files, and restores
a backed-up `cmdline.txt` when present. It does not delete users.
