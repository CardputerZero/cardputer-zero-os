# cardputer-zero-os

`cardputer-zero-os` is the system profile for running Raspberry Pi OS / Debian as a Cardputer Zero appliance.

中文定义：

`cardputer-zero-os` 是 Cardputer Zero 的 OS profile。它负责把通用 Raspberry Pi OS / Debian 的用户态启动链路接到 Cardputer Zero 设备体验上：Zero splash、Zero 内屏 GUI greeter、PAM 登录、真实用户 session、设备权限、受限 root helper 和恢复路径。

它不实现登录后的桌面。登录后的 GUI Shell 属于 `cardputer-zero-shell`。

```text
Pi OS base
  -> Imager creates user
  -> cardputer-zero-os owns userspace boot/login/session handoff
  -> cardputer-zero-shell owns post-login GUI desktop
```

## Intent

`cardputer-zero-os` 的意图是：

```text
把 Raspberry Pi OS / Debian 变成 Cardputer Zero appliance 的系统 profile，
同时继续尊重 Pi OS 的用户、PAM、LightDM/HDMI 和恢复路径。
```

It provides:

- a minimal Cardputer Zero userspace startup experience,
- an internal-screen Zero GUI greeter,
- PAM-based login for existing system users,
- a Cardputer Zero session launcher,
- device permission setup,
- and a restricted privileged helper.

It does not provide:

- a launcher home screen,
- settings pages,
- file manager UI,
- terminal UI,
- app store UI,
- system monitor UI,
- post-login application UI.

Those belong to `cardputer-zero-shell` or separate user applications.

## Boundary

`cardputer-zero-os` owns:

- quiet boot setup where practical,
- userspace Zero splash,
- internal-screen GUI greeter,
- existing system user discovery,
- PAM authentication,
- PAM session opening,
- dropping to the authenticated user,
- launching `cardputer-zero-session`,
- device permission rules,
- `zero-helper`,
- recovery fallback.

`cardputer-zero-os` does not own:

- Raspberry Pi Imager user creation,
- password storage,
- a custom account database,
- fixed `zero` user creation,
- autologin,
- HDMI LightDM replacement,
- post-login launcher pages,
- app discovery,
- app launching,
- app store implementation,
- user desktop as root.

More detail: [docs/intent.md](docs/intent.md)

## Boot/Login Flow

```text
systemd boot
  -> zero-splash.service
  -> zero-greeter.service
  -> PAM authenticate existing user
  -> cardputer-zero-session
  -> /opt/cardputer-zero-shell/bin/zero-shell
```

Key rule:

The greeter may run as root because it is a login component. The user desktop must run as the authenticated user.

More detail: [docs/boot-flow.md](docs/boot-flow.md)

## HDMI Rule

The Zero greeter is for the Cardputer Zero internal screen.

It does not replace HDMI login. `lightdm`, `display-manager`, and `getty@tty1` can remain available as Pi OS / HDMI / recovery surfaces.

More detail: [docs/greeter.md](docs/greeter.md)

## Install

Build and install on the target Raspberry Pi OS / Debian system:

```sh
sudo ./install.sh
```

This installs:

- `/usr/local/bin/zero-splash`
- `/usr/local/bin/zero-greeter`
- `/usr/local/bin/cardputer-zero-session`
- `/usr/local/sbin/zero-helper`
- `/etc/pam.d/zero-greeter`
- `/etc/systemd/system/zero-splash.service`
- `/etc/systemd/system/zero-greeter.service`
- `/etc/sudoers.d/cardputer-zero`
- `/etc/udev/rules.d/99-cardputer-zero.rules`
- `/etc/cardputer-zero/*.conf`

It enables:

- `zero-splash.service`
- `zero-greeter.service`

It does not disable:

- `lightdm`
- `display-manager`
- `getty@tty1`

More detail: [docs/install.md](docs/install.md)

## Uninstall

```sh
sudo ./uninstall.sh
```

Uninstall disables Zero services and removes installed profile files. It does not delete users or remove user groups.

## Relationship To ZeroShell

`cardputer-zero-os` starts:

```text
/usr/local/bin/cardputer-zero-session
```

The session script starts:

```text
/opt/cardputer-zero-shell/bin/zero-shell
```

If the shell is missing, the session script starts the user's login shell for recovery.

More detail: [docs/zero-shell-interface.md](docs/zero-shell-interface.md)

## Documentation

- [docs/intent.md](docs/intent.md)
- [docs/boot-flow.md](docs/boot-flow.md)
- [docs/splash.md](docs/splash.md)
- [docs/greeter.md](docs/greeter.md)
- [docs/session.md](docs/session.md)
- [docs/permissions.md](docs/permissions.md)
- [docs/zero-helper.md](docs/zero-helper.md)
- [docs/recovery.md](docs/recovery.md)
- [docs/install.md](docs/install.md)
- [docs/zero-shell-interface.md](docs/zero-shell-interface.md)
- [docs/spec.md](docs/spec.md)

