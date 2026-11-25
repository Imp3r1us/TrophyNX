#!/bin/sh
# Prepares sysmodule/data/ (embedded with bin2o). Run before `make`: the Makefile lists data/ files
# when it starts, so they must already exist.
#   botw.json  live-mode flags (BotW definition)
#   icon.jpg   fallback popup icon
#   games.bin  every romfs/games definition, for the scan on close: "<file name>\n<JSON>\0" per file
#   universal.json  achievements of every game (play time, launches...), for the live play-log popups
set -e
cd "$(dirname "$0")"
mkdir -p data
cp ../romfs/games/01007EF00011E000.json data/botw.json
cp ../icon.jpg data/icon.jpg
cp ../romfs/universal.json data/universal.json
: > data/games.bin
for f in ../romfs/games/*.json; do
    printf '%s\n' "$(basename "$f")" >> data/games.bin
    cat "$f" >> data/games.bin
    printf '\000' >> data/games.bin
done
echo "data/games.bin: $(ls ../romfs/games/*.json | wc -l) definitions, $(wc -c < data/games.bin) bytes"
