# ani2xcursor

Convert Windows animated cursor themes (.ani/.cur) to Linux Xcursor format.

## Features

- Full .ani/.cur format support
- Multi-size export with auto-rescale for missing target sizes
- Optional source output, enable further processing.
- Manifest mode: generate cursor previews + manifest template, then customize roles and sizes (primary configuration)
  ### Screenshots
  cursor preview:

  <img width="798" height="277" alt="图片" src="https://github.com/user-attachments/assets/ea4983dd-e476-4212-9c23-1704d8d39364" />

  corrupted cursor preview:

  <img width="798" height="277" alt="output" src="https://github.com/user-attachments/assets/90b56fa2-6b11-4fde-8f30-d7ea2448c5dd" />

## Installation

### Releases
Download an AppImage from [releases](https://github.com/yuzujr/ani2xcursor/releases).

### Arch Linux (AUR)
* `ani2xcursor` — build from source
* `ani2xcursor-bin` — AppImage

Install one of them:

use `paru`:
```bash
paru -S ani2xcursor
# or
paru -S ani2xcursor-bin
```

or use `yay`:
```bash
yay -S ani2xcursor
# or
yay -S ani2xcursor-bin
```

### NixOS

Add to your `flake.nix`:

```nix
inputs.ani2xcursor.url = "github:yuzujr/ani2xcursor";
```

Then in your NixOS configuration:

```nix
environment.systemPackages = [
  inputs.ani2xcursor.packages.${pkgs.system}.default
];
```

Or try it without installing:

```bash
nix run github:yuzujr/ani2xcursor -- /path/to/cursor/folder
```

## Build

### xmake

```bash
xmake
```

Requirements: Linux, `xmake`, a C++20 compiler, and `msgfmt` from gettext.

### make

```bash
make
```

Requirements: Linux, a C++20 compiler, `pkg-config`, `msgfmt` from gettext, and the development packages for `spdlog`, `fmt`, `stb`, `libXcursor`, and `libX11`.

## Usage

```bash
# Convert theme
ani2xcursor /path/to/cursor/folder

# Options
  --out, -o <dir>           Output directory (default: ./out)
  --format, -f <mode>       Output format: xcursor (default) or source
  --size, -s <mode>         Size selection mode:
                                all    - Export all sizes (default)
                                max    - Export only largest size
                                24,32  - Ensure sizes (reuse if present, rescale if missing)
  --manifest, -m            Generate previews + manifest.toml then exit
  --list, -l                Show available sizes in cursor files then exit
  --install, -i             Install theme to $XDG_DATA_HOME/icons
  --verbose, -v             Enable verbose logging
  --skip-broken             Continue on conversion errors
  --version, -V             Show version information
  --help, -h                Show this help message
```

## Manifest (primary configuration)

Manifest controls both cursor role mapping and per-role size customization.

If `Install.inf` is missing, ani2xcursor will use this mode automatically.
You can also force this mode with `--manifest`:

```
<input_dir>/ani2xcursor/
├── manifest.toml
└── previews/
    └── *.png
```

Edit `manifest.toml` and re-run the command. If `manifest.toml` exists, it takes priority over `Install.inf`,
and its size settings override `--size`.

### Manifest sizes

Use a comma-separated list of sizes per role in the `[sizes]` section (auto-filled with all available sizes by default):

```
[sizes]
pointer = "48"
text    = "32, 48"
```

If a role is left empty, its current sizes are preserved.

## Enable Theme

**GNOME:**
```bash
gsettings set org.gnome.desktop.interface cursor-theme 'ThemeName'
```

**XFCE:**
Use Settings Manager -> Mouse and Touchpad -> Theme, or run:
```bash
xfconf-query -c xsettings -p /Gtk/CursorThemeName -s ThemeName
xfconf-query -c xsettings -p /Gtk/CursorThemeSize -s 24
```

**KDE Plasma:**
```bash
plasma-apply-cursortheme ThemeName
```

**LXQt:**
Use `lxqt-config-appearance` and select the cursor theme there. On X11, the `~/.Xresources` settings below are also relevant.

**Niri:**
Add to `~/.config/niri/config.kdl`:
```
cursor {
    xcursor-theme "ThemeName"
}
```

**Hyprland:**
Add to `~/.config/hypr/hyprland.conf`:
```bash
exec = hyprctl setcursor ThemeName 24
```

**Sway:**
Add to `~/.config/sway/config`:
```
seat * xcursor_theme ThemeName 24
```

**X11 (~/.Xresources):**
```
Xcursor.theme: ThemeName
Xcursor.size: 24
```

**libXcursor fallback (`~/.icons/default/index.theme`):**
Many X11 and XWayland applications load cursors through libXcursor. If `XCURSOR_THEME` / `Xcursor.theme` is unset for that client, or the selected theme does not provide a requested cursor, libXcursor falls back to a theme named `default`. You can define that fallback alias by creating `~/.icons/default/index.theme` and pointing it at your installed cursor theme:

```ini
[Icon Theme]
Inherits=ThemeName
```

Use `/usr/share/icons/default/index.theme` for a system-wide default.
Only the first theme listed in `Inherits` is used by libXcursor; `Name` and `Comment` are optional and are ignored for cursor lookup.

## Cursor Mappings

| Windows    | Linux           | Aliases                      |
|------------|-----------------|------------------------------|
| pointer    | left_ptr        | default, arrow               |
| help       | help            | question_arrow, whats_this   |
| working    | left_ptr_watch  | progress                     |
| busy       | watch           | wait, clock                  |
| precision  | crosshair       | cross                        |
| text       | xterm           | ibeam, text                  |
| hand       | pencil          | handwriting                  |
| unavailable| not-allowed     | no-drop, forbidden           |
| vert       | sb_v_double_arrow | ns-resize, size_ver        |
| horz       | sb_h_double_arrow | ew-resize, size_hor        |
| dgn1       | bd_double_arrow | nwse-resize                  |
| dgn2       | fd_double_arrow | nesw-resize                  |
| move       | fleur           | move, size_all               |
| alternate  | center_ptr      | up-arrow                     |
| link       | hand2           | hand, hand1, pointing_hand   |

## Acknowledgements
This project was inspired by [ani-to-xcursor](https://github.com/nicdgonzalez/ani-to-xcursor). Its clear end-to-end workflow (INF mapping → ANI frame extraction → Xcursor theme output) helped me understand the pipeline and implement my own tool.

See also [win2xcur](https://github.com/quantum5/win2xcur), a related project in the same space. I didn’t use it as a direct reference (it’s Python-based), but it appears to include some features that aren’t currently covered by this project.

## License

MIT
