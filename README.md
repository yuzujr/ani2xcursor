# ani2xcursor

Convert Windows Animated Cursors (.ani) with Install.inf to Linux Xcursor themes.

## Requirements

- Linux (tested on X11 and Wayland)
- xmake build system
- libXcursor development files

## Building

```bash
# Clone and build
cd ani2xcursor
xmake

# Or with verbose output
xmake -v
```

## Usage

```bash
# Basic conversion
./build/linux/x86_64/release/ani2xcursor /path/to/cursor/folder

# Specify output directory
./build/linux/x86_64/release/ani2xcursor /path/to/cursor/folder --out ./my-themes

# Convert and install
./build/linux/x86_64/release/ani2xcursor /path/to/cursor/folder --install

# Verbose output for debugging
./build/linux/x86_64/release/ani2xcursor /path/to/cursor/folder -v

# Skip broken files and continue
./build/linux/x86_64/release/ani2xcursor /path/to/cursor/folder --skip-broken
```

### Input Directory Structure

The input directory should contain:
```
MyCursor/
├── Install.inf
├── Normal.ani
├── Help.ani
├── Working.ani
├── Busy.ani
├── Precision.ani
├── Text.ani
├── Handwriting.ani
├── Unavailable.ani
├── Vertical.ani
├── Horizontal.ani
├── Diagonal1.ani
├── Diagonal2.ani
├── Move.ani
├── Alternate.ani
├── Link.ani
├── Person.ani
└── Pin.ani
```

### Output Directory Structure

```
out/
└── MyCursor/
    ├── index.theme
    └── cursors/
        ├── left_ptr          (from Normal.ani)
        ├── default -> left_ptr
        ├── arrow -> left_ptr
        ├── help              (from Help.ani)
        ├── question_arrow -> help
        ├── watch             (from Busy.ani)
        ├── wait -> watch
        ├── left_ptr_watch    (from Working.ani)
        ├── progress -> left_ptr_watch
        ├── xterm             (from Text.ani)
        ├── text -> xterm
        ├── ibeam -> xterm
        └── ... (more cursors and aliases)
```

## Enabling the Cursor Theme

### GNOME / GTK

```bash
gsettings set org.gnome.desktop.interface cursor-theme 'YourThemeName'
```

### KDE Plasma

Open System Settings → Appearance → Cursors

Or via command line:
```bash
plasma-apply-cursortheme YourThemeName
```

### Niri
Add to `~/.config/niri/config.kdl`:
```
cursor {
    xcursor-theme "YourThemeName"
}
```

### Hyprland

Add to `~/.config/hypr/hyprland.conf`:
```
exec-once = hyprctl setcursor YourThemeName 24
```

Or run immediately:
```bash
hyprctl setcursor YourThemeName 24
```

### Sway

Add to `~/.config/sway/config`:
```
seat * xcursor_theme YourThemeName 24
```

### X11 (Generic)

Add to `~/.Xresources`:
```
Xcursor.theme: YourThemeName
Xcursor.size: 24
```

Then run:
```bash
xrdb -merge ~/.Xresources
```

### Environment Variables (Universal)

```bash
export XCURSOR_THEME=YourThemeName
export XCURSOR_SIZE=24
```

Add these to your shell profile (`~/.bashrc`, `~/.zshrc`, etc.) for persistence.

## Technical Details

### ANI File Format

The .ani format is based on RIFF (Resource Interchange File Format):
- Container: `RIFF ACON`
- Required chunks:
  - `anih`: Animation header (frame count, display rate, flags)
  - `LIST fram`: Contains `icon` sub-chunks (one per frame)
- Optional chunks:
  - `rate`: Per-frame delays (in jiffies = 1/60 second)
  - `seq `: Playback sequence (frame indices)
  - `LIST INFO`: Metadata (ignored)

### ICO/CUR Format

Each animation frame is stored as an ICO/CUR file, which can contain:
- PNG-encoded images (modern cursors)
- BMP/DIB-encoded images (legacy cursors)

The decoder supports:
- 1, 4, 8, 24, and 32-bit BMP images
- PNG images with alpha channel
- Hotspot extraction from CUR header

### Xcursor Format

Output uses libXcursor to create X11 cursor files with:
- Multiple frames for animations
- Per-frame delays (in milliseconds)
- Hotspot coordinates
- ARGB pixel format

## Cursor Role Mappings

| Windows Role | Primary X11 Name | Common Aliases |
|--------------|------------------|----------------|
| pointer | left_ptr | default, arrow |
| help | help | question_arrow, whats_this |
| working | left_ptr_watch | progress |
| busy | watch | wait, clock |
| precision | crosshair | cross |
| text | xterm | ibeam, text |
| hand | pencil | handwriting |
| unavailable | not-allowed | no-drop, forbidden |
| vert | sb_v_double_arrow | ns-resize, size_ver |
| horz | sb_h_double_arrow | ew-resize, size_hor |
| dgn1 | bd_double_arrow | nwse-resize |
| dgn2 | fd_double_arrow | nesw-resize |
| move | fleur | move, size_all |
| alternate | center_ptr | up-arrow |
| link | pointer | hand, hand1, hand2 |

## License

MIT License
