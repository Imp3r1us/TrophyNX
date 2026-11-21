#!/usr/bin/env python3
"""Finds the THEATRHYTHM FINAL BAR LINE save password in YOUR copy of the game, and writes it to a file.

The password is one of the string literals of the game's global-metadata.dat (romfs: Data/Managed/Metadata/).
Its index changes between game versions (#13610 in 1.00, #12425 in the 2023+ Switch build), so instead of a fixed
index every literal is tried against one of your saves: the right one decrypts it (valid PKCS#7 padding, then raw
DEFLATE, then a MessagePack map). Nothing secret is printed: only the index and the length.

Usage: python3 tools/theatrhythm/find_password.py global-metadata.dat backup.inf OUT_FILE
Then copy OUT_FILE to sdmc:/switch/TrophyNX/keys/theatrhythm.txt (and/or paste it in the Save Lab).
Keep it out of git (local/ is ignored). Standard library only.
"""
import hashlib
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from decrypt_saves import _expand, _decrypt_block, aes128_cbc_decrypt  # noqa: E402


def literals(meta):
    if struct.unpack_from('<I', meta, 0)[0] != 0xFAB11BAF:
        raise SystemExit('not a global-metadata.dat (bad magic)')
    lo, lsz, do = struct.unpack_from('<3i', meta, 8)
    for i in range(lsz // 8):
        n, di = struct.unpack_from('<Ii', meta, lo + 8 * i)
        yield i, meta[do + di:do + di + n]


def main():
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    meta = open(sys.argv[1], 'rb').read()
    data = open(sys.argv[2], 'rb').read()
    if len(data) >= 52 and (len(data) - 4) % 16 == 0 and struct.unpack_from('<I', data)[0] == len(data) - 4:
        data = data[4:]
    if len(data) < 48 or len(data) % 16:
        raise SystemExit('the save is not a THEATRHYTHM .inf file (size)')
    q = [data[8 * i:8 * i + 8] for i in range(4)]
    salt, iv, ct = q[0] + q[3], q[2] + q[1], data[32:]
    seen = set()
    for i, s in literals(meta):
        if not 4 <= len(s) <= 128 or s in seen:
            continue
        seen.add(s)
        key = hashlib.pbkdf2_hmac('sha1', s, salt, 1000, 16)
        last = bytes(a ^ b for a, b in zip(_decrypt_block(_expand(key), ct[-16:]), ct[-32:-16]))
        pad = last[-1]
        if not 1 <= pad <= 16 or last[-pad:] != bytes([pad]) * pad:
            continue
        try:
            raw = zlib.decompress(aes128_cbc_decrypt(key, iv, ct)[:-pad], -15)
        except zlib.error:
            continue
        if raw[:1] and (raw[0] & 0xF0 == 0x80 or raw[0] in (0xDE, 0xDF)):
            with open(sys.argv[3], 'wb') as f:
                f.write(s)
            print('found: literal #%d, %d bytes -> %s' % (i, len(s), sys.argv[3]))
            return
    raise SystemExit('no literal decrypts this save (other game, or damaged save)')


if __name__ == '__main__':
    main()
