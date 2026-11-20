#!/bin/sh
# Patches DELTARUNE chapters 1-4 of your own dump for TrophyNX and writes them in the LayeredFS layout.
#   sh patch.sh <dump>/romfs <path to UndertaleModCli> [out dir, default .]
# -> <out>/atmosphere/contents/0100A0D022A68000/romfs/chapterN_switch/game.win (N = 1..4): copy that folder to the SD card.
set -e
romfs=$1; cli=$2; out=${3:-.}
[ -n "$romfs" ] && [ -n "$cli" ] || { echo "usage: sh patch.sh <romfs dir> <UndertaleModCli> [out dir]"; exit 2; }
here=$(cd "$(dirname "$0")" && pwd)
dest="$out/atmosphere/contents/0100A0D022A68000/romfs"
for n in 1 2 3 4; do
    src="$romfs/chapter${n}_switch/game.win"
    [ -f "$src" ] || { echo "missing $src (pass the romfs folder of the dump)"; exit 1; }
    mkdir -p "$dest/chapter${n}_switch"
    dst="$dest/chapter${n}_switch/game.win"
    echo "Chapter $n: $src"
    # stdin closed: the CLI waits for input otherwise
    log=$("$cli" load "$src" -s "$here/TrophyNX_trophies.csx" -o "$dst" -f </dev/null 2>&1) || true
    printf '%s\n' "$log" | grep -E "TrophyNX|rror|xception" | sed 's/^/  /' || true
    if ! printf '%s\n' "$log" | grep -qE "TrophyNX: (patched|already patched)"; then
        rm -f "$dst"; echo "chapter $n: the patch did not apply (see the messages above)"; exit 1
    fi
done
echo "Done. Copy $out/atmosphere to the root of the SD card."
