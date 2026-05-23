# cardputer-zero-os

`cardputer-zero-os` is the Cardputer Zero system profile for Raspberry Pi OS /
Debian.

中文定义：

`cardputer-zero-os` 不是桌面，不是 launcher，也不是 app。它负责把通用
Raspberry Pi OS / Debian 的启动、登录、权限、显示会话和恢复路径组织成
Cardputer Zero 设备体验。

它的边界是：

```text
Raspberry Pi OS / Debian base
  -> Raspberry Pi Imager creates normal Linux users
  -> cardputer-zero-os owns internal-screen boot/login/session policy
  -> cardputer-zero-shell owns the post-login launcher/task UI
  -> applications own their own UI
```

它不创建固定 `zero` 用户，不保存密码，不做 autologin，不替代 HDMI 上的
Pi OS 桌面，也不实现登录后的 launcher、settings、file browser、terminal、
app store 或 system monitor。这些属于 `cardputer-zero-shell` 或独立应用。

## Why This Exists

Cardputer Zero 的内屏不是普通 HDMI 显示器。它是一个 SPI ST7789 小屏，分辨率
是 `320x170` 级别，输入也是设备上的小键盘。直接把 Pi OS 桌面铺到这个屏幕上
会得到错误的产品形态：

- HDMI 桌面会被裁到小屏上；
- LightDM/labwc 可能把鼠标、桌面或登录框画到内屏；
- direct framebuffer app 会互相抢 `/dev/fb0`；
- Shell 无法标准地最小化、切换或关闭应用；
- 自己写 PAM/login/session 又容易绕开 Linux 的 logind/seat 机制。

这个工程的目标不是重新发明 Linux 桌面，而是把 Zero 内屏接回 Linux 标准图形
和登录链路：

```text
ST7789 internal display
  -> DRM/KMS output
  -> greetd creates an authenticated logind session
  -> labwc owns the internal output as compositor
  -> ZeroShell runs as a Wayland client
  -> apps run as Wayland/Xwayland windows
```

这条路线让“登录是谁负责的”“窗口是谁负责的”“权限是谁负责的”都回到 Linux
已有机制里，而不是让 Shell 用 root 权限猜进程、抢屏幕或自己当窗口管理器。

## Current UI

![Cardputer Zero OS greeter](docs/assets/zero-os-current-greeter-320x170.png)

这是 Zero 内屏上的登录界面。它只负责登录前的系统入口，不负责登录后的桌面。

## High-Level Timing

当前已验证的启动和登录时序是：

```text
systemd boot
  -> zero-splash.service
  -> zero-greetd.service on VT8
  -> greetd starts /usr/local/bin/zero-greeter as _greetd
  -> zero-greeter draws the 320x170 login UI on the internal framebuffer
  -> user selects an existing Linux account and enters password
  -> zero-greeter talks to greetd over GREETD_SOCK
  -> greetd performs PAM authentication and opens the user session
  -> greetd starts /usr/local/bin/cardputer-zero-session as that user
  -> cardputer-zero-session starts the labwc-based Zero session
  -> cardputer-zero-labwc-session constrains wlroots to the internal DRM card
  -> labwc starts /opt/cardputer-zero-shell/bin/zero-shell-wayland
  -> ZeroShell launches Wayland/Xwayland apps from APPLaunch desktop entries
```

Important rule:

```text
zero-greeter may run as a greeter.
ZeroShell and user apps must run as the authenticated user.
```

The expected runtime shape is:

```text
lightdm  /usr/bin/labwc -C /etc/xdg/labwc-greeter/
pi       /usr/bin/labwc -C /etc/xdg/cardputer-zero-labwc -S /opt/cardputer-zero-shell/bin/zero-shell-wayland
pi       /opt/cardputer-zero-shell/bin/zero-shell-wayland
pi       python3 /usr/local/bin/zero-key-policy
```

Not acceptable:

```text
root     /opt/cardputer-zero-shell/bin/zero-shell-wayland
root     user applications
```

## Linux Mechanisms Used

### Raspberry Pi Imager And Linux Users

