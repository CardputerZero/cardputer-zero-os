# Internal DRM/KMS + Labwc Stack

This document describes the Cardputer Zero internal-screen graphics stack.

```text
Cardputer Zero internal ST7789 display
  -> panel-mipi-dbi-spi
  -> DRM/KMS output
  -> labwc / wlroots compositor
  -> Wayland greeter and Wayland/Xwayland apps
```

## Why DRM/KMS

labwc and wlroots manage DRM/KMS outputs. Once the internal display is a DRM
output, the same Linux compositor model can manage:

- login UI,
- ZeroShell,
- app windows,
- focus,
- minimize,
- close,
- stacking,
- task listing.

That is the standard mechanism that makes multitasking possible without asking
ZeroShell to guess process trees or arbitrate display ownership.

## Device Tree Strategy

The base `cardputerzero-overlay.dtbo` stays enabled for non-display hardware:

- keyboard,
- audio,
- M5 IO expander,
- sensors,
- backlight-related nodes.

The display-specific overlay is:

```text
/boot/firmware/overlays/cardputerzero-kms-display.dtbo
```

It adds a SPI `display@0` node compatible with:

```text
panel-mipi-dbi-spi
```

The panel firmware is:

```text
/lib/firmware/cardputerzero,st7789v.bin
```

It contains the ST7789 initialization sequence used by `panel-mipi-dbi`.

## Orientation

The verified user-facing geometry is:

```text
320x170 landscape
MADCTL = MV | MY = 0xa0
page offset = 35
```

The panel firmware keeps that orientation so labwc sees a correctly aligned
320x170 output.

## Module Loading

The module-load file is:

```text
/etc/modules-load.d/cardputer-zero-kms.conf
```

It loads:

```text
panel_mipi_dbi
```

This makes display binding deterministic on Raspberry Pi OS kernels where the
modalias does not auto-load the generic panel driver.

## Install

`install.sh` runs:

```sh
sh ./scripts/setup-internal-drm-display.sh
```

The script requires:

```sh
sudo apt install device-tree-compiler
```

It writes backups under:

```text
/var/backups/cardputer-zero-os/internal-drm-display/
```

## Verify

```sh
sudo sh ./scripts/probe-graphics-stack.sh
ls /sys/class/drm
dmesg | grep -Ei 'panel-mipi-dbi|mipi.*dbi|st7789|drm|spi'
```

Expected shape:

```text
/sys/class/drm/card*-SPI-*
panel_mipi_dbi loaded
/dev/dri/cardputer-zero-internal exists
```

## Session Binding

The greeter and user sessions set:

```text
WLR_DRM_DEVICES=/dev/dri/cardputer-zero-internal
WLR_BACKENDS=drm,libinput
WLR_RENDERER=pixman
XDG_SEAT=seat-cardputer-zero
```

HDMI policy uses `/dev/dri/cardputer-zero-hdmi` so HDMI remains a separate Pi
OS login/recovery path.

## Seat Split

HDMI and the Zero internal screen must not share one active logind seat if both
are expected to display at the same time. logind allows multiple sessions on a
seat, but only one of them is active. A background wlroots compositor on that
same seat may expose only a headless `NOOP-1` output.

The udev rules assign:

```text
internal ST7789 DRM card -> seat-cardputer-zero
tca8418c keyboard       -> seat-cardputer-zero
HDMI vc4 DRM card       -> seat0
```

The internal greeter and user sessions export `XDG_SEAT=seat-cardputer-zero`.
The HDMI LightDM greeter remains on `seat0`.

## Acceptance Criteria

- The internal screen appears as a DRM/KMS connector.
- `zero-greetd.service` shows the Wayland greeter on the internal screen.
- Login creates an active logind user session.
- `zero-shell-wayland` runs as the authenticated user.
- `loginctl list-seats` shows `seat0` and `seat-cardputer-zero`.
- HDMI LightDM remains available and independent.
- Apps shown in the task list are compositor-managed windows.
