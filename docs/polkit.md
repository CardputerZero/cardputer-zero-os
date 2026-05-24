# Polkit Authorization

`cardputer-zero-os` uses polkit as the authorization boundary for privileged
operations requested from the logged-in Zero session.

## Flow

```text
normal user app
  -> pkexec /usr/local/sbin/zero-helper <allowed-action>
  -> polkit policy check
  -> zero-polkit-agent opens zero-polkit-prompt-wayland
  -> root helper performs the whitelisted action
```

The app remains a normal user process. Only `zero-helper` runs as root after
polkit authorizes the action.

## Policy

Policy file:

```text
/usr/share/polkit-1/actions/org.cardputerzero.zero-helper.policy
```

Executable annotation:

```text
/usr/local/sbin/zero-helper
```

## Agent

`zero-polkit-agent` registers inside the active Zero user session. It opens:

```text
/usr/local/bin/zero-polkit-prompt-wayland
```

That prompt is a 320x170 Wayland client with app id:

```text
cardputer-zero-polkit
```

The normal Zero labwc session starts the agent from:

```text
/etc/xdg/cardputer-zero-labwc/autostart
```

This matters because polkit agents register against a specific logind session.
Starting the agent from SSH or before the Zero login flow creates the real user
session can make polkit reject the registration.

## AppStore Contract

AppStore must not run as root. It calls the helper for package mutations:

```text
pkexec /usr/local/sbin/zero-helper appstore install-deb DEB_PATH PACKAGE
pkexec /usr/local/sbin/zero-helper appstore remove PACKAGE
pkexec /usr/local/sbin/zero-helper appstore repair-dpkg
```

The helper validates package names and accepted cache paths.

## Forbidden Shape

Do not reintroduce:

- passwordless helper sudoers,
- arbitrary `sudo`,
- arbitrary `pkexec sh -c`,
- arbitrary `apt-get`,
- arbitrary `dpkg`,
- running ZeroShell or AppStore as root.
