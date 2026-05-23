# Polkit Authorization

`cardputer-zero-os` uses polkit as the authorization boundary for privileged
operations requested from the logged-in Zero session.

## Flow

```text
normal user app
  -> /usr/local/sbin/zero-helper <allowed-action>
  -> pkexec /usr/local/sbin/zero-helper <allowed-action>
  -> polkit policy check
  -> zero-polkit-agent password prompt
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

`zero-polkit-agent` registers with the current user session and draws an
internal-screen framebuffer password prompt when polkit asks for authentication.

Installed files:

```text
/usr/local/bin/zero-polkit-agent
/etc/systemd/user/zero-polkit-agent.service
```

The normal Zero session starts the agent directly from `cardputer-zero-session`.
This matters because polkit agents register against a specific login session.
A global `systemctl --user` service may belong to a different HDMI/desktop user
manager session and will not authorize the Zero framebuffer session.

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
