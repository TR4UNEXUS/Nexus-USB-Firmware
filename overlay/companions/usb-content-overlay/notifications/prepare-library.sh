#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
library=${1:-"$root/build-deps/libultrahand"}
pin=ea02e8d90c961a6bc17469aa772475bf5d4fc5f7
if ! git -C "$library" rev-parse --git-dir >/dev/null 2>&1 || [ ! -f "$library/ultrahand.mk" ]; then
    mkdir -p "$library"
    git -C "$library" init -q
    git -C "$library" fetch -q --depth 1 https://github.com/ppkantorski/libultrahand "$pin"
    git -C "$library" checkout -q --detach FETCH_HEAD
fi
test "$(git -C "$library" rev-parse HEAD)" = "$pin"
git -C "$library" diff --quiet HEAD --
