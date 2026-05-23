# zero-polkit-agent

`zero-polkit-agent` is the Cardputer Zero polkit authentication agent.

It registers inside the active Zero user session and opens
`zero-polkit-prompt-wayland`, a fixed-size 320x170 Wayland client, when polkit
needs user authentication for a `zero-helper` action.

It is not a sudo wrapper, not a package manager, and not an app launcher. It
only collects the password required by polkit and passes the response to
`PolkitAgentSession`.

Build requirements:

```sh
sudo apt-get install \
  build-essential pkg-config wayland-protocols libwayland-dev libxkbcommon-dev \
  libglib2.0-dev libpolkit-agent-1-dev libpolkit-gobject-1-dev
```
