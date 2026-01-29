# ani2xcursor fish completions

complete -c ani2xcursor -f \
    -n __fish_is_first_arg \
    -a "(__fish_complete_directories)" \
    -d "Cursor theme folder"

# --out, -o <dir>  输出目录
complete -c ani2xcursor -l out -s o -r \
    -a "(__fish_complete_directories)" \
    -d "Output directory (default: ./out)"

# --format, -f <mode>
complete -c ani2xcursor -l format -s f -r -f \
    -a "xcursor source" \
    -d "Output format (default: xcursor)"

# --size, -s <mode>
complete -c ani2xcursor -l size -s s -r -f \
    -a "all max" \
    -d "Size selection mode (default: all)"

# --manifest, -m
complete -c ani2xcursor -l manifest -s m \
    -d "Generate previews + manifest.toml then exit"

# --list, -l
complete -c ani2xcursor -l list -s l \
    -d "Show available sizes then exit"

# --install, -i
complete -c ani2xcursor -l install -s i \
    -d "Install theme to \$XDG_DATA_HOME/icons"

# --verbose, -v
complete -c ani2xcursor -l verbose -s v \
    -d "Enable verbose logging"

# --skip-broken
complete -c ani2xcursor -l skip-broken \
    -d "Continue on conversion errors"

# --help, -h
complete -c ani2xcursor -l help -s h \
    -d "Show help message"
