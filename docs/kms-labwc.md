# DRM/KMS + Labwc Graphics Stack

This document describes the DRM/KMS + greetd + labwc + Wayland graphics stack:

```text
Cardputer Zero internal ST7789 display
  -> DRM/KMS panel
  -> labwc / wlroots output
  -> Wayland applications
  -> ZeroShell as launcher/task UI
```

On the verified target, this is now the default `cardputer-zero-os` session
direction. The old M5Stack fbdev/fbtft implementation remains useful as a
recovery and compatibility reference, but it is no longer the intended
production task model.

The setup scripts still use "experimental" in their names because changing
boot overlays and panel drivers is a hardware bring-up operation that must stay
easy to probe and roll back.

## Original fbdev Display Facts

The original Cardputer Zero Linux overlay bound the internal screen as:

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

That fbdev form is not enough for labwc. labwc can manage DRM/KMS
outputs, not arbitrary fbdev devices owned directly by apps.

## Experiment Strategy

The experiment keeps the existing fbdev implementation recoverable and generates a
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

The framebuffer index is not stable. Under the old fbdev implementation the internal
screen was `/dev/fb0`; under the DRM tiny panel implementation it appeared as `/dev/fb1`.
OS framebuffer components must therefore discover the internal screen by
framebuffer name instead of hard-coding `/dev/fb0`.

The final verified DRM/KMS framebuffer is landscape (`320x170`). During the
bring-up, an earlier portrait (`170x320`) mode produced a correct raw
framebuffer capture but a shifted physical image, because the ST7789 controller
orientation did not match the old fbdev `rotate=90` model.

`zero-splash`, `zero-greeter`, and `zero-polkit-agent` now use internal
framebuffer discovery and tolerate both:

```text
fb_st7789v_m5st
panel-mipi-dbid
```

This keeps framebuffer discovery usable for the greeter/splash while the
post-login desktop moves to a proper Wayland/labwc session.

## HDMI And Autologin Policy

The Zero internal screen and HDMI are separate display surfaces:

```text
SPI-1      = Cardputer Zero internal appliance screen
HDMI-A-1   = external Pi OS / recovery / desktop surface
```

They must not be mirrored by default. If both are present, HDMI should remain a
normal external display unless the user explicitly chooses mirror,
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

## Current Scope

The current verified DRM/KMS + greetd + labwc stack uses:

- the internal ST7789 screen as a DRM/KMS output,
- a separate greetd-backed internal login service,
- labwc constrained to the internal DRM output,
- `zero-shell-wayland` as a Wayland launcher/task UI client,
- compositor-visible task listing through `wlrctl`,
- Tab task UI and short/long Esc policy through OS input policy.

Remaining work is mostly application and compositor integration quality:

- migrate AppStore and LoFiBox toward stable Wayland-native app ids where
  practical,
- replace `wlrctl` output parsing with a direct toplevel protocol or a small
  window-state agent,
- add stronger close-then-kill fallback through user systemd scopes,
- keep HDMI and internal-seat input ownership well documented as more devices
  are added.

## Target Model

The DRM/KMS + labwc stack is not a process-task patch for the framebuffer
ZeroShell. Its purpose is to put display ownership, focus, window state,
minimize, activation, and close semantics back into the normal Linux graphics
stack:

```text
Zero internal ST7789 display
  -> DRM/KMS output
  -> labwc / wlroots compositor
  -> Wayland or Xwayland application windows
  -> ZeroShell as launcher/task UI client
```

In this model, the primary task object is a compositor toplevel/window, not a
PID. A process tree such as:

```text
sh -lc /usr/lib/lofibox/lofibox-applaunch
  -> /usr/lib/lofibox/lofibox_zero_device
```

is only an implementation detail unless the application creates a compositor
managed window. Standard minimize, activate, focus, and close behavior can only
apply to windows that labwc can see.

This means the labwc session has a hard application requirement:

- ZeroShell must become a Wayland client instead of owning `/dev/fb0`.
- LoFiBox, AppStore, and other graphical apps must become Wayland, Xwayland,
  SDL, GTK, Qt, or equivalent compositor-managed clients.
- Direct-fb applications can still run as a compatibility mode, but they cannot
  receive real compositor task management.

