# wallpaper-runner

`wallpaper-runner` is a native Linux/Wayland video wallpaper player intended as a practical replacement for `mpvpaper`.

It uses:
- `gtk4-layer-shell` to place the window in the compositor background layer
- GTK media backend (GStreamer on Linux) to play and loop `mp4`

## Features

- Runs as true layer-shell background (not a normal toplevel window)
- Loops MP4 forever
- Audio muted by default
- Monitor selection (`auto`, connector/model, or index)
- Graceful shutdown on `SIGINT` / `SIGTERM`

## Build

```bash
zig build -Doptimize=ReleaseFast
```

## Nix Flake

Build package:

```bash
nix build .
```

Run directly from flake:

```bash
nix run . -- --video ~/.config/wallpapers/wallpaper.mp4 --monitor auto
```

Use in another flake:

```nix
inputs.wallpaper-runner.url = "github:rakhimgaliyev/wallpaper-runner";

# later in a module
environment.systemPackages = [ inputs.wallpaper-runner.packages.${pkgs.system}.default ];
```

Binary:

```bash
./zig-out/bin/wallpaper-runner
```

## Usage

```bash
wallpaper-runner --video ~/.config/wallpapers/wallpaper.mp4 --monitor auto
```

or:

```bash
wallpaper-runner ~/.config/wallpapers/wallpaper.mp4
```

Options:
- `-f, --video <path>`: MP4 file path
- `-m, --monitor <value>`: `auto`, monitor connector/model (e.g. `DP-2`), or numeric index (`0`, `1`, ...)
- `-v, --verbose`: detailed logs
- `-V, --version`: print version
- `-h, --help`: help

## Hyprland autostart example

```ini
exec-once = /home/user/path/to/wallpaper-runner/zig-out/bin/wallpaper-runner --video /home/user/.config/wallpapers/wallpaper.mp4 --monitor auto
```

## Runtime dependencies (NixOS)

You need GTK4 + layer-shell + media plugins at runtime.

Typical package set:
- `gtk4`
- `gtk4-layer-shell`
- `gstreamer`
- `gst-plugins-base`
- `gst-plugins-good`
- `gst-plugins-bad`
- `gst-libav`

If your GPU decode path is available in GStreamer stack, it will be used by the backend.
