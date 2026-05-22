# Greeter UI

`zero-greeter` is the Cardputer Zero login surface for the internal screen. It
is intentionally styled like ZeroShell, but it is not a shell and does not
launch applications.

## Login View

The login view contains:

- TopBar with time, `LOGIN`, and battery,
- a centered login panel,
- current system user,
- password dots and cursor,
- PAM status line,
- BottomBar with `POWER`, `TAB USER`, `ENTER LOGIN`, and `ESC`.

Normal status:

```text
PAM AUTHENTICATION
```

Failed authentication:

```text
AUTH FAILED
```

The failure state stays in-place by changing the password border and status
line to Warn Red. It does not open a modal error dialog.

## User Selector

`TAB` opens the system-user popup.

The popup lists normal interactive users discovered from the base OS:

- UID is at least `1000`,
- UID is below `60000`,
- home is under `/home/`,
- shell does not contain `nologin` or `false`.

Navigation:

- `TAB`, `Down`, or `Right`: next user,
- `Up` or `Left`: previous user,
- `Enter`: select user,
- `Esc`: close without changing.

The greeter never creates users. Raspberry Pi Imager, Debian tools, or the
base OS own user creation.

## Power Menu

`Esc` opens the power menu. The menu contains:

- `SHUTDOWN`,
- `REBOOT`,
- `CANCEL`.

Navigation:

- `Down`, `Right`, or `TAB`: next item,
- `Up` or `Left`: previous item,
- `Enter`: confirm,
- `Esc`: close.

Power actions call `zero-helper`:

```text
/usr/local/sbin/zero-helper shutdown
/usr/local/sbin/zero-helper reboot
```

The greeter does not run arbitrary shell commands or systemctl actions.

## Session Handoff

On successful PAM authentication, the greeter:

- opens a PAM session,
- initializes supplementary groups,
- switches to the authenticated user's gid and uid,
- applies PAM environment where available,
- sets `USER`, `LOGNAME`, `HOME`, and `SHELL`,
- execs `/usr/local/bin/cardputer-zero-session`.

The post-login desktop is provided by `cardputer-zero-shell`, not by the
greeter.
