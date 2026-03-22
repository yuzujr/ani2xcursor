#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "usage: $0 <version> <output>" >&2
    exit 1
fi

VERSION="$1"
OUTPUT="$2"
ROOT="ani2xcursor-${VERSION}"
TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT

mkdir -p "$TMPDIR/$ROOT"
git archive --format=tar HEAD | tar -xf - -C "$TMPDIR/$ROOT"

tar \
    --sort=name \
    --mtime='UTC 1970-01-01' \
    --owner=0 \
    --group=0 \
    --numeric-owner \
    -czf "$OUTPUT" \
    -C "$TMPDIR" \
    "$ROOT"