The APPLaunch `.desktop` contract can remain the application discovery surface.
However, running state should eventually be matched through compositor window
metadata such as `app_id`, title, and desktop-entry identifiers rather than by
guessing process trees.

## Task And Esc Policy

Task switching belongs to the compositor/window layer:

```text
Tab
  -> ZeroShell shows compositor toplevel/task list
  -> Enter requests activation of the selected window

short Esc
  -> input policy requests iconify/minimize of the active window
  -> ZeroShell launcher is shown/focused

long Esc
  -> input policy requests close of the active window
  -> if the app does not exit, terminate its user app scope
```

The short/long Esc policy cannot live only inside ZeroShell once applications
are Wayland clients. When a foreground app has keyboard focus, ZeroShell is just
another client and does not receive global key events. The policy must be owned
by one of:

- labwc key bindings/actions where they are expressive enough,
- a small Zero input-policy daemon,
- a narrow labwc customization,
- or another compositor-side integration.

Long Esc should not start by killing an arbitrary PID. The standard-friendly
sequence is:

1. request close on the active window,
2. give the app a short grace period to exit,
3. terminate the corresponding user app scope only as a fallback.

For reliable cleanup, app launchers should prefer a user systemd scope:

```text
systemd-run --user --scope --collect <app command>
```

The scope is cleanup metadata, not the task identity. The task identity remains
the compositor toplevel/window.

## Current Session Stack

The current session stack belongs to `cardputer-zero-os` because it establishes
the authenticated logind session, DRM device selection, labwc process, and
global input policy for the internal output:

- `zero-greeter` remains a small framebuffer greeter for the login screen;
- greetd owns PAM authentication and session creation;
- labwc starts from the authenticated user session;
- wlroots is constrained to the internal `SPI-1` DRM output;
- HDMI/LightDM remains available as the Pi OS login and recovery surface;
- autologin remains disabled on all login surfaces;
- ZeroShell runs as a Wayland client instead of owning the framebuffer directly.

The repository now installs the wrapper for this bring-up stack:

```text
/usr/local/bin/cardputer-zero-labwc-session
/etc/xdg/cardputer-zero-labwc/*
```

It is selected by the default `/etc/cardputer-zero/session.conf`:

```text
CARDPUTER_ZERO_SESSION_MODE="labwc"
```

The legacy direct-framebuffer ZeroShell mode is available only when explicitly
configured:

```text
CARDPUTER_ZERO_SESSION_MODE="framebuffer"
```

It is not used as an automatic fallback. labwc/Wayland failures should stay
visible so they can be fixed instead of silently entering the wrong graphics
model.

For a one-shot labwc smoke test that does not change the default session mode:

```sh
sh ./scripts/test-labwc-internal-session.sh
```

Run this as the authenticated Zero user, not with `sudo`. The test launches
labwc on the internal DRM device with `/bin/true` as its session command so it
can prove whether wlroots can open the internal output and then exit.

For the greetd-backed session, use the auto-rollback HITL test instead of
hand-editing `/etc/greetd/cardputer-zero.toml`:

```sh
sudo sh ./scripts/test-greetd-labwc-session.sh
```

It temporarily changes the Zero greetd config, waits while the operator logs in
on the internal screen, captures labwc diagnostics, and restores the normal
greetd config before it exits.

## Verified Session Findings On 50.35

On `pi@192.168.50.35`, the internal DRM output is present and correctly
separated from HDMI:

```text
/dev/dri/cardputer-zero-internal -> card0
/dev/dri/cardputer-zero-hdmi     -> card1
/sys/class/drm/card0-SPI-1       -> connected, 320x170
```

The original self-managed `zero-greeter.service` implementation was not enough for a
wlroots DRM compositor. A one-shot internal labwc smoke test from that implementation
failed before the compositor could open the DRM backend:

```text
Could not open target tty: Permission denied
Timeout waiting session to become active
failed to start a session
failed to add backend 'drm'
unable to create backend
```

The important fact was that the self-managed PAM session was registered by
logind as a background session without an active seat/TTY:

```text
zero-greeter.service
  -> PAM session for pi
  -> class=background
  -> no active seat
```

