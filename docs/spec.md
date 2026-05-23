# cardputer-zero-os Specification

本文档定义当前 `cardputer-zero-os` profile 的规格。

## Scope

In scope:

- userspace splash,
- internal-screen GUI greeter,
- greetd/PAM authentication for existing users,
- real user session launch,
- Cardputer Zero session script,
- device permission setup,
- restricted privileged helper,
- recovery fallback,
- quiet boot tuning where practical.

Out of scope:

- creating users,
- autologin,
- replacing Pi firmware output,
- replacing all kernel output,
- replacing HDMI LightDM login,
- post-login GUI launcher,
- app scanner,
- app store,
- file manager,
- terminal UI,
- settings UI,
- system monitor UI.

## Components

### zero-splash

Path:

```text
/usr/local/bin/zero-splash
```

Role:

- draw a lightweight userspace startup visual on internal framebuffer,
- hold visual continuity before greeter starts.

Non-role:

- firmware splash,
- total kernel-output suppression,
- HDMI display manager.

### zero-greeter

Path:

```text
/usr/local/bin/zero-greeter
```

Role:

- discover existing normal users,
- render an internal-screen Zero-style GUI login,
- accept password input,
- act as a greetd frontend when `GREETD_SOCK` is present,
- ask greetd to authenticate through PAM,
- ask greetd to start `cardputer-zero-session`.

Non-role:

- launcher,
- shell,
- password database,
- user manager,
- HDMI LightDM replacement.

The self-managed `zero-greeter.service` systemd unit is not part of the current
architecture and must not be installed. `zero-greeter` is a program;
`zero-greetd.service` is the internal-screen login backend.

### PAM Service

Path:

```text
/etc/pam.d/zero-greeter
```

Role:

- delegate authentication/account/session policy to base OS PAM stack.

### cardputer-zero-session

Path:

```text
/usr/local/bin/cardputer-zero-session
```

Role:

- define post-login Cardputer Zero session environment,
- start the configured session mode,
- default to the Wayland/labwc session,
- allow direct-framebuffer ZeroShell only when explicitly configured.

### zero-helper

Path:

```text
/usr/local/sbin/zero-helper
```

Role:

- provide narrow root operations for logged-in session UI.

### zero-polkit-agent

Path:

```text
/usr/local/bin/zero-polkit-agent
```

Role:

- register as a polkit authentication agent for the logged-in user session,
- render a small internal-screen authorization prompt,
- pass user-entered credentials to `PolkitAgentSession`.

Non-role:

- sudo wrapper,
- app launcher,
- password database,
- package manager UI.

### udev Rules

Path:

```text
/etc/udev/rules.d/99-cardputer-zero.rules
```

Role:

- assign relevant device groups,
- add `uaccess` tags where useful.

## systemd Specification

`zero-splash.service`:

- `DefaultDependencies=no`
- `After=local-fs.target`
- `Before=zero-greetd.service`
- `WantedBy=multi-user.target`

`zero-greetd.service`:

- `After=systemd-user-sessions.service zero-splash.service`
- `Restart=always`
- starts `/usr/sbin/greetd -c /etc/greetd/cardputer-zero.toml --vt 8`,
- does not conflict with `lightdm` or `getty@tty1`.

## User Specification

The greeter shows normal interactive users:

- UID >= 1000,
- UID < 60000,
- home under `/home`,
- shell not matching `nologin` or `false`.

It does not create users.

## Security Specification

The profile must:

- authenticate via PAM,
- open a PAM session,
- initialize supplementary groups,
- drop privileges before launching user session,
- keep the user desktop out of root,
- pass privileged helper requests through polkit/pkexec,
- restrict helper actions to explicit subcommands.

The profile must not:

- store passwords,
- implement custom password auth,
- run ZeroShell as root,
- grant arbitrary sudo,
- install NOPASSWD helper sudoers entries,
- launch arbitrary root commands from UI.

## HDMI Specification

The Zero greeter is internal-screen only.

The profile should not disable:

- `lightdm`,
- `display-manager`,
- `getty@tty1`.

HDMI login remains a base OS/recovery surface.

## Recovery Specification

Recovery surfaces:

- SSH,
- HDMI / base OS display manager,
- `getty@tty1`,
- disabling `zero-greetd.service`,
- restoring `cmdline.txt.cardputer-zero.bak`.

## Visual Specification

The greeter and shell share the visual system in
[`zero-visual-system.md`](zero-visual-system.md), but keep separate semantics:

- `cardputer-zero-os` renders login, user selection, PAM status, and power.
- `cardputer-zero-shell` renders the post-login desktop, task UI, app cards,
  and application launch flow.
