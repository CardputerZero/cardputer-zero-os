# Splash

`zero-splash` provides a Cardputer Zero userspace startup experience.

It is not a firmware splash and is not a complete replacement for every Pi OS
boot message.

## What It Can Do

- draw a lightweight startup splash on the Cardputer Zero internal framebuffer,
- and hold the visual handoff until the greeter starts.

## What It Cannot Promise

- replacing the Raspberry Pi firmware stage,
- suppressing all early kernel output,
- suppressing every service message before userspace splash starts,
- taking over HDMI or Pi OS `lightdm`,
- or replacing a full boot theme system.

## Quiet Boot

`scripts/setup-quiet-boot.sh` updates the Raspberry Pi command line
idempotently when it finds `cmdline.txt`.

It appends conservative tokens:

```text
quiet loglevel=3 vt.global_cursor_default=0 logo.nologo consoleblank=0
```

The original file is backed up beside it as:

```text
cmdline.txt.cardputer-zero.bak
```
