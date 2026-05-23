# Polkit Authorization

`cardputer-zero-os` uses polkit as the authorization boundary for privileged
operations requested from the logged-in Zero session.

## Flow

```text
normal user app
  -> /usr/local/sbin/zero-helper <allowed-action>
  -> pkexec /usr/local/sbin/zero-helper <allowed-action>
  -> polkit policy check
  -> zero-polkit-agent opens the Zero-sized prompt
  -> root helper performs whitelisted action
```

The application remains a normal user process. The user desktop remains a normal
user process. Only `zero-helper` runs as root after polkit authorizes it.

## Policy

Policy file:

```text
/usr/share/polkit-1/actions/org.cardputerzero.zero-helper.policy
```

Action id:

```text
org.cardputerzero.zero-helper
```

Executable annotation:

```text
/usr/local/sbin/zero-helper
```

## Agent

`zero-polkit-agent` registers with the current user session. In the normal
internal labwc session it opens:

```text
/usr/local/bin/zero-polkit-prompt-wayland
```

That prompt is a small 320x170 Wayland client with app id:

```text
cardputer-zero-polkit
```

This is deliberate. Once the Zero internal screen is owned by DRM/KMS and
labwc, authorization UI must be a compositor-managed window. It must not draw
directly to `/dev/fb*` or pause the compositor.

The older framebuffer prompt remains inside `zero-polkit-agent` only for
explicit legacy framebuffer sessions. The labwc autostart sets:

```text
CARDPUTER_ZERO_POLKIT_PROMPT=wayland
```

so a missing or broken Wayland prompt fails clearly instead of silently falling
back to framebuffer drawing.

Installed files:

```text
/usr/local/bin/zero-polkit-agent
/usr/local/bin/zero-polkit-prompt-wayland
/etc/systemd/user/zero-polkit-agent.service
```

The normal Zero labwc session starts the agent from:

```text
/etc/xdg/cardputer-zero-labwc/autostart
```

This matters because polkit agents register against a specific logind login
session. Starting the agent from SSH, from the wrong user manager, or before
greetd has created the real session can produce:

```text
Passed session and the session the caller is in differs
```

That is polkit doing the right thing. The fix is to start the agent from the
actual labwc session, not to bypass polkit with `sudo`.

## AppStore Contract

AppStore must not run as root and must not call `apt-get` or `dpkg` directly for
privileged package changes. It calls:

```text
/usr/local/sbin/zero-helper appstore install-deb DEB_PATH PACKAGE
/usr/local/sbin/zero-helper appstore remove PACKAGE
/usr/local/sbin/zero-helper appstore repair-dpkg
```

The helper validates package names and only accepts `.deb` files from the
AppStore cache:

```text
/home/*/.local/share/cardputerzero-appstore/cache/downloads/*.deb
/var/cache/cardputerzero-appstore/downloads/*.deb
```

This preserves the boundary:

- AppStore owns catalog and install intent UI.
- `cardputer-zero-os` owns privileged package mutation.
- polkit owns user authorization.

## Forbidden Shape

Do not reintroduce:

- `NOPASSWD` helper sudoers,
- arbitrary `sudo`,
- arbitrary `pkexec sh -c`,
- arbitrary `apt-get`,
- arbitrary `dpkg`,
- running ZeroShell or AppStore as root.
