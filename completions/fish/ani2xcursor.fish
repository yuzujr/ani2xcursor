# ani2xcursor fish completions

complete -c ani2xcursor -f \
    -n __fish_is_first_arg \
    -a "(__fish_complete_directories)" \
    -d "Cursor theme folder"

# --out, -o <dir>  输出目录
complete -c ani2xcursor -l out -s o -r \
    -a "(__fish_complete_directories)" \
    -d "Output directory (default: ./out)"

# --install, -i
complete -c ani2xcursor -l install -s i \
    -d "Install theme to \$XDG_DATA_HOME/icons"

# --verbose, -v
complete -c ani2xcursor -l verbose -s v \
    -d "Enable verbose logging"

# --skip-broken
complete -c ani2xcursor -l skip-broken \
    -d "Continue on conversion errors"

# --manual-mapping
complete -c ani2xcursor -l manual-mapping \
    -d "Generate previews + mapping.toml then exit"

# --list-sizes
complete -c ani2xcursor -l list-sizes \
    -d "Show available sizes then exit"

# --sizes <mode>
complete -c ani2xcursor -l sizes -r -f \
    -a "all max" \
    -d "Size selection mode (default: all)"

# --help, -h
complete -c ani2xcursor -l help -s h \
    -d "Show help message"
