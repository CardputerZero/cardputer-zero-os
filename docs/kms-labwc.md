# KMS/Labwc Experiment

This document describes the experimental Route 2 graphics path:

```text
Cardputer Zero internal ST7789 display
  -> DRM/KMS panel
  -> labwc / wlroots output
  -> Wayland applications
  -> ZeroShell as launcher/task UI
```

This is not the default `cardputer-zero-os` path yet. The current stable path
uses the M5Stack fbdev/fbtft driver and exposes the internal screen as
`/dev/fb0`.

## Current Display Facts

The current Cardputer Zero Linux overlay binds the internal screen as:

```text
compatible: sitronix,st7789v_m5stack
driver:     fb_st7789v_m5stack / fbtft
device:     SPI0 CE0
dc gpio:    BCM GPIO25
reset gpio: m5ioe1 GPIO4, present in source but currently commented
backlight:  m5ioe1 PWM channel 3
panel RAM:  170x320
user view:  320x170 through rotate=90
offset:     ram-x-offset=0, ram-y-offset=35
speed:      50 MHz in cardputerzero-overlay, 40 MHz in older fbtft config
```

That means labwc cannot manage the internal display today. Labwc can manage
DRM/KMS outputs, not arbitrary fbdev devices owned directly by apps.

## Experiment Strategy

The experiment keeps the existing fbdev path recoverable and generates a
separate overlay:

```text
/boot/firmware/overlays/cardputerzero-kms-display.dtbo
```

The generated overlay is deliberately small. The normal
`cardputerzero-overlay.dtbo` stays enabled so it can keep owning the keyboard,
audio, m5ioe1, sensors, and backlight. The experimental display overlay only:

- marks the existing fbdev `st7789v@0` SPI node as disabled,
- adds a new `display@0` node using `panel-mipi-dbi-spi`.

The experiment also installs a panel firmware file:

```text
/lib/firmware/cardputerzero,st7789v.bin
```

The file contains the ST7789V initialization command stream for the
`panel-mipi-dbi` Linux driver.

The firmware keeps the controller orientation equivalent to the old fbdev
driver's `rotate=90` mode:

```text
MADCTL = MV | MY = 0xa0
```

Together with the `320x170` KMS mode and the 35-pixel page offset, this makes
the physical landscape screen line up with the framebuffer instead of wrapping
or shifting the image.

The experiment installs a modules-load file too:

```text
/etc/modules-load.d/cardputer-zero-kms.conf
```

On the current Raspberry Pi OS kernel, the first compatible string must remain
`cardputerzero,st7789v` so that `panel-mipi-dbi` requests the matching firmware
file name. That makes the SPI modalias `spi:st7789v`, which does not auto-load
the generic `panel_mipi_dbi` module. The modules-load file makes that binding
deterministic during boot.

## Install

Run on the target device:

```sh
sudo sh ./scripts/probe-graphics-stack.sh
sudo sh ./scripts/setup-kms-experimental.sh
sudo reboot
```

The setup script requires:

```text
dtc
```

On Debian/Raspberry Pi OS this is provided by:

```sh
sudo apt install device-tree-compiler
```

The script backs up the current boot config under:

```text
/var/backups/cardputer-zero-os/kms-experimental/
```

## Verify

After reboot, check whether the internal display appears under DRM:

```sh
sudo sh ./scripts/probe-graphics-stack.sh
ls /sys/class/drm
dmesg | grep -Ei 'panel-mipi-dbi|mipi.*dbi|st7789|drm|spi'
```

Expected direction:

```text
/sys/class/drm/card*-SPI-*
panel-mipi-dbi in lsmod/dmesg
no fb_st7789v_m5stack binding for the internal display
```

The exact connector name is kernel-dependent.

## Verified On 50.35

On `pi@192.168.50.35`, this experiment has been verified with Raspberry Pi OS
kernel:

```text
Linux 6.12.75+rpt-rpi-v8
```

After installing the experimental overlay and rebooting:

