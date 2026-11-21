#!/usr/bin/env python3
"""THEATRHYTHM FINAL BAR LINE saves (backup.inf, musicBackup.inf, proficaBackup.inf) -> JSON.

Format (same on PS4 and Switch; see https://github.com/rcostanza/theatrhythm-fbl-save-converter):
  [u32 LE length, PS4 only] + 32-byte header (quarters q0 q1 q2 q3) + AES-128-CBC ciphertext.
  salt = q0||q3, iv = q2||q1, key = PBKDF2-HMAC-SHA1(password, salt, 1000, 16 bytes).
  plaintext = raw DEFLATE (no zlib header) of a MessagePack map, PKCS#7 padded.

The password is a string literal of the game's global-metadata.dat. It is never stored in this repo:
pass it with --key-file (e.g. local/theatrhythm.key, git-ignored).

Usage: python3 decrypt_saves.py --key-file local/theatrhythm.key OUT_DIR SAVE_DIR_OR_FILE...
Writes OUT_DIR/<capture>/<file>.json (and prints a short summary). Standard library only (pure-Python AES).
"""
import argparse
import hashlib
import json
import os
import struct
import sys
import zlib

# ------------------------------------------------------------------ AES-128 decryption (pure Python)
def _xt(a):
    a <<= 1
    return (a ^ 0x11B) & 0xFF if a & 0x100 else a

def _mul(a, b):
    r = 0
    while b:
        if b & 1:
            r ^= a
        a = _xt(a)
        b >>= 1
    return r

def _sbox():
    inv = [0] * 256
    for i in range(1, 256):
        for j in range(1, 256):
            if _mul(i, j) == 1:
                inv[i] = j
                break
    s = []
    for i in range(256):
        x = inv[i]
        y = x
        for _ in range(4):
            x = ((x << 1) | (x >> 7)) & 0xFF
            y ^= x
        s.append(y ^ 0x63)
    return s

SBOX = _sbox()
INV_SBOX = [0] * 256
for _i, _v in enumerate(SBOX):
    INV_SBOX[_v] = _i
M9, M11, M13, M14 = ([_mul(i, k) for i in range(256)] for k in (9, 11, 13, 14))

def _expand(key):
    w = [list(key[4 * i:4 * i + 4]) for i in range(4)]
    rcon = 1
    for i in range(4, 44):
        t = list(w[i - 1])
        if i % 4 == 0:
            t = t[1:] + t[:1]
            t = [SBOX[b] for b in t]
            t[0] ^= rcon
            rcon = _xt(rcon)
        w.append([a ^ b for a, b in zip(w[i - 4], t)])
    return [sum(w[4 * r:4 * r + 4], []) for r in range(11)]

def _decrypt_block(rk, blk):
    s = [a ^ b for a, b in zip(blk, rk[10])]
    for rnd in range(9, -1, -1):
        s = [s[r + 4 * ((c - r) % 4)] for c in range(4) for r in range(4)]   # InvShiftRows (column-major)
        s = [INV_SBOX[b] for b in s]
        s = [a ^ b for a, b in zip(s, rk[rnd])]
        if rnd:
            o = []
            for c in range(4):
                a0, a1, a2, a3 = s[4 * c:4 * c + 4]
                o += [M14[a0] ^ M11[a1] ^ M13[a2] ^ M9[a3], M9[a0] ^ M14[a1] ^ M11[a2] ^ M13[a3],
                      M13[a0] ^ M9[a1] ^ M14[a2] ^ M11[a3], M11[a0] ^ M13[a1] ^ M9[a2] ^ M14[a3]]
            s = o
    return bytes(s)

def aes128_cbc_decrypt(key, iv, data):
    rk = _expand(key)
    out = bytearray()
    prev = iv
    for i in range(0, len(data), 16):
        blk = data[i:i + 16]
        out += bytes(a ^ b for a, b in zip(_decrypt_block(rk, blk), prev))
        prev = blk
    return bytes(out)

