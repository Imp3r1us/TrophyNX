#!/bin/bash
set -e

OUT_NSO=${OUT}/${BINARY_NAME}
OUT_NPDM=${OUT}/main.npdm

# Clear older build.
rm -rf ${OUT}

# Create out directory.
mkdir ${OUT}

# Copy build into out
mv ${NAME}.nso ${OUT_NSO}
# TrophyNX: never ship exlaunch's npdm (it allows every syscall). tools/unityhook --make-hook writes
# the game's own npdm + the two syscalls hooking needs.
rm -f ${NAME}.npdm

# Copy ELF to user path if defined.
if [ ! -z $ELF_EXTRACT ]; then
    cp "$NAME.elf" "$ELF_EXTRACT"
fi
