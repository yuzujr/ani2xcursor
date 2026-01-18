# ani2xcursor

Convert Windows animated cursor themes (.ani/.cur with Install.inf) to Linux Xcursor format.

## Features

- Full .ani/.cur format support
- Multi-size export

## Build

```bash
xmake
```

Requirements: Linux, xmake, libXcursor-dev

## Usage

```bash
# Convert theme
ani2xcursor /path/to/cursor/folder

# Options
-o, --out <dir>      Output directory (default: ./out)
-i, --install        Install to ~/.local/share/icons
-v, --verbose        Verbose output
--sizes <mode>       Size export: all (default) | max | 24,32,48
--skip-broken        Continue on errors
-h, --help           Show help
```

## Input Structure

```
ThemeName/
├── Install.inf
└── *.ani files
```

## Output Structure

```
out/ThemeName/
├── index.theme
└── cursors/
    ├── left_ptr (+ symlinks)
    ├── watch
    ├── xterm
    └── ...
```

## Enable Theme

**GNOME/GTK:**
```bash
gsettings set org.gnome.desktop.interface cursor-theme 'ThemeName'
```

**KDE Plasma:**
```bash
plasma-apply-cursortheme ThemeName
```

**Niri:**
Add to `~/.config/niri/config.kdl`:
```
cursor {
    xcursor-theme "YourThemeName"
}
```

**Hyprland:**
```bash
hyprctl setcursor ThemeName 24
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

## License

MIT
