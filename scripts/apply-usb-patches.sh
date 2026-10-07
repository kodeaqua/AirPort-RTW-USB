#!/bin/sh
# Apply patches/*.patch to the AirPort-RTW clone, skipping any that are already applied.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT/AirPort-RTW"
for p in "$ROOT"/patches/*.patch; do
    if patch -p1 -R --dry-run -s < "$p" >/dev/null 2>&1; then
        echo "already applied: $(basename "$p")"
    elif patch -p1 --dry-run -s < "$p" >/dev/null 2>&1; then
        patch -p1 -s < "$p" && echo "applied: $(basename "$p")"
    else
        echo "ERROR: $(basename "$p") neither applies nor is applied (clone modified or out of date)" >&2
        exit 1
    fi
done
