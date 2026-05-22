# Greeter

`zero-greeter` is the core OS component in this profile.

It is a login manager, not a launcher. Its job ends once an authenticated user
session has been started.

## User Discovery

The greeter reads users from the base OS account database and displays normal
interactive users:

- UID is at least `1000`,
- UID is below `60000`,
- home directory is under `/home/`,
- shell is not `nologin` or `false`.

The greeter does not create users. Raspberry Pi Imager, Debian tooling, or the
base OS owns that lifecycle.

## Authentication

The greeter authenticates with PAM service `zero-greeter`.

Current PAM file:

```text
/etc/pam.d/zero-greeter
```

The initial Debian/Pi OS-oriented stack includes `common-auth`,
`common-account`, `common-password`, and `common-session`.

## Session Handoff

After successful authentication, the greeter:

- opens a PAM session,
- forks,
- initializes supplementary groups,
- sets gid and uid to the authenticated user,
- sets `USER`, `LOGNAME`, `HOME`, and `SHELL`,
- changes to the user's home directory when possible,
- and execs `/usr/local/bin/cardputer-zero-session`.

The desktop is never launched as the greeter's root process.

## Renderer

The greeter renders a small GUI directly on the Cardputer Zero internal
framebuffer (`/dev/fb0` on current hardware) and reads keyboard input from
Linux input events.

It is not a replacement for Pi OS `lightdm` on HDMI. HDMI and the normal Pi OS
graphical desktop remain part of the base OS/recovery surface.
