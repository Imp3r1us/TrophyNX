# TrophyNX hook module

This folder is [exlaunch](https://github.com/shadowninja108/exlaunch) by shadowninja108 (GPL-2.0), vendored at
commit `f9f4b0dd07b68f97958cb9c79228bbca22ca80d5` (2026-08-26). The built module is therefore distributed under
GPLv2, like `sysmodule/` with `dmntcht`.

TrophyNX changes, everything else is upstream:

- `source/program/main.cpp`: the hook itself (config table, signature check, trampolines, ring buffer). See
  [tools/unityhook/DESIGN.md](../tools/unityhook/DESIGN.md).
- `source/program/setting.hpp`: module name `TrophyNXHook`, no debug logging.
- `config.mk`: no program id (one module for every game).
- `misc/scripts/post-build.sh`: exlaunch's npdm is deleted, never shipped (it allows every syscall).
  `tools/unityhook/unityhook.py hook-make` writes the game's own npdm + `svcMapProcessMemory` /
  `svcUnmapProcessMemory` instead.

Build: `make` here, in the `devkitpro/devkita64` Docker image (or CI's `TrophyNX-hook-module` artifact).
Output: `deploy/subsdk9` (generic, empty table), turned into one game's module by `hook-make`.