Users are owned by the base OS. Raspberry Pi Imager or normal Linux tools create
the account, password, WiFi, SSH settings, and groups.

`cardputer-zero-os` discovers ordinary users from `/etc/passwd`:

- UID is normal user range,
- home directory is under `/home`,
- shell is not `nologin` or `false`.

It does not create a hard-coded `zero` user.

### PAM And greetd

The first self-managed greeter implementation authenticated with PAM directly,
but that was not enough for the DRM/KMS + labwc stack. A wlroots compositor
needs an active logind/seat/TTY session so it can open the DRM device
correctly.

The current implementation therefore uses `greetd` as the session authority:

```text
zero-greeter UI
  -> greetd IPC
  -> PAM authentication handled by greetd
  -> logind session created by greetd
  -> cardputer-zero-session runs as the user
```

This keeps the custom 320x170 Zero login UI while relying on a standard Linux
display-manager style backend for authentication and session creation.

### DRM/KMS Instead Of fbdev For The Internal Screen

The original Cardputer Zero display implementation exposed the ST7789 screen through
`fb_st7789v_m5stack` / `fbtft` as a framebuffer device. That is fine for a
single program drawing directly to `/dev/fb0`, but it is not a normal compositor
output.

`labwc` and wlroots manage DRM/KMS outputs. They cannot standardly minimize or
stack apps that draw directly to fbdev. Therefore this profile exposes the
internal screen through the Linux DRM/KMS stack using `panel-mipi-dbi-spi`.

The verified direction is:

```text
old:
  ST7789 -> fbtft/fbdev -> /dev/fb0 -> one direct drawing program

new:
  ST7789 -> panel-mipi-dbi-spi -> DRM connector SPI-1 -> labwc -> Wayland/Xwayland clients
```

### Device Tree Overlay Strategy

The KMS setup deliberately does not replace the whole existing
`cardputerzero-overlay.dtbo`.

The base overlay still owns non-display hardware such as:

- keyboard,
- audio,
- M5 IO expander,
- sensors,
- backlight-related nodes.

The added display overlay is small:

```text
/boot/firmware/overlays/cardputerzero-kms-display.dtbo
```

It does two display-specific things:

1. disables the old SPI `st7789v@0` fbdev display node;
2. adds a new SPI `display@0` node compatible with `panel-mipi-dbi-spi`.

The corresponding panel firmware is:

```text
/lib/firmware/cardputerzero,st7789v.bin
```

That firmware contains the ST7789 initialization command stream expected by
Linux `panel-mipi-dbi`. It preserves the old usable orientation:

```text
user view: 320x170
controller: MADCTL = MV | MY = 0xa0
offset: 35-pixel panel glass offset
```

The kernel module is loaded explicitly through:

```text
/etc/modules-load.d/cardputer-zero-kms.conf
```

Reason: the first compatible string is kept as `cardputerzero,st7789v` so the
driver requests the firmware name `cardputerzero,st7789v.bin`; on the verified
Pi OS kernel that modalias does not always auto-load the generic
`panel_mipi_dbi` module.

### HDMI Stays Separate

HDMI remains a Pi OS / LightDM / recovery surface. The Zero internal greeter is
only for the internal screen.

Two stable udev symlinks are created:

```text
/dev/dri/cardputer-zero-internal  -> Zero SPI panel DRM card
/dev/dri/cardputer-zero-hdmi      -> vc4 HDMI/display pipeline DRM card
```

LightDM/labwc for HDMI is constrained with:

```text
WLR_DRM_DEVICES=/dev/dri/cardputer-zero-hdmi
LABWC_FALLBACK_OUTPUT=NOOP-1
```

The Zero session is constrained with:

```text
WLR_DRM_DEVICES=/dev/dri/cardputer-zero-internal
WLR_BACKENDS=drm,libinput
WLR_RENDERER=pixman
```

This prevents the Pi OS desktop or LightDM greeter from drawing a cursor or
desktop onto `SPI-1`.

### labwc Owns Windows

In the DRM/KMS + labwc stack, labwc is the compositor/window manager for the
Zero internal screen. That means task identity is a window/toplevel, not a PID.