That is sufficient for the current framebuffer ZeroShell mode, because direct
framebuffer access is granted by device permissions. It is not sufficient for a
wlroots DRM compositor. A real labwc session needs display-manager or
login-session machinery that creates an active graphical seat/session for the
internal output.

The greetd experiment solves this specific session-authority problem while
preserving the existing 320x170 Zero login UI. On 50.35, this chain was
verified:

```text
zero-greetd.service
  -> zero-greeter as a greetd frontend
  -> greetd PAM authentication
  -> /usr/local/bin/cardputer-zero-session
  -> /opt/cardputer-zero-shell/bin/zero-shell-wayland
```

The resulting user session was active on the Zero login VT:

```text
pi  seat0  tty8  active
```

and `zero-shell-wayland` ran as the real authenticated user:

```text
pi  /opt/cardputer-zero-shell/bin/zero-shell-wayland
```

This proves the login/session handoff direction and the current labwc/Wayland
desktop session.

The greetd-backed labwc smoke test later verified that labwc can open the Zero
internal DRM output:

```text
WLR_DRM_DEVICES=/dev/dri/cardputer-zero-internal
DRM backend: /dev/dri/card0 (panel-mipi-dbi)
Connector: SPI-1
Mode: 320x170 @ 60 Hz
Enabled: yes
Transform: normal
```

The first run failed in Xwayland because `/tmp/.X11-unix` had been created as
`lightdm:lightdm 755`. The standard directory state is `root:root 1777`.
`cardputer-zero-os` installs:

```text
/etc/tmpfiles.d/cardputer-zero-xwayland.conf
```

and `zero-greetd.service` also repairs the directory in `ExecStartPre`. This is
needed because HDMI LightDM/labwc can recreate the directory after boot; the
Zero internal labwc session must see it as `root:root 1777` before Xwayland can
start.

## greetd Experiment

The current implementation adds a separate greetd service for the Zero internal
screen:

```text
/etc/greetd/cardputer-zero.toml
/etc/systemd/system/zero-greetd.service
```

This does not use the package's default `greetd.service` display-manager alias,
because HDMI must keep its Pi OS LightDM service. `zero-greetd.service` is a
separate service and is the only installed internal-screen login service.

The greetd config runs the existing small-screen `zero-greeter` UI as the
greeter command:

```text
[default_session]
command = "/usr/local/bin/zero-greeter"
user = "_greetd"
```

When `zero-greeter` sees `GREETD_SOCK`, it acts as a greetd frontend:

```text
Zero 320x170 greeter UI
  -> greetd IPC
  -> PAM/auth/session handled by greetd
  -> /usr/local/bin/cardputer-zero-session
```

This preserves the Zero login UI while moving session authority to a standard
display-manager backend. `cardputer-zero-session` remains the single session
handoff script and reads `/etc/cardputer-zero/session.conf`; labwc mode is
selected there with `CARDPUTER_ZERO_SESSION_MODE=labwc`.

## Acceptance Criteria

The DRM/KMS + greetd + labwc OS stack should be considered healthy when all of
these are true:

- `SPI-1` remains visible as a DRM/KMS output after reboot.
- HDMI remains available as a Pi OS / LightDM login and recovery surface.
- No login surface autologins.
- A labwc session can be started for the authenticated Zero user on the Zero
  internal output.
- The labwc session is constrained to the Zero internal DRM output and does not
  claim HDMI by default.
- The Cardputer keyboard drives the Zero internal session.
- HDMI keyboard/mouse devices are not stolen by the Zero internal session.
- The internal session is recoverable through SSH or HDMI if it fails.
- ZeroShell is launched as a Wayland client inside that session.

Non-acceptance examples:

- the internal screen showing a cropped part of the HDMI desktop,
- Pi OS booting directly into a user session without authentication,
- LightDM/labwc drawing its cursor or desktop on `SPI-1`,
- ZeroShell continuing to own `/dev/fb0` while claiming to run inside labwc,
- direct-fb LoFiBox/AppStore being treated as compositor tasks.

## Boundary

`cardputer-zero-os` owns this experiment because it changes boot graphics,
device tree overlays, display stack setup, and recovery.

`cardputer-zero-shell` changes only at the post-login launcher/task UI layer. It
does not own boot overlays, LightDM/greetd setup, DRM device selection, or
global input policy.
