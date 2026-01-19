# ani2xcursor

Convert Windows animated cursor themes (.ani/.cur with Install.inf) to Linux Xcursor format.

## Features

- Full .ani/.cur format support
- Multi-size export with auto-rescale for missing target sizes
- Manual mapping mode: generate cursor previews + mapping template, then customize roles (useful for overrides with or without Install.inf)
  ### Screenshots
  cursor preview:

  <img width="798" height="277" alt="图片" src="https://github.com/user-attachments/assets/ea4983dd-e476-4212-9c23-1704d8d39364" />

  corrupted cursor preview:

  <img width="798" height="277" alt="output" src="https://github.com/user-attachments/assets/90b56fa2-6b11-4fde-8f30-d7ea2448c5dd" />

## Build

```bash
xmake
```

Requirements: Linux, xmake

## Usage

```bash
# Convert theme
ani2xcursor /path/to/cursor/folder

# Options
--out, -o <dir>       Output directory (default: ./out)
--install, -i         Install theme to $XDG_DATA_HOME/icons
--verbose, -v         Enable verbose logging
--skip-broken         Continue on conversion errors
--manual-mapping      Generate previews + mapping.toml then exit
--list-sizes          Show available sizes in cursor files then exit
--sizes <mode>        Size selection mode:
                          all    - Export all sizes (default)
                          max    - Export only largest size
                          24,32  - Ensure sizes (reuse if present, rescale if missing)
--help, -h            Show this help message
```

## Manual Mapping (fallback or forced)

If `Install.inf` is missing, ani2xcursor generates previews and a mapping template.
You can also force this mode with `--manual-mapping` or by removing `Install.inf`:

```
<input_dir>/ani2xcursor/
├── mapping.toml
└── previews/
    └── *.png
```

Edit `mapping.toml` and re-run the same command. If `mapping.toml` exists, it takes priority over `Install.inf`.

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
    xcursor-theme "ThemeName"
}
```

**Hyprland:**
Add to `~/.config/hypr/hyprland.conf`:
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

## Acknowledgements
This project was inspired by [ani-to-xcursor](https://github.com/nicdgonzalez/ani-to-xcursor). Its clear end-to-end workflow (INF mapping → ANI frame extraction → Xcursor theme output) helped me understand the pipeline and implement my own tool.

See also [win2xcur](https://github.com/quantum5/win2xcur), a related project in the same space. I didn’t use it as a direct reference (it’s Python-based), but it appears to include some features that aren’t currently covered by this project.

## License

MIT
