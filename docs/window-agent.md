# Zero Window Agent

`zero-window-agent` is the Cardputer Zero user-session bridge between labwc and
launcher/task UI clients.

It exists because a Zero task is a compositor-managed Wayland/Xwayland
toplevel. A task is not a PID, process group, desktop entry, package, or launch
command.

## Ownership

`zero-window-agent` belongs to `cardputer-zero-os`.

It is OS/session infrastructure because it depends on the internal labwc
session, Wayland globals, and window-management protocols. It does not own
launcher UI, APPLaunch application discovery, icons, application ordering, or
desktop-entry matching. Those belong to `cardputer-zero-shell`.

## Hard Constraints

- Production task state must come from `zero-window-agent`.
- Production task actions must go through `zero-window-agent`.
- `wlrctl` is diagnostic tooling only.
- No production path may parse `wlrctl toplevel list`.
- No production path may use `wlrctl toplevel focus`, `find`, `close`, or
  equivalent commands as a fallback.
- No task identity may be derived from PID, process group, parent process, or
  Shell child process state.
- No task identity may be derived from `.desktop` files alone.
- The agent must not parse APPLaunch `.desktop` files.
- The agent must not know Shell icon, order, or application catalog policy.
- The agent must not store passwords, ask for authorization, or invoke sudo.
- Failure must be explicit. If the agent is offline, clients must report an
  offline task backend instead of silently falling back to another source.

These constraints exist to prevent the system from sliding back into launcher
process guessing or command-output scraping.

## Protocol Source

The first implemented backend is:

```text
wlr-foreign-toplevel-management-unstable-v1
```

The agent listens for compositor toplevel add/update/remove events and converts
them into a small local socket protocol. A future `ext-foreign-toplevel` backend
may be added, but it must preserve the same agent contract.

## Socket

The agent listens on:

```text
/run/user/$UID/cardputer-zero/window-agent.sock
```

The socket is owned by the authenticated login user. It is not a system socket
and is not a privileged helper.

`XDG_RUNTIME_DIR` is required. The agent must not create a fallback socket under
`/tmp` or another shared directory, because that would bypass the authenticated
user-session boundary.

The wire protocol is `ZWA1`, a newline-delimited, tab-separated text protocol.
Fields after the verb are percent-encoded UTF-8 strings.

Encoding rules:

- Literal tab, newline, carriage return, percent, and other non-printable bytes
  are encoded as `%HH`.
- Printable ASCII other than `%` and tab may be sent literally.
- Empty fields are allowed.

## Client Commands

```text
hello
list
subscribe
activate<TAB>task-id
minimize<TAB>task-id
restore<TAB>task-id
close<TAB>task-id
focus-shell
minimize-active
close-active
active-is-shell
```

`subscribe` sends a snapshot immediately and then streams later snapshots when
the compositor state changes.

`focus-shell`, `minimize-active`, `close-active`, and `active-is-shell` exist so
global key policy can operate through the same authoritative task source as the
Shell.

## Server Messages

```text
hello<TAB>ZWA1<TAB>zero-window-agent
ok<TAB>request
error<TAB>code<TAB>message
snapshot-begin
task<TAB>id<TAB>app-id<TAB>title<TAB>state
snapshot-end
```

`state` is a comma-separated set:

```text
activated,minimized,maximized,fullscreen
```

The set may be empty.

Task ids are session-local. They are stable only while the same agent process
tracks the same compositor toplevel handle.

## Action Semantics

`activate` asks the compositor to activate a toplevel.

`minimize` asks the compositor to minimize a toplevel.

`restore` asks the compositor to unset minimized state and activate the
toplevel.

`close` asks the compositor to close a toplevel. It is not a process kill.

`minimize-active` minimizes the active non-Shell toplevel.

`close-active` closes the active non-Shell toplevel.

`focus-shell` activates the `cardputer-zero-shell` toplevel.

`active-is-shell` succeeds only when the active toplevel is the Shell.

## Startup

The internal labwc session starts a small wrapper:

```text
/usr/local/bin/cardputer-zero-shell-session
```

The wrapper:

1. starts `zero-window-agent`,
2. waits briefly for `window-agent.sock`,
3. then execs `/opt/cardputer-zero-shell/bin/zero-shell-wayland`.

This gives both the agent and the Shell the same Wayland session environment and
prevents Shell startup from racing a missing window-state backend.

If `zero-window-agent` is not installed or the socket does not appear during
session startup, the wrapper exits non-zero. Startup failure is preferable to a
hidden compatibility path, because the internal-screen session has no alternate
production task backend.

## Failure Behavior

If the protocol global is missing, the agent must exit non-zero and log a clear
message.

If the agent exits after Shell is already running, Shell must show an offline
task backend state. It must not start parsing `wlrctl`.

If a requested task id no longer exists, the agent returns:

```text
error<TAB>not-found<TAB>task not found
```

If the compositor does not support a requested action, the agent returns an
explicit error. It must not emulate the action by guessing a PID.

## Diagnostic Boundary

Diagnostic scripts may still call:

```text
wlrctl toplevel list
```

They must label the result as diagnostic compositor output. Diagnostic output is
not a production task-state source.