```text
/sys/class/graphics/fb1/name  = panel-mipi-dbid
/sys/class/graphics/fb1/modes = U:320x170p-0
/sys/class/drm/card0-SPI-1    = connected, enabled, 320x170
/sys/bus/spi/devices/spi0.0   = panel-mipi-dbi-spi
wlr-randr output              = SPI-1, 320x170 px, 60 Hz
```

The framebuffer index is not stable. Under the old fbdev path the internal
screen was `/dev/fb0`; under the DRM tiny panel path it appeared as `/dev/fb1`.
OS framebuffer components must therefore discover the internal screen by
framebuffer name instead of hard-coding `/dev/fb0`.

The final verified Route 2 framebuffer is landscape (`320x170`). During the
bring-up, an earlier portrait (`170x320`) mode produced a correct raw
framebuffer capture but a shifted physical image, because the ST7789 controller
orientation did not match the old fbdev `rotate=90` model.

`zero-splash`, `zero-greeter`, and `zero-polkit-agent` now use internal
framebuffer discovery and tolerate both:

```text
fb_st7789v_m5st
panel-mipi-dbid
```

This keeps the existing fbdev login path usable while Route 2 moves toward a
proper Wayland/labwc login/session path.

## HDMI And Autologin Policy

The Zero internal screen and HDMI are separate display surfaces:

```text
SPI-1      = Cardputer Zero internal appliance screen
HDMI-A-1   = external Pi OS / recovery / desktop surface
```

They must not be mirrored by default. If both are present, HDMI should remain a
normal external display path unless the user explicitly chooses mirror,
extended, or internal-only mode.

LightDM may remain enabled for HDMI and recovery, but it must not autologin.
All login surfaces must require authentication. This prevents the base Pi OS
desktop from booting straight into a user session and taking over `SPI-1` when
the Zero internal display is the only connected DRM output.

Raspberry Pi OS labwc greeter sessions are restricted to the HDMI/vc4 DRM card
with the `cardputer-zero-pi-greeter-labwc` LightDM greeter entry. Its wrapper
sets:

```text
WLR_DRM_DEVICES=/dev/dri/cardputer-zero-hdmi
LABWC_FALLBACK_OUTPUT=NOOP-1
```

This keeps LightDM/labwc from drawing a cursor or desktop on the Zero internal
`SPI-1` output. The stable DRM paths observed on 50.35 are:

```text
/dev/dri/by-path/platform-3f204000.spi-cs-0-card = Zero internal SPI panel
/dev/dri/by-path/platform-soc:gpu-card           = vc4 HDMI/display pipeline
```

The HDMI `by-path` name contains `:`, so it is not used directly with
`WLR_DRM_DEVICES`; wlroots interprets `:` as a device-list separator. The udev
rule provides `/dev/dri/cardputer-zero-hdmi` for that policy.

## Restore

If the internal screen fails or the experiment should be disabled:

```sh
sudo sh ./scripts/restore-kms-experimental.sh
sudo reboot
```

The restore script reinstalls the latest saved `config.txt` and removes the
experimental overlay/firmware if they did not exist before.

## Scope

This experiment only proves the internal display can become a DRM/KMS output.
It does not yet:

- move `zero-greeter` to Wayland,
- move `zero-shell` to a Wayland client backend,
- configure labwc for the internal output,
- migrate LoFiBox/AppStore away from direct framebuffer rendering,
- implement task switching through compositor toplevel protocols.

Those are later phases after the KMS output exists.

## Next Phase

The next phase is not a ZeroShell change. It belongs to `cardputer-zero-os` and
should establish a real Wayland session path for the internal output:

- decide whether `zero-greeter` remains a framebuffer greeter temporarily or
  becomes a Wayland client,
- start labwc from the authenticated user session with the correct seat/logind
  permissions,
- configure wlroots output `SPI-1` for the 320x170 user orientation,
- then migrate ZeroShell to run as a Wayland client instead of owning the
  framebuffer directly.

## Boundary

`cardputer-zero-os` owns this experiment because it changes boot graphics,
device tree overlays, display stack setup, and recovery.

`cardputer-zero-shell` should only change after this experiment proves that
labwc can see the internal screen as a real output.
