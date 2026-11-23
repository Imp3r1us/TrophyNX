#!/usr/bin/env python3
"""Builds a synthetic Switch save image (the container FS stores at NAND User:/save/<id>) and a small FAT32
volume holding it, for host/test/test_saveimage.cpp.

Written from hactool's save.c, independently of source/saveimage.cpp, with every layer made non-trivial:
remap entries out of order, duplex blocks taken from copy A or B, L1 bitmap itself duplexed, journal blocks
shuffled, files fragmented in the allocation table, an unwritten block (zero hash) holding junk, header A
broken (B in use) on request, long file names and a fragmented cluster chain in FAT32.

usage: make_save_image.py <out dir> [--header-b] [--v4] [--seed N]
writes <out>/nand.img, <out>/expected/<path> (the save's files), <out>/info.txt (save id)
"""
import hashlib, os, random, struct, sys

args = sys.argv[1:]
if not args:
    sys.exit(__doc__)
out = args[0]
use_b = "--header-b" in args
v4 = "--v4" in args
seed = int(args[args.index("--seed") + 1]) if "--seed" in args else 1
rnd = random.Random(seed)

BS = 0x4000            # save FS block size = journal block size = IVFC data block size = duplex data block size
SAVE_ID = 0x8000000000000123

def junk(n):
    return bytes(rnd.getrandbits(8) for _ in range(n))

# ---------------------------------------------------------------- save file system (core data)
files = {
    "progress.json": b'{"chapter": 3, "gold": 1234, "flags": [1, 2, 3]}',
    "slot0/game.sav": junk(3 * BS + 123),     # spans 4 blocks, fragmented below
    "slot0/sub/deep.bin": b"deep" * 10,
    "empty.dat": b"",
    "sparse.bin": bytes(BS),                  # never written: zero IVFC hash, junk bytes underneath
}
N = 24                                        # blocks in the save FS
core = bytearray(junk(N * BS))                # unused blocks hold junk
fat = [[0, 0] for _ in range(N + 1)]          # entry i+1 = block i: [prev, next]
free = list(range(N))
rnd.shuffle(free)
UNWRITTEN = None

def alloc_chain(nblocks):
    """Allocates nblocks in several segments, some of them ranges (multi-block). Returns the block list."""
    blocks = []
    segs = []
    while len(blocks) < nblocks:
        want = min(nblocks - len(blocks), rnd.choice([1, 2, 3]))
        # a contiguous run of free blocks starting at a random free one
        start = None
        for b in sorted(free):
            if all((b + k) in free for k in range(want)):
                start = b
                if rnd.random() < 0.5:
                    break
        if start is None:
            want, start = 1, free[0]
        for k in range(want):
            free.remove(start + k)
        segs.append((start, want))
        blocks += range(start, start + want)
    for i, (start, length) in enumerate(segs):
        ei = start + 1
        prev = 0x80000000 if i == 0 else segs[i - 1][0] + 1
        nxt = 0 if i == len(segs) - 1 else segs[i + 1][0] + 1
        if length > 1:
            fat[ei] = [prev, nxt | 0x80000000]
            fat[ei + 1] = [ei | 0x80000000, ei + length - 1]   # range entry: last entry index
            fat[ei + length - 1] = [ei | 0x80000000, ei + length - 1]
        else:
            fat[ei] = [prev, nxt]
    return blocks

def write_chain(blocks, data):
    for i, b in enumerate(blocks):
        part = data[i * BS:(i + 1) * BS]
        core[b * BS:b * BS + len(part)] = part

def entry(parent, name, next_sibling, value, nxt):
    e = struct.pack("<I", parent) + name.encode().ljust(0x40, b"\0") + struct.pack("<I", next_sibling) + value
    e = e.ljust(0x5C, b"\0") + struct.pack("<I", nxt)
    assert len(e) == 0x60
    return e

