# zero-polkit-agent

`zero-polkit-agent` is the Cardputer Zero internal-screen polkit authentication
agent.

It registers with polkit for the current user session and draws a small
framebuffer password prompt when a process in that session asks for a privileged
`zero-helper` action through `pkexec`.

It is not a sudo wrapper and it does not run applications as root. It only
collects the password required by polkit and passes the response to
`PolkitAgentSession`.

Build requirements on Raspberry Pi OS / Debian:

```sh
sudo apt-get install build-essential pkg-config libglib2.0-dev libpolkit-agent-1-dev
```
