# cardputer-zero-os Intent

本文档定义 `cardputer-zero-os` 的业务意图和边界。

## Core Intent

`cardputer-zero-os` 的核心意图是：

```text
把 Raspberry Pi OS / Debian 变成 Cardputer Zero appliance 的系统 profile。
```

它接管的是用户态启动、登录边界和 session handoff，不接管登录后的 GUI desktop。

## Positive Definition

`cardputer-zero-os` is:

- a Cardputer Zero OS profile,
- a userspace startup experience provider,
- an internal-screen GUI greeter provider,
- a PAM login boundary,
- a user session launcher,
- a device permission profile,
- a restricted root helper provider,
- a recovery profile.

中文：

- Cardputer Zero 系统画像
- 用户态启动体验
- 内屏 GUI 登录界面
- PAM 登录边界
- 用户 session 启动器
- 设备权限配置
- 受限 root helper
- 恢复路径

## Negative Definition

`cardputer-zero-os` is not:

- a full Linux distribution builder,
- a replacement for Raspberry Pi Imager,
- a user account system,
- an autologin profile,
- a post-login launcher,
- a settings app,
- a file manager,
- a terminal UI,
- an app store UI,
- a system monitor UI.

中文：

- 不替代 Pi OS
- 不替代 Raspberry Pi Imager
- 不创建固定 `zero` 用户
- 不强制 autologin
- 不实现 Home 菜单
- 不实现 Settings 页面
- 不实现 File Browser 页面
- 不实现 Terminal 页面
- 不实现 App Store 页面
- 不实现 System Monitor 页面

这些登录后的界面属于 `cardputer-zero-shell` 或单独应用。

## User Ownership

User accounts belong to:

```text
Raspberry Pi OS / Debian / Raspberry Pi Imager
```

`cardputer-zero-os` must not create a fixed `zero` user as the normal path.

The greeter reads existing system users:

- UID >= 1000,
- UID < 60000,
- home under `/home`,
- shell is not `nologin` or `false`.

## Authentication Ownership

Authentication belongs to Linux PAM.

`cardputer-zero-os` provides:

```text
/etc/pam.d/zero-greeter
```

The greeter calls PAM. It does not store passwords and does not implement password policy.

## Session Ownership

`cardputer-zero-os` owns:

```text
/usr/local/bin/cardputer-zero-session
```

because that script defines what the OS profile starts after successful login.

It does not implement the desktop. It execs the configured shell:

```text
/opt/cardputer-zero-shell/bin/zero-shell
```

## Display Ownership

The Zero greeter targets the Cardputer Zero internal display.

It does not own HDMI login and should not disable LightDM or the base OS display manager.

## Privilege Ownership

Privileged operations are routed through:

```text
/usr/local/sbin/zero-helper
```

The helper is intentionally narrow. It must not become:

- arbitrary shell execution,
- arbitrary `sudo`,
- arbitrary `systemctl`,
- arbitrary package installation,
- arbitrary process killing.

## Design Consequences

Because `cardputer-zero-os` is an OS profile:

- it may install system files,
- it may install PAM config,
- it may install systemd units,
- it may install udev rules,
- it may install sudoers policy for `zero-helper`,
- it may build and install `zero-greeter`.

Because it is not the shell:

- it must not hard-code launcher apps,
- it must not scan APPLaunch applications,
- it must not implement desktop pages,
- it must not run the user desktop as root.

