#!/usr/bin/env python3
"""Extract TotK completion hash lists from Marc Robledo's savegame editor and
check them against a progress.sav. Usage: totk_extract.py completism.js [progress.sav]"""
import json, re, struct, sys

def murmur3_32(data: bytes, seed=0) -> int:
    c1, c2 = 0xcc9e2d51, 0x1b873593
    h = seed
    n = len(data) // 4
    for i in range(n):
        k = struct.unpack_from('<I', data, i * 4)[0]
        k = (k * c1) & 0xffffffff; k = ((k << 15) | (k >> 17)) & 0xffffffff; k = (k * c2) & 0xffffffff
        h ^= k; h = ((h << 13) | (h >> 19)) & 0xffffffff; h = (h * 5 + 0xe6546b64) & 0xffffffff
    tail = data[n * 4:]; k = 0
    if len(tail) >= 3: k ^= tail[2] << 16
    if len(tail) >= 2: k ^= tail[1] << 8
    if len(tail) >= 1:
        k ^= tail[0]; k = (k * c1) & 0xffffffff; k = ((k << 15) | (k >> 17)) & 0xffffffff; k = (k * c2) & 0xffffffff; h ^= k
    h ^= len(data)
    h ^= h >> 16; h = (h * 0x85ebca6b) & 0xffffffff; h ^= h >> 13; h = (h * 0xc2b2ae35) & 0xffffffff; h ^= h >> 16
    return h

def extract(js):
    src = open(js).read()
    out = {}
    for m in re.finditer(r'\n\t([A-Z_]+):\[(.*?)\n\t\]', src, re.S):
        body = re.sub(r'//[^\n]*', '', m.group(2))
        if re.search(r"'\d{6,}'", body):   # GUID lists (64-bit ids), not flag hashes
            continue
        vals = [int(v, 16) for v in re.findall(r'0x[0-9a-fA-F]{8}', body)]
        vals += [murmur3_32(n.encode()) for n in re.findall(r"hash\('([^']+)'\)", body)]
        if vals:
            out[m.group(1)] = vals
    return out

class Save:
    def __init__(self, path):
        d = open(path, 'rb').read()
        assert struct.unpack_from('<I', d, 0)[0] == 0x01020304, 'not a TotK progress.sav'
        self.idx = {}
        o = 0x28
        while o + 8 <= len(d):
            h, v = struct.unpack_from('<II', d, o)
            if h == 0xa3db7114: break
            self.idx.setdefault(h, v)
            o += 8
    def get(self, h): return self.idx.get(h)

if __name__ == '__main__':
    lists = extract(sys.argv[1])
    for k, v in lists.items(): print(k, len(v))
    if len(sys.argv) > 2:
        s = Save(sys.argv[2])
        clear, open_ = murmur3_32(b'Clear'), murmur3_32(b'Open')
        print('hash table entries', len(s.idx), 'Clear=%08x Open=%08x' % (clear, open_))
        print('shrines clear', sum(1 for h in lists['SHRINES_STATUS'] if s.get(h) == clear))
        print('shrines found', sum(1 for h in lists['SHRINES_FOUND'] if s.get(h)))
        print('lightroots open', sum(1 for h in lists['LIGHTROOTS_STATUS'] if s.get(h) == open_))
        print('towers activated', sum(1 for h in lists['TOWERS_ACTIVATED'] if s.get(h)))
        print('koroks hidden', sum(1 for h in lists['KOROKS_HIDDEN'] if s.get(h)), 'carry', sum(1 for h in lists['KOROKS_CARRY'] if s.get(h) == clear))
        for k in ['BOSSES_HINOXES_DEFEATED','BOSSES_TALUSES_DEFEATED','BOSSES_MOLDUGAS_DEFEATED','BOSSES_FLUX_CONSTRUCT_DEFEATED','BOSSES_FROXS_DEFEATED','BOSSES_GLEEOKS_DEFEATED','BUBBULS_DEFEATED']:
            if k in lists: print(k, sum(1 for h in lists[k] if s.get(h)), '/', len(lists[k]), 'missing', sum(1 for h in lists[k] if s.get(h) is None))
        for name, h in [('MaxLife', 0xfbe01da1), ('Rupee', 0xa77921d7), ('MaxStamina', 0xf9212c74), ('Playtime', 0xe573f564), ('MaxEnergy', 0xafd01d68)]:
            v = s.get(h); print(name, v, struct.unpack('<f', struct.pack('<I', v))[0] if v is not None else None)
        print('PlayerStatus.MaxLife murmur', hex(murmur3_32(b'PlayerStatus.MaxLife')), 'DungeonState.Dungeon000', hex(murmur3_32(b'DungeonState.Dungeon000')))
        if len(sys.argv) > 3:
            json.dump({k: ['0x%08x' % x for x in v] for k, v in lists.items()}, open(sys.argv[3], 'w'))
