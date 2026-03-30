#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -lt 2 ]; then
    echo "usage: $0 <max-glibc-version> <file> [file...]" >&2
    exit 1
fi

MAX_GLIBC="$1"
shift

version_gt() {
    local left="$1"
    local right="$2"
    [ "$(printf '%s\n%s\n' "$left" "$right" | sort -V | tail -n 1)" = "$left" ] && [ "$left" != "$right" ]
}

checked=0
max_seen=""

for path in "$@"; do
    [ -f "$path" ] || continue

    versions="$(
        {
            readelf --version-info "$path" 2>/dev/null
            objdump -T "$path" 2>/dev/null
        } \
            | grep -o 'GLIBC_[0-9][0-9.]*' \
            | sort -Vu \
            || true
    )"

    [ -n "$versions" ] || continue

    checked=1
    required="$(printf '%s\n' "$versions" | tail -n 1 | sed 's/^GLIBC_//')"
    echo "$path requires up to GLIBC_$required"

    if [ -z "$max_seen" ] || version_gt "$required" "$max_seen"; then
        max_seen="$required"
    fi

    if version_gt "$required" "$MAX_GLIBC"; then
        echo "error: $path exceeds GLIBC_$MAX_GLIBC" >&2
        exit 1
    fi
done

if [ "$checked" -eq 0 ]; then
    echo "error: no ELF files with GLIBC version requirements were found" >&2
    exit 1
fi

echo "glibc baseline check passed: max required is GLIBC_$max_seen"