This is why LoFiBox and AppStore must become Wayland or Xwayland clients for
standard minimize/switch/close behavior. A direct framebuffer app can still be
a compatibility mode, but labwc cannot minimize something it cannot see as a
window.

### Global Esc/Tab Policy

When a foreground app has keyboard focus, ZeroShell does not receive ordinary
key events. That is normal Wayland behavior. Therefore device-wide shortcuts
belong to the compositor/input-policy layer.

The current policy is:

```text
Tab
  -> labwc keybind calls zero-shell-control tasks
  -> ZeroShell toggles its RUNNING TASKS panel

short Esc
  -> zero-key-policy sees KEY_ESC on the Cardputer keyboard evdev device
  -> zero-shell-control minimize-active
  -> wlrctl minimizes the active toplevel and focuses ZeroShell

long Esc
  -> zero-key-policy sees a long KEY_ESC press
  -> zero-shell-control close-active
  -> wlrctl requests close on the active toplevel and focuses ZeroShell
```

`zero-key-policy` is intentionally narrow. It is not a general hotkey daemon.
It only implements the Zero device-level Esc policy that a normal Wayland client
cannot reliably implement.

### polkit Instead Of Passwordless sudo

Privileged operations should use polkit, not arbitrary `sudo`.

The model is:

```text
user app or shell
  -> pkexec /usr/local/sbin/zero-helper <allowed action>
  -> polkit checks org.cardputerzero.zero-helper.*
  -> zero-polkit-agent opens the Zero-sized Wayland password UI when auth is needed
```

`zero-helper` is intentionally restricted. It does not run arbitrary shell
commands, arbitrary `systemctl`, arbitrary `apt`, or arbitrary process kills.

## File Map

### Entry Points

| File | Role |
| --- | --- |
| `install.sh` | Installs the OS profile files, builds greeter/polkit agent, enables `zero-splash.service` and `zero-greetd.service`, removes legacy `zero-greeter.service` fallback. |
| `uninstall.sh` | Disables Zero services and removes installed profile files while leaving users and groups intact. |
| `README.md` | This overview and mechanism guide. |

### Boot, Login, And Session Files

| File | Role |
| --- | --- |
| `files/etc/systemd/system/zero-splash.service` | Early userspace splash unit before the internal greeter. |
| `files/etc/systemd/system/zero-greetd.service` | Separate internal-screen greetd service on VT8. It does not replace HDMI LightDM. |
| `files/etc/greetd/cardputer-zero.toml` | greetd config that runs `/usr/local/bin/zero-greeter` as `_greetd`. |
| `files/usr/local/bin/zero-splash` | Draws the lightweight startup visual. |
| `greeter/zero-greeter.c` | 320x170 login UI; user selection; password input; greetd IPC when `GREETD_SOCK` exists; legacy direct PAM code is kept for diagnostics but is not the installed production service. |
| `greeter/pam_auth.c` | PAM helper for the legacy/self-managed greeter implementation. |
| `files/etc/pam.d/zero-greeter` | PAM service config for the legacy greeter implementation. greetd normally uses its own PAM service. |
| `files/usr/local/bin/cardputer-zero-session` | The single post-auth session handoff script. It selects `labwc` or explicit legacy `framebuffer` mode. |
| `files/usr/local/bin/cardputer-zero-labwc-session` | Starts labwc on the internal DRM output and then starts `zero-shell-wayland`. |
| `files/etc/cardputer-zero/session.conf` | Session policy: defaults to `CARDPUTER_ZERO_SESSION_MODE=labwc`; framebuffer mode is explicit legacy compatibility only. |

### labwc, Display, And Input Policy

