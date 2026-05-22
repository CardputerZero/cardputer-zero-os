# zero-greeter

`zero-greeter` is the Cardputer Zero login boundary.

It is intentionally small:

- read existing normal users from the base OS,
- draw a GUI login screen on the Cardputer Zero internal framebuffer,
- accept password input from the device keyboard input events,
- authenticate with PAM service `zero-greeter`,
- open a PAM session,
- drop privileges to the authenticated user,
- and exec `/usr/local/bin/cardputer-zero-session`.

It does not create users, store passwords, implement account policy, or launch
the user desktop as root.

## Build

On Raspberry Pi OS / Debian:

```sh
sudo apt-get install build-essential libpam0g-dev
make
```

The installer runs the equivalent build and installs the binary to:

```text
/usr/local/bin/zero-greeter
```

## Runtime Keys

- `TAB`: cycle users
- `ENTER`: authenticate selected user
- `ESC`: power menu

The renderer targets the Zero internal screen (`/dev/fb0` on current hardware).
It is not a LightDM replacement and does not own HDMI login.