# directories: index 2 = root, then slot0, slot0/sub; files get their own indexes
dirs = {"": 2, "slot0": 3, "slot0/sub": 4}
dir_rows = [(2, 0, ""), (3, 2, "slot0"), (4, 3, "sub")]
file_rows = []
file_blocks = {}
for i, (path, data) in enumerate(sorted(files.items())):
    parent = dirs[os.path.dirname(path)]
    nb = max(1, (len(data) + BS - 1) // BS)
    blocks = alloc_chain(nb)
    if path == "sparse.bin":
        UNWRITTEN = blocks[0]                 # keeps the junk already in core
    else:
        write_chain(blocks, data)
    file_blocks[path] = blocks
    file_rows.append((5 + i, parent, os.path.basename(path), blocks[0], len(data)))

def table(rows, capacity, value_of):
    tbl = bytearray(capacity * 0x60)
    # entry 0: free list head, bytes 4..8 = capacity; entry 1: used list head
    tbl[4:8] = struct.pack("<I", capacity)
    order = [r[0] for r in rows]
    rnd.shuffle(order)                       # used list order doesn't follow the indexes
    tbl[0x60:0xC0] = entry(0, "", 0, b"", order[0] if order else 0)
    for k, idx in enumerate(order):
        r = next(r for r in rows if r[0] == idx)
        tbl[idx * 0x60:(idx + 1) * 0x60] = entry(r[1], r[2], 0, value_of(r), order[k + 1] if k + 1 < len(order) else 0)
    return bytes(tbl)

dir_tbl = table(dir_rows, 12, lambda r: b"")
file_tbl = table(file_rows, 12, lambda r: struct.pack("<IQ", r[3], r[4]))
dir_blocks = alloc_chain(1)
write_chain(dir_blocks, dir_tbl)
file_blocks_tbl = alloc_chain(1)
write_chain(file_blocks_tbl, file_tbl)

fat_bytes = b"".join(struct.pack("<II", *e) for e in fat)
fat_bytes = fat_bytes.ljust(((len(fat_bytes) + 0x3FF) // 0x400) * 0x400, b"\0")

# IVFC data level: the core data, at an offset inside the journal
IVFC_DATA_OFF = BS
ivfc_hashes = bytearray()
for b in range(N):
    h = bytearray(hashlib.sha256(core[b * BS:(b + 1) * BS]).digest())
    h[0x1F] |= 0x80
    if b == UNWRITTEN:
        h = bytearray(0x20)
    ivfc_hashes += h
ivfc_hashes = bytes(ivfc_hashes).ljust(0x400, b"\0")

# ---------------------------------------------------------------- journal
journal_virtual = bytes(junk(IVFC_DATA_OFF)) + bytes(core)
JB = len(journal_virtual) // BS
phys_order = list(range(JB + 3))             # a few spare physical blocks
rnd.shuffle(phys_order)
jmap = phys_order[:JB]
journal_data = bytearray(junk((JB + 3) * BS))
for v, p in enumerate(jmap):
    journal_data[p * BS:(p + 1) * BS] = journal_virtual[v * BS:(v + 1) * BS]
jmap_bytes = b"".join(struct.pack("<II", p | (0x80000000 if rnd.random() < 0.3 else 0), v) for v, p in enumerate(jmap))
jmap_bytes = jmap_bytes.ljust(((len(jmap_bytes) + 0x3FF) // 0x400) * 0x400, b"\0")

# ---------------------------------------------------------------- meta (virtual): journal map, IVFC hashes, FAT
meta_parts = {}
meta = bytearray()
def meta_put(name, data):
    meta_parts[name] = len(meta)
    meta.extend(data)
    meta.extend(b"\0" * ((-len(meta)) % 0x400))
meta_put("jmap", jmap_bytes)
meta_put("ivfc_l1", junk(0x400))
meta_put("ivfc_l2", junk(0x400))
meta_put("ivfc_l3", ivfc_hashes)
meta_put("fat", fat_bytes)
meta.extend(b"\0" * ((-len(meta)) % BS))
META_SIZE = len(meta)

# meta remap: virtual -> duplex data layer, two entries in swapped physical order
half = META_SIZE // 2 // 0x400 * 0x400
meta_remap = [(0, META_SIZE - half, half), (half, 0, META_SIZE - half)]   # (virt, phys, size)
duplex_data = bytearray(META_SIZE)
for virt, phys, size in meta_remap:
    duplex_data[phys:phys + size] = meta[virt:virt + size]

# ---------------------------------------------------------------- hierarchical duplex
DATA_POW, L1_POW = 14, 7
nblk = len(duplex_data) // BS
bits = [rnd.random() < 0.5 for _ in range(nblk)]
dup_a = bytearray(junk(len(duplex_data)))
dup_b = bytearray(junk(len(duplex_data)))
for i, bit in enumerate(bits):
    (dup_b if bit else dup_a)[i * BS:(i + 1) * BS] = duplex_data[i * BS:(i + 1) * BS]

def bitmap(bitlist, size):
    words = []
    for w in range(0, len(bitlist), 32):
        v = 0
        for k, bit in enumerate(bitlist[w:w + 32]):
            if bit:
                v |= 1 << (31 - k)
        words.append(struct.pack("<I", v))
    return b"".join(words).ljust(size, b"\0")

L1_SIZE = 0x100                               # 2 L1 blocks of 128 bytes
l1 = bitmap(bits, L1_SIZE)
l1_bits = [rnd.random() < 0.5 for _ in range(L1_SIZE >> L1_POW)]
l1_a = bytearray(junk(L1_SIZE))
l1_b = bytearray(junk(L1_SIZE))
for i, bit in enumerate(l1_bits):
    s = slice(i << L1_POW, (i + 1) << L1_POW)
    (l1_b if bit else l1_a)[s] = l1[s]
MASTER_SIZE = 0x40
master = bitmap(l1_bits, MASTER_SIZE)
duplex_index = rnd.choice([0, 1])

# ---------------------------------------------------------------- data remap: virtual space of the file region
virt = {}
dvirt = bytearray()
def dv_put(name, data):
    virt[name] = len(dvirt)
    dvirt.extend(data)
    dvirt.extend(b"\0" * ((-len(dvirt)) % BS))
dv_put("l1_a", l1_a)
dv_put("l1_b", l1_b)
dv_put("data_a", dup_a)
dv_put("data_b", dup_b)
dv_put("journal", journal_data)
DV_SIZE = len(dvirt)
# cut into 3 entries stored in reverse physical order
cuts = [0, DV_SIZE // 3 // BS * BS, 2 * DV_SIZE // 3 // BS * BS, DV_SIZE]
data_remap = []
phys = 0
for k in reversed(range(3)):
    size = cuts[k + 1] - cuts[k]
    data_remap.append((cuts[k], phys, size))
    phys += size
data_remap.sort()
file_region = bytearray(DV_SIZE)
for v, p, size in data_remap:
    file_region[p:p + size] = dvirt[v:v + size]

# ---------------------------------------------------------------- the image
FILE_MAP_ENTRY_OFF = 0x8000
META_MAP_ENTRY_OFF = 0x8400
FILE_MAP_DATA_OFF = 0xC000
def remap_table(entries):
    return b"".join(struct.pack("<QQQII", v, p, s, 0, 0) for v, p, s in entries)

def header(valid=True):
    h = bytearray(0x4000)
    layout = [0] * 45
    layout[0] = FILE_MAP_ENTRY_OFF; layout[1] = 0x400
    layout[2] = META_MAP_ENTRY_OFF; layout[3] = 0x400
    layout[4] = FILE_MAP_DATA_OFF; layout[5] = DV_SIZE
    layout[6] = virt["l1_a"]; layout[7] = virt["l1_b"]; layout[8] = L1_SIZE
    layout[9] = virt["data_a"]; layout[10] = virt["data_b"]; layout[11] = len(duplex_data)
    layout[12] = virt["journal"]; layout[13] = len(journal_data); layout[14] = 0; layout[15] = 3 * BS
    layout[16] = 0x3000; layout[17] = 0x3100; layout[18] = MASTER_SIZE
    layout[22] = meta_parts["jmap"]; layout[23] = len(jmap_bytes)
    layout[30] = meta_parts["ivfc_l1"]; layout[31] = 0x400
    layout[32] = meta_parts["ivfc_l2"]; layout[33] = 0x400
    layout[34] = meta_parts["ivfc_l3"]; layout[35] = 0x400
    layout[36] = meta_parts["fat"] if v4 else 0xDEAD00   # v5+: the FAT IVFC header is used instead
    layout[37] = len(fat_bytes)
    layout[38] = duplex_index
    struct.pack_into("<II", h, 0x100, 0x46534944, 0x40000 if v4 else 0x50000)
    struct.pack_into("<45Q", h, 0x128, *layout)
    # master bitmaps: the one in use, the other junk
    h[0x3000:0x3040] = master if duplex_index == 0 else junk(MASTER_SIZE)
    h[0x3100:0x3140] = master if duplex_index == 1 else junk(MASTER_SIZE)
    # duplex header
    struct.pack_into("<II", h, 0x300, 0x53465044, 0x10000)
    for i, (o, l, p) in enumerate([(0, MASTER_SIZE, 6), (0, L1_SIZE, L1_POW), (0, len(duplex_data), DATA_POW)]):
        struct.pack_into("<QQI", h, 0x308 + i * 0x14, o, l, p)
    # data IVFC header: levels L1, L2, L3 (meta), data (journal)
    struct.pack_into("<IIII", h, 0x344, 0x43465649, 0x20000, 0x20, 5)
    lv = [(meta_parts["ivfc_l1"], 0x400, 14), (meta_parts["ivfc_l2"], 0x400, 14),
          (meta_parts["ivfc_l3"], len(ivfc_hashes), 14), (IVFC_DATA_OFF, N * BS, 14)]
    for i, (o, s, p) in enumerate(lv):
        struct.pack_into("<QQII", h, 0x354 + i * 0x18, o, s, p, 0)
    # journal
    struct.pack_into("<IIQQQ", h, 0x408, 0x4C474E4A, 0x10000, JB * BS + 3 * BS, 3 * BS, BS)
    struct.pack_into("<IIII", h, 0x428, 0x10000, JB, 3, 0)
    # save FS + FAT header
    struct.pack_into("<IIQQ", h, 0x608, 0x45564153, 0x60000, N, BS)
    struct.pack_into("<QQIIQIIII", h, 0x620, BS, 0, N, 0, 0, N, 0, dir_blocks[0], file_blocks_tbl[0])
    # remap headers
    struct.pack_into("<IIIII", h, 0x650, 0x50414D52, 0x10000, len(data_remap), 1, 1)
    struct.pack_into("<IIIII", h, 0x690, 0x50414D52, 0x10000, len(meta_remap), 1, 1)
    # FAT IVFC header (v5+): level 2 = the FAT itself
    struct.pack_into("<IIII", h, 0xAD8, 0x43465649, 0x20000, 0x20, 4)
    flv = [(0xBAD0000, 0x400, 14), (0xBAD0000, 0x400, 14), (meta_parts["fat"], len(fat_bytes), 14)]
    for i, (o, s, p) in enumerate(flv):
        struct.pack_into("<QQII", h, 0xAE8 + i * 0x18, o, s, p, 0)
    digest = hashlib.sha256(bytes(h[0x300:0x4000])).digest()
    h[0x108:0x128] = digest if valid else junk(0x20)
    return bytes(h)

image = bytearray(FILE_MAP_DATA_OFF + DV_SIZE)
image[0:0x4000] = header(valid=not use_b)
image[0x4000:0x8000] = header(valid=True) if use_b else junk(0x4000)
image[FILE_MAP_ENTRY_OFF:FILE_MAP_ENTRY_OFF + 0x20 * len(data_remap)] = remap_table(data_remap)
image[META_MAP_ENTRY_OFF:META_MAP_ENTRY_OFF + 0x20 * len(meta_remap)] = remap_table(meta_remap)
image[FILE_MAP_DATA_OFF:] = file_region

# ---------------------------------------------------------------- FAT32 volume
BPS, SPC = 512, 8
CL = BPS * SPC
RESERVED, NFATS = 32, 2
data_clusters = (len(image) + CL - 1) // CL * 2 + 64
FATSZ = (data_clusters + 2) * 4 // BPS + 1
total_sectors = RESERVED + NFATS * FATSZ + data_clusters * SPC
vol = bytearray(total_sectors * BPS)
bs = bytearray(512)
bs[0:3] = b"\xEB\x58\x90"; bs[3:11] = b"MSWIN4.1"
struct.pack_into("<HBHBHHBHHHII", bs, 0x0B, BPS, SPC, RESERVED, NFATS, 0, 0, 0xF8, 0, 63, 255, 0, total_sectors)
struct.pack_into("<IHHIHH", bs, 0x24, FATSZ, 0, 0, 2, 1, 6)
bs[0x52:0x5A] = b"FAT32   "
bs[0x1FE:0x200] = b"\x55\xAA"
vol[0:512] = bs
fat32 = [0x0FFFFFF8, 0x0FFFFFFF] + [0] * data_clusters
next_free = [3]

def clusters_for(nbytes, fragment):
    n = max(1, (nbytes + CL - 1) // CL)
    out = []
    while len(out) < n:
        c = next_free[0]
        next_free[0] += 1
        if fragment and rnd.random() < 0.3:
            next_free[0] += 1      # leave a hole: the chain jumps
        out.append(c)
    for a, b in zip(out, out[1:]):
        fat32[a] = b
    fat32[out[-1]] = 0x0FFFFFFF
    return out

def write_clusters(cl, data):
    for i, c in enumerate(cl):
        off = (RESERVED + NFATS * FATSZ) * BPS + (c - 2) * CL
        part = data[i * CL:(i + 1) * CL]
        vol[off:off + len(part)] = part

def lfn_entries(name, short):
    csum = 0
    for ch in short:
        csum = (((csum & 1) << 7) + (csum >> 1) + ch) & 0xFF
    chars = [ord(c) for c in name] + [0]
    chars += [0xFFFF] * ((-len(chars)) % 13)
    parts = [chars[i:i + 13] for i in range(0, len(chars), 13)]
    ents = []
    for i, p in enumerate(parts):
        e = bytearray(32)
        e[0] = (i + 1) | (0x40 if i == len(parts) - 1 else 0)
        e[0x0B] = 0x0F
        e[0x0D] = csum
        pos = [1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30]
        for k, ch in enumerate(p):
            struct.pack_into("<H", e, pos[k], ch)
        ents.append(bytes(e))
    return list(reversed(ents))

def dirent(short, attr, cluster, size):
    e = bytearray(32)
    e[0:11] = short
    e[0x0B] = attr
    struct.pack_into("<H", e, 0x14, cluster >> 16)
    struct.pack_into("<H", e, 0x1A, cluster & 0xFFFF)
    struct.pack_into("<I", e, 0x1C, size)
    return bytes(e)

# /save/<16 hex>, a decoy save next to it (different id), a deleted entry; the save dir spans 2 clusters
save_name = "%016x" % SAVE_ID
decoy_name = "%016x" % (SAVE_ID + 1)
decoy = junk(5000)
img_cl = clusters_for(len(image), True)
write_clusters(img_cl, image)
decoy_cl = clusters_for(len(decoy), False)
write_clusters(decoy_cl, decoy)
save_dir = bytearray()
save_dir += dirent(b".          ", 0x10, 0, 0) + dirent(b"..         ", 0x10, 0, 0)
for i in range(150):                          # deleted entries, so the directory needs a 2nd cluster
    save_dir += b"\xE5" + bytes(31)
save_dir += b"".join(lfn_entries(decoy_name, b"800000~2   ")) + dirent(b"800000~2   ", 0x20, decoy_cl[0], len(decoy))
save_dir += b"".join(lfn_entries(save_name, b"800000~1   ")) + dirent(b"800000~1   ", 0x20, img_cl[0], len(image))
save_dir_cl = clusters_for(len(save_dir) + 32, True)
assert len(save_dir_cl) >= 2
write_clusters(save_dir_cl, save_dir)
root = bytearray()
root += dirent(b"NAND USER  ", 0x08, 0, 0)  # volume label
root += b"".join(lfn_entries("saveMeta", b"SAVEMETA   ")) + dirent(b"SAVEMETA   ", 0x10, 0, 0)
root += dirent(b"SAVE       ", 0x10, save_dir_cl[0], 0)
fat32[2] = 0x0FFFFFFF
write_clusters([2], root)
fat_raw = b"".join(struct.pack("<I", v) for v in fat32).ljust(FATSZ * BPS, b"\0")
for k in range(NFATS):
    off = (RESERVED + k * FATSZ) * BPS
    vol[off:off + len(fat_raw)] = fat_raw

os.makedirs(os.path.join(out, "expected"), exist_ok=True)
with open(os.path.join(out, "nand.img"), "wb") as f:
    f.write(vol)
with open(os.path.join(out, "save.img"), "wb") as f:
    f.write(image)
for path, data in files.items():
    p = os.path.join(out, "expected", path)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, "wb") as f:
        f.write(data)
with open(os.path.join(out, "info.txt"), "w") as f:
    f.write("%s %d %d\n" % (save_name, UNWRITTEN, len(files)))
print("save image %d bytes, %d files, header %s, %s, duplex index %d, unwritten block %d -> %s"
      % (len(image), len(files), "B" if use_b else "A", "v4" if v4 else "v5", duplex_index, UNWRITTEN, out))