| File | Role |
| --- | --- |
| `files/etc/xdg/cardputer-zero-labwc/environment` | Internal Zero labwc environment: DRM device, renderer, cursor theme, desktop identity. |
| `files/etc/xdg/cardputer-zero-labwc/rc.xml` | labwc config for Zero session; includes the `Tab` keybind to call `zero-shell-control tasks`. |
| `files/etc/xdg/cardputer-zero-labwc/autostart` | Starts `zero-key-policy` and `zero-polkit-agent` inside the real Zero labwc session. |
| `files/usr/local/bin/zero-shell-control` | Small command bridge: writes ZeroShell command files, focuses ZeroShell, asks `wlrctl` to minimize/close/focus toplevels. |
| `files/usr/local/bin/zero-key-policy` | Reads the `tca8418c` evdev keyboard and implements short/long Esc policy. |
| `files/etc/cardputer-zero/lightdm-labwc-environment` | Constrains Pi OS LightDM/labwc to the HDMI DRM card. |
| `files/usr/local/bin/cardputer-zero-lightdm-labwc` | Wrapper used by the LightDM greeter session so HDMI does not claim the internal panel. |
| `files/usr/share/xgreeters/cardputer-zero-pi-greeter-labwc.desktop` | LightDM greeter session entry that points to the HDMI-constrained wrapper. |
| `files/etc/lightdm/lightdm.conf.d/99-cardputer-zero-no-autologin.conf` | Disables LightDM autologin while preserving HDMI as a login/recovery surface. |
| `files/etc/tmpfiles.d/cardputer-zero-xwayland.conf` | Keeps `/tmp/.X11-unix` at standard `root:root 1777` so Xwayland can start in the user session. |

### Device Permissions

| File | Role |
| --- | --- |
| `files/etc/udev/rules.d/99-cardputer-zero.rules` | Input, DRM, render, audio, GPIO, SPI, I2C permissions and stable DRM symlinks for internal/HDMI cards. |
| `scripts/setup-udev.sh` | Creates/uses groups and adds ordinary users to the device groups needed by the Zero session. |

### polkit And Privileged Helper

| File | Role |
| --- | --- |
| `files/usr/local/sbin/zero-helper` | Restricted root helper for approved system actions. |
| `files/usr/share/polkit-1/actions/org.cardputerzero.zero-helper.policy` | polkit action definitions for `zero-helper`. |
| `polkit-agent/zero-polkit-agent.c` | Polkit authentication agent registered inside the active Zero user session. |
| `polkit-agent/zero-polkit-prompt-wayland.cpp` | 320x170 Wayland password prompt used by the labwc session. |
| `files/etc/systemd/user/zero-polkit-agent.service` | Legacy/user-service form of the agent; the current session script can also launch the agent directly. |
| `scripts/setup-polkit-agent.sh` | Builds and installs `zero-polkit-agent` and `zero-polkit-prompt-wayland`. |

### KMS/Overlay Scripts

| File | Role |
| --- | --- |
| `scripts/probe-graphics-stack.sh` | Prints kernel, boot config, framebuffer, DRM, SPI, modules, overlays, and labwc facts. |
| `scripts/setup-kms-experimental.sh` | Installs the KMS display overlay, panel firmware, module-load config, and edits boot config with backup. |
| `scripts/build-st7789v-panel-firmware.sh` | Generates `/lib/firmware/cardputerzero,st7789v.bin` for `panel-mipi-dbi`. |
| `scripts/restore-kms-experimental.sh` | Restores the previous boot display configuration from `/var/backups/cardputer-zero-os/kms-experimental`. |
| `scripts/probe-labwc-session.sh` | Inspects labwc/logind/session state. |
| `scripts/test-labwc-internal-session.sh` | One-shot internal labwc smoke test without changing the login flow. |
| `scripts/test-greetd-labwc-session.sh` | Human-in-the-loop greetd/labwc test with automatic config rollback. |
| `scripts/test-greetd-labwc-visible-client.sh` | Visible client test for the internal labwc session. |

### Documentation

| File | Role |
| --- | --- |
| `docs/boot-flow.md` | Boot and login sequence. |
| `docs/greeter.md` | Greeter design and greetd relationship. |
| `docs/session.md` | Session handoff and labwc/framebuffer mode policy. |
| `docs/kms-labwc.md` | Detailed KMS/labwc bring-up notes and verified findings. |
| `docs/permissions.md` | Device permissions and groups. |
| `docs/polkit.md` | polkit/zero-helper/agent model. |
| `docs/recovery.md` | SSH/HDMI recovery procedures. |
| `docs/zero-shell-interface.md` | Contract with `cardputer-zero-shell`. |
| `docs/spec.md` | OS profile specification. |

