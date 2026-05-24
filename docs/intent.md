# cardputer-zero-os Intent

`cardputer-zero-os` is the system profile that makes Raspberry Pi OS / Debian
behave like a Cardputer Zero appliance on the internal screen.

It owns:

- internal-screen display setup,
- login UI,
- PAM-backed authentication for existing Linux users,
- real user session handoff,
- device permissions,
- restricted privileged helper policy,
- polkit authentication UI,
- recovery surfaces.

It does not own:

- user creation,
- autologin,
- post-login launcher UI,
- app scanning,
- app store UI,
- file manager UI,
- terminal UI,
- settings UI,
- system monitor UI.

Those belong to Raspberry Pi OS / Debian, `cardputer-zero-shell`, or separate
apps.

## Users

User accounts belong to:

```text
Raspberry Pi OS / Debian / Raspberry Pi Imager
```

The greeter reads existing normal users. The profile does not create a fixed
Zero user.

## Authentication

Authentication belongs to Linux PAM. The visible greeter is not root; it calls a
restricted root helper that owns the authentication and session-creation
boundary:

```text
zero-greeter-wayland
  -> zero-greeter-auth
  -> PAM service cardputer-zero-login
  -> systemd/logind user session
```

The greeter does not store passwords.

## Session

The OS profile starts:

```text
/usr/local/bin/cardputer-zero-session
```

which starts the labwc user session and then:

```text
/opt/cardputer-zero-shell/bin/zero-shell-wayland
```

## Display

The internal display belongs to the DRM/KMS + labwc stack. HDMI remains a normal
Pi OS login/recovery surface.

## Privilege

Privileged actions go through:

```text
pkexec /usr/local/sbin/zero-helper <allowed-action>
```

The helper is intentionally narrow and must not become arbitrary root command
execution.
