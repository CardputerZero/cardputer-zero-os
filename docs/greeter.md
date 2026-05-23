# Greeter

`zero-greeter` is the internal-screen login UI component in this profile.

It is a greeter frontend, not a launcher and not a standalone display manager.
Its job ends once greetd has started an authenticated user session.

## User Discovery

The greeter reads users from the base OS account database and displays normal
interactive users:

- UID is at least `1000`,
- UID is below `60000`,
- home directory is under `/home/`,
- shell is not `nologin` or `false`.

The greeter does not create users. Raspberry Pi Imager, Debian tooling, or the
base OS owns that lifecycle.

## Authentication Backend

The current production implementation runs `zero-greeter` from `zero-greetd.service`.
When `GREETD_SOCK` is present, `zero-greeter` keeps the same 320x170 UI but
delegates authentication, PAM session creation, seat handling, and session
startup to greetd.

The greetd PAM service uses:

```text
/etc/pam.d/greetd
```

The repository still installs this compatibility PAM file:

```text
/etc/pam.d/zero-greeter
```

That file exists for greeter builds and diagnostics, not as a separate
production login service.

## Session Handoff

After successful authentication, greetd starts:

```text
/usr/local/bin/cardputer-zero-session
```

as the authenticated user.

The old self-managed greeter implementation, where the UI process opened PAM itself and
then dropped privileges, is intentionally not installed as a systemd service.
Wayland/labwc needs greetd/logind to create an active user session for the
internal DRM device.

Historical self-managed behavior was:

- opens a PAM session,
- forks,
- initializes supplementary groups,
- sets gid and uid to the authenticated user,
- sets `USER`, `LOGNAME`, `HOME`, and `SHELL`,
- changes to the user's home directory when possible,
- and execs `/usr/local/bin/cardputer-zero-session`.

The current rule remains: the desktop is never launched as the greeter process.

## Renderer

The greeter renders a small GUI directly on the Cardputer Zero internal
framebuffer and reads keyboard input from Linux input events. It discovers the
internal framebuffer by device name because the fbdev implementation and DRM
tiny panel implementation can expose different `/dev/fbN` indexes.

It is not a replacement for Pi OS `lightdm` on HDMI. HDMI and the normal Pi OS
graphical desktop remain part of the base OS/recovery surface, but LightDM must
not autologin.

## Visual System

The greeter follows the shared Zero visual system documented in
[`zero-visual-system.md`](zero-visual-system.md).

The detailed login, user selector, and power-menu UI behavior is documented in
[`greeter-ui.md`](greeter-ui.md).
