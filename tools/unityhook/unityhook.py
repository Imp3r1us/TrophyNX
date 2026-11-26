#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""List the achievement methods of a Switch Unity (IL2CPP) game, with their addresses in the main NSO.

Usage:
  python tools/unityhook/unityhook.py <global-metadata.dat> <main> [--all] [--json out.json] [--pattern REGEX]
  python tools/unityhook/unityhook.py --scan <folder of dumps> [--json survey.json]
  python tools/unityhook/unityhook.py hook-add | hook-make ...   (the injected hook: see hookgen.py)

<global-metadata.dat>  RomFS: Data/Managed/Metadata/global-metadata.dat (nxdumptool, docs/dumping-guide.md)
<main>                 ExeFS: main (the game's code, an NSO; compressed or not)

For each method whose type or method name matches the pattern (Achievement / Trophy / Unlock / SetAchievement /
ReportProgress by default) it prints:
  main+0x...   the method's entry, as an offset from the start of the main module (what exlaunch / Skyline hook)
  size         bytes up to the next method entry (4 = a lone `ret`: a stub, still hookable if it is called)
  calls        direct BL / B (tail call) sites to that address in .text
  shared       other methods at the same address (identical code folding: hooking it catches all of them)
and a verdict: HOOKABLE (own address, called directly), VIRTUAL (virtual method: called through a vtable or
interface, the entry is still hookable), NO-CALLER? (non-virtual, no direct call: inlined, delegate / UnityEvent /
reflection, or dead code), HOOKABLE-SHARED / STUB-SHARED / NO-CODE (unusable).

Method addresses come from the binary's Il2CppCodeRegistration -> codeGenModules[] -> methodPointers[] (Unity 2019+,
metadata v24.2 and later), found like Il2CppDumper does (credit: Perfare/Il2CppDumper, MIT): a pointer to the
"mscorlib.dll" string is the first field of its Il2CppCodeGenModule. Older builds (one global methodPointers array)
are reported as unsupported.
"""
import argparse
import json
import re
import struct
import sys
from bisect import bisect_right
from collections import Counter, defaultdict

DEFAULT_PATTERN = r'Achiev|Trophy|Troph|Unlock|ReportProgress|SetAchievement|Stats'


# ------------------------------------------------------------------ NSO

def lz4_block(src, size):
    try:
        import lz4.block
        return lz4.block.decompress(src, uncompressed_size=size)
    except ImportError:
        pass
    out = bytearray()
    i = 0
    n = len(src)
    while i < n:
        tok = src[i]; i += 1
        lit = tok >> 4
        if lit == 15:
            while True:
                b = src[i]; i += 1; lit += b
                if b != 255: break
        out += src[i:i + lit]; i += lit
        if i >= n: break
        off = src[i] | (src[i + 1] << 8); i += 2
        ml = tok & 15
        if ml == 15:
            while True:
                b = src[i]; i += 1; ml += b
                if b != 255: break
        ml += 4
        start = len(out) - off
        for k in range(ml):
            out.append(out[start + k])
    return bytes(out)


class Nso:
    def __init__(self, data):
        if data[:4] != b'NSO0':
            raise ValueError('not an NSO (no NSO0 magic): give the ExeFS "main" file')
        flags = struct.unpack_from('<I', data, 0xC)[0]
        self.build_id = data[0x40:0x54].hex().upper()   # the 20 bytes Atmosphere uses for patch / cheat folders
        segs = []
        for i in range(3):
            foff, moff, dsize = struct.unpack_from('<III', data, 0x10 + i * 0x10)
            csize = struct.unpack_from('<I', data, 0x60 + i * 4)[0]
            raw = data[foff:foff + (csize if flags & (1 << i) else dsize)]
            if flags & (1 << i):
                raw = lz4_block(raw, dsize)
            segs.append((moff, dsize, raw))
        bss = struct.unpack_from('<I', data, 0x3C)[0]
        end = max(m + s for m, s, _ in segs) + bss
        img = bytearray(end)
        for m, s, raw in segs:
            img[m:m + len(raw)] = raw
        self.img = img
        self.text = (segs[0][0], segs[0][0] + segs[0][1])
        self.ro = (segs[1][0], segs[1][0] + segs[1][1])
        self.data = (segs[2][0], segs[2][0] + segs[2][1] + bss)
        self._relocate()

    def u32(self, a): return struct.unpack_from('<I', self.img, a)[0]
    def u64(self, a): return struct.unpack_from('<Q', self.img, a)[0]
    ptr = u64          # relocated pointers already hold module offsets
    base = 0
    arch = 'arm64'
    module = 'main'

    def _relocate(self):
        # MOD0 -> .dynamic -> DT_RELA: apply R_AARCH64_RELATIVE so pointers in .data hold module offsets.
        mod0 = self.u32(4)
        if self.img[mod0:mod0 + 4] != b'MOD0':
            raise ValueError('no MOD0 header')
        dyn = mod0 + struct.unpack_from('<i', self.img, mod0 + 4)[0]
        tags = {}
        a = dyn
        while a + 16 <= len(self.img):
            tag, val = struct.unpack_from('<qQ', self.img, a)
            a += 16
            if tag == 0: break
            tags.setdefault(tag, val)
        self.relative = 0
        for tag_off, tag_sz in ((7, 8), (0x17, 2)):   # DT_RELA / DT_RELASZ, DT_JMPREL / DT_PLTRELSZ
            if tag_off not in tags: continue
            off, sz = tags[tag_off], tags.get(tag_sz, 0)
            for r in range(off, off + sz, 24):
                roff, info, addend = struct.unpack_from('<QQq', self.img, r)
                if info & 0xFFFFFFFF == 0x403 and roff + 8 <= len(self.img):   # R_AARCH64_RELATIVE
                    struct.pack_into('<Q', self.img, roff, addend & 0xFFFFFFFFFFFFFFFF)
                    self.relative += 1
        # DT_RELR (newer toolchains): packed relative relocations whose addend is already in place. With the module
        # loaded at 0 they need no change; counted only so the report shows the binary was understood.
        if 0x24 in tags:
            off, sz = tags[0x24], tags.get(0x23, 0)
            for r in range(off, off + sz, 8):
                e = self.u64(r)
                self.relative += 1 if e & 1 == 0 else bin(e >> 1).count('1')

    def cstr(self, a, maxlen=256):
        if not (0 <= a < len(self.img)): return None
        e = self.img.find(b'\0', a, a + maxlen)
        if e < 0: return None
        try:
            return self.img[a:e].decode('ascii')
        except UnicodeDecodeError:
            return None

    def pointers_to(self, target):
        """Aligned 8-byte slots in .rodata / .data that hold `target` (a module offset)."""
        pat = struct.pack('<Q', target + self.base)
        lo, hi = self.ro[0], self.data[1]
        out = []
        i = self.img.find(pat, lo, hi)
        while i >= 0:
            if i % 8 == 0: out.append(i)
            i = self.img.find(pat, i + 1, hi)
        return out

    def bl_targets(self):
        """Counter of direct BL / B targets in .text (B: tail calls; branches inside a function never hit an entry)."""
        lo, hi = self.text
        words = struct.unpack_from('<%dI' % ((hi - lo) // 4), self.img, lo)
        c = Counter()
        pc = lo
        for w in words:
            if w & 0x7C000000 == 0x14000000:   # BL (call) or B (tail call)
                imm = w & 0x3FFFFFF
                if imm & 0x2000000: imm -= 0x4000000
                c[pc + imm * 4] += 1
            pc += 4
        return c


class Pe:
    """Windows GameAssembly.dll (PC IL2CPP build): only used to check the tool against a PC copy of a game."""
    arch = 'x64'
    module = 'GameAssembly'

    def __init__(self, data):
        pe = struct.unpack_from('<I', data, 0x3C)[0]
        if data[:2] != b'MZ' or data[pe:pe + 4] != b'PE\0\0':
            raise ValueError('not a PE file')
        nsec, = struct.unpack_from('<H', data, pe + 6)
        optsz, = struct.unpack_from('<H', data, pe + 20)
        opt = pe + 24
        self.base, = struct.unpack_from('<Q', data, opt + 24)
        size, = struct.unpack_from('<I', data, opt + 56)
        self.img = bytearray(size)
        self.build_id = '%08X' % struct.unpack_from('<I', data, pe + 8)[0]
        self.text = self.ro = self.data = None
        lo_d, hi_d = size, 0
        for k in range(nsec):
            o = opt + optsz + 40 * k
            name = data[o:o + 8].rstrip(b'\0')
            vsz, rva, rsz, roff = struct.unpack_from('<IIII', data, o + 8)
            ch, = struct.unpack_from('<I', data, o + 36)
            self.img[rva:rva + min(rsz, vsz)] = data[roff:roff + min(rsz, vsz)]
            if ch & 0x20000000:   # executable: keep the biggest one (IL2CPP code can live in a custom section)
                if not self.text or vsz > self.text[1] - self.text[0]: self.text = (rva, rva + vsz)
            else:
                lo_d, hi_d = min(lo_d, rva), max(hi_d, rva + vsz)
        self.ro = self.data = (lo_d, hi_d)

    u32 = Nso.u32
    u64 = Nso.u64
    cstr = Nso.cstr
    pointers_to = Nso.pointers_to

    def ptr(self, a):
        v = self.u64(a)
        return v - self.base if v >= self.base else 0

    def bl_targets(self):
        """Counter of direct E8 call targets in .text (byte scan: approximate, but exact targets only count)."""
        lo, hi = self.text
        c = Counter()
        img = self.img
        i = img.find(b'\xE8', lo, hi - 5)
        while i >= 0:
            c[i + 5 + struct.unpack_from('<i', img, i + 1)[0]] += 1
            i = img.find(b'\xE8', i + 1, hi - 5)
        return c


def load_binary(data):
    return Pe(data) if data[:2] == b'MZ' else Nso(data)


def find_legacy_method_pointers(nso, count):
    """Unity 2017-2018 (metadata v24.0/24.1): Il2CppCodeRegistration starts with methodPointersCount then
    methodPointers. Look for that count followed by a pointer to `count` entries that all point into .text."""
    lo, hi = nso.ro[0], nso.data[1]
    t0, t1 = nso.text
    pat = struct.pack('<Q', count)
    i = nso.img.find(pat, lo, hi)
    while i >= 0:
        if i % 8 == 0:
            arr = nso.ptr(i + 8)
            if lo <= arr and arr + 8 * count <= hi:
                step = max(1, count // 200)
                if all(t0 <= nso.ptr(arr + 8 * k) < t1 for k in range(0, count, step)):
                    return arr
        i = nso.img.find(pat, i + 1, hi)
    return None


def find_code_gen_modules(nso, image_names):
    """[(moduleName, methodPointerCount, methodPointers)] of the Il2CppCodeRegistration, or None."""
    needle = b'mscorlib.dll\0'
    starts = []
    i = nso.img.find(needle)
    while i >= 0:
        if i == 0 or nso.img[i - 1] == 0: starts.append(i)
        i = nso.img.find(needle, i + 1)

    def module_at(p):
        if not (nso.ro[0] <= p < nso.data[1] - 24): return None
        name = nso.cstr(nso.ptr(p))
        if not name or not (name.endswith('.dll') or name.startswith('__')): return None
        cnt = nso.u32(p + 8)
        ptrs = nso.ptr(p + 16)
        if cnt and not (0 < ptrs < len(nso.img)): return None
        return name, cnt, ptrs

    for s in starts:
        for mod in nso.pointers_to(s):                 # Il2CppCodeGenModule.moduleName
            for slot in nso.pointers_to(mod):          # codeGenModules[k]
                a = slot
                while module_at(nso.ptr(a - 8)): a -= 8
                b = slot
                while module_at(nso.ptr(b)): b += 8
                mods = [module_at(nso.ptr(x)) for x in range(a, b, 8)]
                names = {m[0] for m in mods}
                if len(mods) >= max(2, len(image_names) // 2) and len(names & set(image_names)) >= len(mods) // 2:
                    return mods
    return None


# ------------------------------------------------------------------ global-metadata.dat

class Metadata:
    """Names of every method / type / image. Versions 24 to 31: header of (offset, size) pairs, fixed-width indexes.
    Version 38+ (Unity 6): header of (offset, size, count) triples and indexes 1, 2 or 4 bytes wide depending on the
    size of the table they point into."""

    def __init__(self, data):
        self.d = data
        magic, self.version = struct.unpack_from('<Ii', data, 0)
        if magic != 0xFAB11BAF:
            raise ValueError('not a global-metadata.dat (bad magic)')
        if self.version < 24:
            raise ValueError('metadata version %d is older than Unity 2018 (v24): unsupported' % self.version)
        if self.version >= 38:
            sec = lambda i: struct.unpack_from('<III', data, 8 + 12 * i)
            self.str_off, self.str_size, _ = sec(2)
            m_off, m_size, m_cnt = sec(5)
            t_off, t_size, t_cnt = sec(19)
            i_off, i_size, i_cnt = sec(20)
            self.tdw = self._width(t_cnt)     # TypeDefinitionIndex width
            self.methods = self._records(m_off, m_size, self._method_layout(m_off, m_size, m_size // m_cnt))
            self.types = self._typedefs(t_off, t_size, t_size // t_cnt)
            self.images = self._images_v38(i_off, i_size // i_cnt, i_cnt)
        else:
            h = lambda o: struct.unpack_from('<II', data, o)
            self.str_off, self.str_size = h(24)
            m_off, m_size = h(48)
            t_off, t_size = h(160)
            self.tdw = 4
            self.methods = self._records(m_off, m_size, self._method_layout(m_off, m_size))
            self.types = self._typedefs(t_off, t_size)
            self.images = self._images()

    @staticmethod
    def _width(count):
        return 1 if count <= 0xFF else 2 if count <= 0xFFFF else 4

    def _idx(self, off, w):
        v = int.from_bytes(self.d[off:off + w], 'little', signed=False)
        return -1 if v == (1 << (8 * w)) - 1 else v

    def s(self, idx):
        a = self.str_off + idx
        e = self.d.find(b'\0', a)
        return self.d[a:e].decode('utf-8', 'replace')

    def _valid_str(self, idx):
        return 0 <= idx < self.str_size

    def _method_layout(self, off, size, only=None):
        # Record size and token position vary with the version: find the pair where every token is 0x06xxxxxx.
        for rs in ([only] if only else range(28, 64, 4)):
            if size % rs: continue
            n = size // rs
            sample = range(0, n, max(1, n // 500))
            for to in (range(6, rs - 4, 2) if only else range(8, rs - 4, 4)):
                if all((struct.unpack_from('<I', self.d, off + k * rs + to)[0] >> 24) == 6 and
                       self._valid_str(struct.unpack_from('<I', self.d, off + k * rs)[0]) for k in sample):
                    rids = [struct.unpack_from('<I', self.d, off + k * rs + to)[0] & 0xFFFFFF for k in sample]
                    if max(rids) > 1:
                        return rs, to
        raise ValueError('method table layout not recognised (metadata v%d)' % self.version)

    def _records(self, off, size, layout):
        rs, to = layout
        # v24.0 / 24.1 (Unity 2017.x - 2018.x): methodIndex, invokerIndex, delegateWrapperIndex, rgctxStartIndex,
        # rgctxCount sit before the token, and methodIndex indexes ONE global methodPointers array.
        self.legacy = self.version == 24 and rs >= 52
        out = []
        self.method_index = []
        self.flags = []   # METHOD_ATTRIBUTE_*: 0x10 static, 0x40 virtual, 0x400 abstract
        for k in range(size // rs):
            o = off + k * rs
            name = struct.unpack_from('<I', self.d, o)[0]
            decl = self._idx(o + 4, self.tdw)
            tok = struct.unpack_from('<I', self.d, o + to)[0]
            out.append((name, decl, tok & 0xFFFFFF))
            self.flags.append(struct.unpack_from('<H', self.d, o + to + 4)[0])
            if self.legacy:
                self.method_index.append(struct.unpack_from('<i', self.d, o + to - 20)[0])
        return out

    def _typedefs(self, off, size, only=None):
        maxdecl = max(m[1] for m in self.methods) + 1
        for rs in ([only] if only else range(64, 160, 4)):
            if size % rs or size // rs < maxdecl: continue
            n = size // rs
            ok = all(self._valid_str(struct.unpack_from('<I', self.d, off + k * rs)[0]) and
                     self._valid_str(struct.unpack_from('<I', self.d, off + k * rs + 4)[0])
                     for k in range(0, n, max(1, n // 500)))
            if ok:
                return [(self.s(struct.unpack_from('<I', self.d, off + k * rs + 4)[0]),
                         self.s(struct.unpack_from('<I', self.d, off + k * rs)[0])) for k in range(n)]
        raise ValueError('type table layout not recognised')

    def _images_v38(self, off, rs, cnt):
        # nameIndex u32, assemblyIndex u32, typeStart (TypeDefinitionIndex), typeCount u32, ...
        imgs = []
        for k in range(cnt):
            o = off + k * rs
            name = struct.unpack_from('<I', self.d, o)[0]
            ts = self._idx(o + 8, self.tdw)
            tc = struct.unpack_from('<I', self.d, o + 8 + self.tdw)[0]
            nm = self.s(name)
            if ts < 0 or ts + tc > len(self.types) or not (nm.endswith('.dll') or nm.startswith('__')):
                raise ValueError('image table not recognised (v%d)' % self.version)
            imgs.append((nm, ts, tc))
        return imgs

    def _images(self):
        # Header slot after typeDefinitions: images (v24.2+) or rgctxEntries then images (v24.0/24.1).
        for ho in (168, 176):
            off, size = struct.unpack_from('<II', self.d, ho)
            for rs in (40, 32, 36, 44, 48, 28):
                if size % rs or not size: continue
                imgs = []
                for k in range(size // rs):
                    name, _asm, ts, tc = struct.unpack_from('<IiiI', self.d, off + k * rs)
                    if not self._valid_str(name): break
                    nm = self.s(name)
                    if not (nm.endswith('.dll') or nm.startswith('__')) or ts < 0 or ts + tc > len(self.types): break
                    imgs.append((nm, ts, tc))
                else:
                    return imgs
        raise ValueError('image table not recognised')


# ------------------------------------------------------------------ main

def analyse(meta_bytes, nso_bytes, pattern, want_all=False):
    md = Metadata(meta_bytes)
    nso = load_binary(nso_bytes)
    res = {'metadata_version': md.version, 'build_id': nso.build_id, 'arch': nso.arch, 'methods': len(md.methods),
           'types': len(md.types), 'images': len(md.images), 'candidates': []}
    legacy = None
    if md.legacy:
        n = max(md.method_index) + 1
        legacy = find_legacy_method_pointers(nso, n)
        mods = [] if legacy is None else None
        res['layout'] = 'v24.0/24.1 global methodPointers[%d]' % n
        if legacy is None:
            res['error'] = 'Il2CppCodeRegistration.methodPointers not found (count %d)' % n
    else:
        mods = find_code_gen_modules(nso, [i[0] for i in md.images])
        res['layout'] = 'codeGenModules'
        if not mods:
            res['error'] = 'Il2CppCodeRegistration.codeGenModules not found (stripped or unusual build)'
    modmap = {m[0]: m for m in (mods or [])}
    type_image = {}
    for nm, ts, tc in md.images:
        for t in range(ts, ts + tc): type_image[t] = nm

    def addr_of(mi):
        if legacy is not None:
            ix = md.method_index[mi]
            return nso.ptr(legacy + 8 * ix) if ix >= 0 else 0
        name, decl, rid = md.methods[mi]
        mod = modmap.get(type_image.get(decl))
        if not mod or not rid or rid > mod[1]: return 0
        return nso.ptr(mod[2] + 8 * (rid - 1))

    rx = re.compile(pattern, re.I)
    picked = []
    for mi, (name, decl, rid) in enumerate(md.methods):
        ns, tn = md.types[decl] if 0 <= decl < len(md.types) else ('', '?')
        mn = md.s(name)
        if rx.search(mn) or rx.search(tn):
            picked.append((mi, ns, tn, mn))
    res['type_hits'] = sorted({(p[1] + '.' if p[1] else '') + p[2] for p in picked})

    resolved = bool(mods) or legacy is not None
    if resolved:
        allp = [addr_of(mi) for mi in range(len(md.methods))]
        shared = Counter(a for a in allp if a)
        starts = sorted(shared)
        calls = nso.bl_targets()
        res['resolved'] = sum(1 for a in allp if a)
    for mi, ns, tn, mn in picked:
        a = addr_of(mi) if resolved else 0
        fl = md.flags[mi]
        c = {'type': (ns + '.' if ns else '') + tn, 'method': mn, 'image': type_image.get(md.methods[mi][1], '?'),
             'kind': 'abstract' if fl & 0x400 else 'virtual' if fl & 0x40 else 'static' if fl & 0x10 else 'instance'}
        if a:
            k = bisect_right(starts, a)
            c.update(addr='%s+0x%X' % (nso.module, a), size=(starts[k] - a) if k < len(starts) else None,
                     calls=calls.get(a, 0), shared=shared[a] - 1)
            if c['shared'] > 0 and c['size'] is not None and c['size'] <= 8:
                c['verdict'] = 'STUB-SHARED'
            elif c['calls'] > 0:
                c['verdict'] = 'HOOKABLE' if not c['shared'] else 'HOOKABLE-SHARED'
            elif c['kind'] == 'virtual':
                c['verdict'] = 'VIRTUAL'      # called through the vtable / an interface: hook the entry, still fine
            else:
                c['verdict'] = 'NO-CALLER?'   # inlined, delegate / UnityEvent / reflection, or dead code
        else:
            c['verdict'] = 'NO-CODE'   # abstract / interface / generic definition / stripped
        res['candidates'].append(c)
    if not want_all:
        res['candidates'] = [c for c in res['candidates'] if c['verdict'] != 'NO-CODE' or
                             re.search(r'Achiev|Troph', c['method'], re.I)]
    return res


# Methods that look like the game's own "unlock" call (the Goldberg-style hook point), not UI or data classes.
STRONG = re.compile(r'^(Unlock|Set|Award|Give|Grant|Report|Complete|Trigger|Earn|Increment|Progress)'
                    r'\w*(Achiev|Troph|Stats?(?![a-z]))|^(Unlock|Award|Grant|Complete|Trigger|Earn)$|'
                    r'^(Achiev|Troph)\w*(Unlock|Award|Complete|Earn|Progress)', re.I)
UI_TYPE = re.compile(r'(^|\.)(UI|Ui)(\b|Logic)|UI\b|Ui$|Tooltip|Debug|^FMOD\.|Binding|View$|Panel|Item$|Tab$|Button|Popup|Menu|Screen|Widget', re.I)


def strong(c):
    if UI_TYPE.search(c['type']): return False
    return bool(STRONG.search(c['method'])) or (bool(re.search(r'Achiev|Troph', c['type'], re.I)) and
                                                 bool(re.match(r'(Unlock|Award|Complete|Grant|Report)', c['method'], re.I)))


def find_pairs(root):
    """(metadata path, binary path) for every global-metadata.dat under root: the binary is the nearest file named
    `main` that starts with NSO0 (nxdumptool ExeFS), or GameAssembly.dll (PC), under the same game folder."""
    import os
    out = []
    for dp, _dn, fn in os.walk(root):
        if 'global-metadata.dat' not in fn: continue
        meta = os.path.join(dp, 'global-metadata.dat')
        up = dp
        found = None
        for _ in range(7):
            up = os.path.dirname(up)
            for dp2, _d2, fn2 in os.walk(up):
                for f in fn2:
                    q = os.path.join(dp2, f)
                    if f == 'GameAssembly.dll' or (f == 'main' and open(q, 'rb').read(4) == b'NSO0'):
                        found = q
                        break
                if found: break
            if found: break
        out.append((meta, found))
    return out


def print_result(res):
    print('metadata v%d, %d types, %d methods, %d images; %s; build id %s' %
          (res['metadata_version'], res['types'], res['methods'], res['images'], res.get('layout'), res['build_id']))
    if 'error' in res:
        print('!! ' + res['error'])
    else:
        print('%d method addresses resolved' % res['resolved'])
    for c in res['candidates']:
        print('%-15s %-16s %-8s size %-6s calls %-3s shared %-3s %s%s::%s' % (
            c['verdict'], c.get('addr', '-'), c['kind'], c.get('size', '-'), c.get('calls', '-'), c.get('shared', '-'),
            '* ' if strong(c) else '', c['type'], c['method']))


def main():
    if len(sys.argv) > 1 and sys.argv[1] in ('hook-add', 'hook-make'):
        sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
        import hookgen
        return hookgen.cli(sys.argv[1:])
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('metadata', help='global-metadata.dat, or with --scan a folder of dumps')
    ap.add_argument('main', nargs='?', help='the ExeFS main (NSO), or GameAssembly.dll for a PC build')
    ap.add_argument('--scan', action='store_true', help='summarise every game found under the folder')
    ap.add_argument('--pattern', default=DEFAULT_PATTERN)
    ap.add_argument('--all', action='store_true', help='also list methods without code')
    ap.add_argument('--json', help='write the result(s) as JSON')
    a = ap.parse_args()
    if a.scan:
        results = []
        for meta, binp in find_pairs(a.metadata):
            r = {'metadata_path': meta, 'binary_path': binp}
            if not binp:
                r['error'] = 'no ExeFS main / GameAssembly.dll found next to it'
            else:
                try:
                    r.update(analyse(open(meta, 'rb').read(), open(binp, 'rb').read(), a.pattern))
                except Exception as e:   # one bad dump must not stop the survey
                    r['error'] = '%s: %s' % (type(e).__name__, e)
            v = Counter(c['verdict'] for c in r.get('candidates', []))
            st = [c for c in r.get('candidates', []) if strong(c)]
            sh = [c for c in st if c['verdict'] in ('HOOKABLE', 'VIRTUAL', 'NO-CALLER?') and not c['shared']]
            r['summary'] = {'verdicts': dict(v), 'strong': len(st), 'strong_usable': len(sh)}
            results.append(r)
            print('== %s' % meta)
            print('   v%s %s  %s  strong %d, usable %d%s' % (
                r.get('metadata_version', '?'), r.get('layout', ''), dict(v), len(st), len(sh),
                ('  !! ' + r['error']) if 'error' in r else ''))
            for c in sh[:12]:
                print('   %-10s %-18s %-8s calls %-3s %s::%s' % (c['verdict'], c['addr'], c['kind'], c['calls'], c['type'],
                                                             c['method']))
        out = results
    else:
        if not a.main:
            ap.error('give the main NSO (or GameAssembly.dll), or use --scan')
        out = analyse(open(a.metadata, 'rb').read(), open(a.main, 'rb').read(), a.pattern, a.all)
        print_result(out)
    if a.json:
        with open(a.json, 'w', encoding='utf-8') as f:
            json.dump(out, f, indent=1)


if __name__ == '__main__':
    main()
