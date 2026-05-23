# zero-polkit-agent

`zero-polkit-agent` is the Cardputer Zero internal-screen polkit authentication
agent.

It registers with polkit for the current user session. In the normal
DRM/KMS + labwc session it opens `zero-polkit-prompt-wayland`, a fixed-size
320x170 Wayland client, when a process in that session asks for a privileged
`zero-helper` action through `pkexec`.

The old framebuffer prompt still exists only as an explicit legacy fallback for
framebuffer sessions. The labwc session sets `CARDPUTER_ZERO_POLKIT_PROMPT=wayland`
so a broken Wayland prompt fails visibly instead of silently drawing over the
compositor through `/dev/fb*`.

It is not a sudo wrapper and it does not run applications as root. It only
collects the password required by polkit and passes the response to
`PolkitAgentSession`.

Build requirements on Raspberry Pi OS / Debian:

```sh
sudo apt-get install \
  build-essential pkg-config wayland-protocols libwayland-dev libxkbcommon-dev \
  libglib2.0-dev libpolkit-agent-1-dev libpolkit-gobject-1-dev
```