## Install

On the target Raspberry Pi OS / Debian system:

```sh
sudo ./install.sh
```

Required packages include the normal build tools plus the runtime pieces used by
this profile:

```sh
sudo apt-get install \
  build-essential pkg-config greetd wlrctl labwc wayland-protocols \
  libpam0g-dev libglib2.0-dev libpolkit-agent-1-dev libpolkit-gobject-1-dev \
  libwayland-dev libxkbcommon-dev
```

For the KMS overlay experiment:

```sh
sudo apt-get install device-tree-compiler
sudo sh ./scripts/probe-graphics-stack.sh
sudo sh ./scripts/setup-kms-experimental.sh
sudo reboot
```

The KMS setup writes backups under:

```text
/var/backups/cardputer-zero-os/kms-experimental/
```

Restore the previous boot display configuration with:

```sh
sudo sh ./scripts/restore-kms-experimental.sh
sudo reboot
```

## Verify

After boot and login:

```sh
systemctl is-enabled zero-greetd.service
systemctl status zero-greetd.service
test ! -e /etc/systemd/system/zero-greeter.service
```

Check display separation:

```sh
ls -l /dev/dri/cardputer-zero-internal /dev/dri/cardputer-zero-hdmi
wlr-randr
```

Check runtime identity:

```sh
ps -eo user,pid,args | grep -E 'labwc|zero-shell-wayland|zero-key-policy'
```

Check task visibility:

```sh
XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0 wlrctl toplevel list
```

Expected examples on the verified device:

```text
cardputer-zero-shell: Cardputer Zero Shell
: LoFiBox Zero
```

The empty app_id for LoFiBox is an Xwayland compatibility limitation. Long-term
Wayland-native apps should set a stable app_id.

## Recovery

If the internal-screen session fails:

1. use SSH, or use HDMI LightDM as the recovery surface;
2. disable the internal greetd service if needed:

```sh
sudo systemctl disable --now zero-greetd.service
```

3. restore the KMS overlay if the display driver experiment is the suspected
   issue:

```sh
sudo sh ./scripts/restore-kms-experimental.sh
sudo reboot
```

There is intentionally no automatic fallback from the DRM/KMS + labwc stack into
the legacy framebuffer shell. Silent fallback makes debugging impossible because
it hides whether the intended session is actually running.

## Relationship To ZeroShell

`cardputer-zero-os` starts ZeroShell; it does not implement ZeroShell.

The interface is:

```text
cardputer-zero-os
  -> creates authenticated user session
  -> starts labwc on the internal DRM output
  -> starts /opt/cardputer-zero-shell/bin/zero-shell-wayland

cardputer-zero-shell
  -> scans /usr/share/APPLaunch/applications/*.desktop
  -> draws launcher/task UI as a Wayland client
  -> launches APPLaunch-compatible applications
```

Applications that want standard task behavior should be Wayland or Xwayland
windows. Direct framebuffer apps are legacy compatibility apps and cannot be
managed by labwc as windows.

## Uninstall

```sh
sudo ./uninstall.sh
```

Uninstall disables/removes Zero profile services and files. It does not delete
Linux users, and it does not remove groups created for device permissions.

## Documentation Index

- [docs/intent.md](docs/intent.md)
- [docs/boot-flow.md](docs/boot-flow.md)
- [docs/splash.md](docs/splash.md)
- [docs/greeter.md](docs/greeter.md)
- [docs/session.md](docs/session.md)
- [docs/permissions.md](docs/permissions.md)
- [docs/zero-helper.md](docs/zero-helper.md)
- [docs/polkit.md](docs/polkit.md)
- [docs/recovery.md](docs/recovery.md)
- [docs/install.md](docs/install.md)
- [docs/kms-labwc.md](docs/kms-labwc.md)
- [docs/zero-shell-interface.md](docs/zero-shell-interface.md)
- [docs/spec.md](docs/spec.md)