# ------------------------------------------------------------------ MessagePack -> Python
class MsgPack:
    def __init__(self, b):
        self.b, self.p, self.ext = b, 0, {}

    def take(self, n):
        if self.p + n > len(self.b):
            raise ValueError('truncated at %d' % self.p)
        v = self.b[self.p:self.p + n]
        self.p += n
        return v

    def u(self, fmt):
        n = struct.calcsize(fmt)
        return struct.unpack('>' + fmt, self.take(n))[0]

    def value(self):
        t = self.take(1)[0]
        if t <= 0x7F: return t
        if t >= 0xE0: return t - 256
        if 0x80 <= t <= 0x8F: return self.map(t & 15)
        if 0x90 <= t <= 0x9F: return self.arr(t & 15)
        if 0xA0 <= t <= 0xBF: return self.take(t & 31).decode('utf-8', 'replace')
        if t == 0xC0: return None
        if t == 0xC2: return False
        if t == 0xC3: return True
        if t in (0xC4, 0xC5, 0xC6): return {'_bin': self.take(self.u('BHI'[t - 0xC4])).hex()}
        if t in (0xC7, 0xC8, 0xC9):
            n = self.u('BHI'[t - 0xC7]); return self.extv(self.u('b'), n)
        if t == 0xCA: return self.u('f')
        if t == 0xCB: return self.u('d')
        if 0xCC <= t <= 0xCF: return self.u('BHIQ'[t - 0xCC])
        if 0xD0 <= t <= 0xD3: return self.u('bhiq'[t - 0xD0])
        if 0xD4 <= t <= 0xD8: return self.extv(self.u('b'), 1 << (t - 0xD4))
        if t in (0xD9, 0xDA, 0xDB): return self.take(self.u('BHI'[t - 0xD9])).decode('utf-8', 'replace')
        if t in (0xDC, 0xDD): return self.arr(self.u('HI'[t - 0xDC]))
        if t in (0xDE, 0xDF): return self.map(self.u('HI'[t - 0xDE]))
        raise ValueError('bad type byte 0x%02X at %d' % (t, self.p - 1))

    def extv(self, typ, n):
        self.ext[typ] = self.ext.get(typ, 0) + 1
        return {'_ext': typ, 'hex': self.take(n).hex()}

    def arr(self, n):
        return [self.value() for _ in range(n)]

    def map(self, n):
        d = {}
        for _ in range(n):
            k = self.value()
            if not isinstance(k, str):
                self.ext['nonstr_key'] = self.ext.get('nonstr_key', 0) + 1
                k = json.dumps(k) if not isinstance(k, (int, float)) else str(k)
            d[k] = self.value()
        return d

# ------------------------------------------------------------------ container
def decrypt_file(data, password):
    if len(data) >= 36 and (len(data) - 4) % 16 == 0 and struct.unpack_from('<I', data)[0] == len(data) - 4:
        data, kind = data[4:], 'ps4'
    elif len(data) >= 48 and len(data) % 16 == 0:
        kind = 'switch'
    else:
        raise ValueError('size %d is not header + AES blocks' % len(data))
    q = [data[8 * i:8 * i + 8] for i in range(4)]
    salt, iv = q[0] + q[3], q[2] + q[1]
    key = hashlib.pbkdf2_hmac('sha1', password, salt, 1000, 16)
    pt = aes128_cbc_decrypt(key, iv, data[32:])
    pad = pt[-1]
    if not 1 <= pad <= 16 or pt[-pad:] != bytes([pad]) * pad:
        raise ValueError('bad padding (wrong password?)')
    raw = zlib.decompress(pt[:-pad], -15)
    return kind, raw

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--key-file', required=True)
    ap.add_argument('out')
    ap.add_argument('inputs', nargs='+')
    a = ap.parse_args()
    password = open(a.key_file, 'rb').read().strip()
    files = []
    for i in a.inputs:
        if os.path.isdir(i):
            for root, _, names in os.walk(i):
                files += [os.path.join(root, n) for n in sorted(names) if n.endswith('.inf')]
        else:
            files.append(i)
    for f in sorted(files):
        kind, raw = decrypt_file(open(f, 'rb').read(), password)
        mp = MsgPack(raw)
        tree = mp.value()
        rest = len(raw) - mp.p
        cap = os.path.basename(os.path.dirname(f))
        os.makedirs(os.path.join(a.out, cap), exist_ok=True)
        dst = os.path.join(a.out, cap, os.path.basename(f) + '.json')
        with open(dst, 'w', encoding='utf-8') as o:
            json.dump(tree, o, indent=1, ensure_ascii=False)
        top = list(tree.keys())[:8] if isinstance(tree, dict) else type(tree).__name__
        print('%s/%s: %s, msgpack %d bytes%s, ext/key stats %s, top %s' % (
            cap, os.path.basename(f), kind, len(raw), ', %d trailing bytes' % rest if rest else '', mp.ext, top))

if __name__ == '__main__':
    main()
