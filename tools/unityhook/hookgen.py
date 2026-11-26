#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Make the per-game files of the TrophyNX hook (see DESIGN.md).

  python tools/unityhook/unityhook.py hook-add <TITLEID> <dump folder> --method Type::Method --arg x1 --type string
      Finds the method in this dump (its build), checks it can be hooked, and records its offset and the first
      16 bytes there in data/unityhook/<TITLEID>.json, under the main NSO's build ID. Run it again for each game
      version (a new dump); every build ends up in the same file.

  python tools/unityhook/unityhook.py hook-make <TITLEID> <dump folder> [--module hook/deploy/subsdk9] [--out out/hook]
      Writes <out>/atmosphere/contents/<TITLEID>/exefs/subsdk9 (the generic module with this game's table in it)
      and exefs/main.npdm (the dump's own npdm + svcMapProcessMemory / svcUnmapProcessMemory, nothing else),
      ready to copy to the SD card. The npdm comes from YOUR dump and is never committed to the repository.

<dump folder> is what nxdumptool extracts: exefs/main, exefs/main.npdm and romfs/Data/Managed/Metadata/.
"""
import datetime
import hashlib
import json
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import unityhook as uh   # noqa: E402

DATA_DIR = os.path.join(REPO, 'data', 'unityhook')
MAX_BUILDS, MAX_HOOKS = 8, 4
ARG_TYPES = {'int32': 1, 'string': 2}
HOOK_SVCS = (0x74, 0x75)   # svcMapProcessMemory, svcUnmapProcessMemory: exlaunch maps .text read-write with them


# ------------------------------------------------------------------ dump helpers

def dump_files(folder):
    main = os.path.join(folder, 'exefs', 'main')
    npdm = os.path.join(folder, 'exefs', 'main.npdm')
    meta = None
    for dp, _d, fn in os.walk(os.path.join(folder, 'romfs')):
        if 'global-metadata.dat' in fn:
            meta = os.path.join(dp, 'global-metadata.dat')
            break
    for p, what in ((main, 'exefs/main'), (npdm, 'exefs/main.npdm'), (meta, 'romfs/.../global-metadata.dat')):
        if not p or not os.path.isfile(p):
            raise SystemExit('%s not found in %s' % (what, folder))
    return main, npdm, meta


def load_data(tid):
    p = os.path.join(DATA_DIR, tid.upper() + '.json')
    if os.path.isfile(p):
        with open(p, encoding='utf-8') as f:
            return p, json.load(f)
    return p, {'title_id': tid.upper(), 'hooks': [], 'ids': {}, 'builds': {}}


def save_data(p, d):
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(d, f, indent=2, ensure_ascii=False)
        f.write('\n')


PC_RELATIVE = (
    (0x9F000000, 0x10000000),   # adr
    (0x9F000000, 0x90000000),   # adrp
    (0x7C000000, 0x14000000),   # b / bl
    (0xFF000010, 0x54000000),   # b.cond
    (0x7E000000, 0x34000000),   # cbz / cbnz
    (0x7E000000, 0x36000000),   # tbz / tbnz
    (0x3B000000, 0x18000000),   # ldr (literal), ldrsw (literal), prfm (literal)
)


def pc_relative(word):
    return any(word & m == v for m, v in PC_RELATIVE)


# ------------------------------------------------------------------ hook-add

def hook_add(tid, folder, method, arg, typ):
    tid = tid.upper()
    if not re.fullmatch(r'[0-9A-F]{16}', tid):
        raise SystemExit('title id must be 16 hex digits')
    m = re.fullmatch(r'x([0-7])', arg)
    if not m:
        raise SystemExit('--arg must be x0..x7 (the register that holds the achievement id)')
    if typ not in ARG_TYPES:
        raise SystemExit('--type must be int32 or string')
    tname, _, mname = method.rpartition('::')
    if not tname:
        raise SystemExit('--method must look like Type::Method')
    main_p, _npdm, meta_p = dump_files(folder)
    nso_bytes = open(main_p, 'rb').read()
    res = uh.analyse(open(meta_p, 'rb').read(), nso_bytes, r'^%s$' % re.escape(mname), want_all=True)
    hits = [c for c in res['candidates'] if c['method'] == mname and (c['type'] == tname or c['type'].endswith('.' + tname))]
    if 'error' in res:
        raise SystemExit(res['error'])
    if len(hits) != 1:
        raise SystemExit('%d methods named %s (overloads or wrong name): %s' % (
            len(hits), method, ', '.join('%s %s' % (c['type'], c.get('addr', '-')) for c in hits)))
    c = hits[0]
    if 'addr' not in c:
        raise SystemExit('%s has no code in this build (%s)' % (method, c['verdict']))
    if c['shared']:
        raise SystemExit('%s shares its code with %d other methods: a hook there would fire for all of them' % (
            method, c['shared']))
    off = int(c['addr'].split('+')[1], 16)
    nso = uh.Nso(nso_bytes)
    words = [nso.u32(off + 4 * i) for i in range(4)]
    if any(pc_relative(w) for w in words):
        raise SystemExit('the first 4 instructions of %s are PC-relative: not hookable safely' % method)
    if c['kind'] in ('instance', 'virtual') and arg == 'x0':
        raise SystemExit('%s is an instance method: x0 is `this`, the first argument is in x1' % method)
    sig = bytes(nso.img[off:off + 16]).hex().upper()
    build = nso.build_id[:16]

    path, d = load_data(tid)
    hook = {'method': method, 'arg': arg, 'type': typ}
    names = [h['method'] for h in d['hooks']]
    if method in names:
        i = names.index(method)
        if d['hooks'][i] != hook:
            raise SystemExit('%s is already recorded with other settings: %s' % (method, d['hooks'][i]))
    else:
        if len(d['hooks']) >= MAX_HOOKS:
            raise SystemExit('at most %d hooks per game' % MAX_HOOKS)
        d['hooks'].append(hook)
        i = len(d['hooks']) - 1
    b = d['builds'].setdefault(build, {'build_id': nso.build_id, 'offsets': {}})
    b['offsets'][method] = {'offset': '0x%X' % off, 'sig': sig, 'calls': c['calls']}
    if len(d['builds']) > MAX_BUILDS:
        raise SystemExit('at most %d builds per game: remove an old one from %s' % (MAX_BUILDS, path))
    save_data(path, d)
    print('%s: %s at main+0x%X (%s, %d direct calls), build %s -> %s' % (
        tid, method, off, c['kind'], c['calls'], build, os.path.relpath(path, REPO)))
    missing = [h['method'] for h in d['hooks'] if h['method'] not in b['offsets']]
    if missing:
        print('  this build still needs: ' + ', '.join(missing))


# ------------------------------------------------------------------ module config (subsdk9)

def config_blob(d):
    tid = int(d['title_id'], 16)
    builds = []
    for key, b in sorted(d['builds'].items()):
        if any(h['method'] not in b['offsets'] for h in d['hooks']):
            print('  build %s skipped: not every hook has an offset' % key)
            continue
        slots = b''
        for h in d['hooks']:
            o = b['offsets'][h['method']]
            sig = bytes.fromhex(o['sig'])
            assert len(sig) == 16
            slots += struct.pack('<IBB2x16s', int(o['offset'], 16), int(h['arg'][1]), ARG_TYPES[h['type']], sig)
        slots = slots.ljust(24 * MAX_HOOKS, b'\0')
        bid = bytes.fromhex(b['build_id'])[:16].ljust(16, b'\0')
        builds.append(struct.pack('<16sII', bid, len(d['hooks']), 0) + slots)
    if not builds:
        raise SystemExit('no complete build in the data: run hook-add first')
    blob = struct.pack('<8sIIQ', b'TNXHCFG1', 1, len(builds), tid) + b''.join(builds)
    return blob.ljust(24 + 120 * MAX_BUILDS, b'\0'), len(builds)


def nso_segments(data):
    if data[:4] != b'NSO0':
        raise SystemExit('the module is not an NSO')
    flags = struct.unpack_from('<I', data, 0xC)[0]
    segs = []
    for i in range(3):
        foff, moff, size = struct.unpack_from('<III', data, 0x10 + 0x10 * i)
        csize = struct.unpack_from('<I', data, 0x60 + 4 * i)[0]
        raw = data[foff:foff + (csize if flags & (1 << i) else size)]
        if flags & (1 << i):
            raw = uh.lz4_block(raw, size)
        assert len(raw) == size
        segs.append(bytearray(raw))
    return segs


def nso_write(orig, segs):
    """Uncompressed NSO with fresh segment hashes (flags: hash checks on, compression off)."""
    h = bytearray(orig[:0x100])
    name_off, name_size = struct.unpack_from('<I', orig, 0x1C)[0], struct.unpack_from('<I', orig, 0x2C)[0]
    name = orig[name_off:name_off + name_size] if name_off and name_size else b''
    body = bytearray(name)
    pos = 0x100 + len(body)
    struct.pack_into('<I', h, 0x1C, 0x100 if name else 0)
    for i, seg in enumerate(segs):
        struct.pack_into('<I', h, 0x10 + 0x10 * i, pos)              # file offset
        struct.pack_into('<I', h, 0x18 + 0x10 * i, len(seg))          # size (memory offsets unchanged)
        struct.pack_into('<I', h, 0x60 + 4 * i, len(seg))             # file size
        h[0xA0 + 0x20 * i:0xC0 + 0x20 * i] = hashlib.sha256(seg).digest()
        body += seg
        pos += len(seg)
    struct.pack_into('<I', h, 0xC, 0b111000)                          # check hashes, no compression
    return bytes(h + body)


def make_module(module_bytes, blob):
    segs = nso_segments(module_bytes)
    ro = segs[1]
    at = ro.find(b'TNXHCFG1')
    if at < 0 or ro.find(b'TNXHCFG1', at + 1) >= 0:
        raise SystemExit('config table not found exactly once in the module (is it the TrophyNX hook build?)')
    if struct.unpack_from('<II', ro, at + 8) != (1, 0):
        raise SystemExit('the module already has a config: give the clean build (hook/deploy/subsdk9)')
    ro[at:at + len(blob)] = blob
    return nso_write(module_bytes, segs)


# ------------------------------------------------------------------ npdm

def _caps(blob, off, size):
    return [struct.unpack_from('<I', blob, off + 4 * k)[0] for k in range(size // 4)]


def _svcs(caps):
    out = set()
    for w in caps:
        if (~w & (w + 1)).bit_length() - 1 == 4:   # syscall mask descriptor: low bits 01111
            m, idx = (w >> 5) & 0xFFFFFF, w >> 29
            out |= {idx * 24 + b for b in range(24) if m >> b & 1}
    return out


def _add_svcs(caps, svcs):
    caps = list(caps)
    for s in svcs:
        idx, bit = divmod(s, 24)
        for k, w in enumerate(caps):
            if (~w & (w + 1)).bit_length() - 1 == 4 and w >> 29 == idx:
                caps[k] = w | (1 << (5 + bit))
                break
        else:
            caps.append((idx << 29) | (1 << (5 + bit)) | 0xF)
    return caps


def _align(n, a=0x10):
    return (n + a - 1) & ~(a - 1)


def _with_caps(blk, base, kc):
    """The block unchanged except its kernel-capability list (header field at base + 16). The list is rewritten in
    place when it is the block's last section (the usual layout), otherwise appended 0x10-aligned."""
    fo, fs, so, ss, ko, ks = struct.unpack_from('<IIIIII', blk, base)
    if ko >= max(fo + fs, so + ss):   # last section: nothing after it to move
        out = bytearray(blk[:ko]) + kc
        struct.pack_into('<I', out, base + 20, len(kc))
    else:
        out = bytearray(blk).ljust(_align(len(blk)), b'\0')
        struct.pack_into('<II', out, base + 16, len(out), len(kc))
        out += kc
    return out.ljust(_align(len(out)), b'\0')


def patch_npdm(npdm):
    if npdm[:4] != b'META':
        raise SystemExit('main.npdm: no META header')
    aci_off, aci_size, acid_off, acid_size = struct.unpack_from('<IIII', npdm, 0x70)
    blocks = {'ACID': (bytes(npdm[acid_off:acid_off + acid_size]), 0x220, acid_off),
              'ACI0': (bytes(npdm[aci_off:aci_off + aci_size]), 0x20, aci_off)}
    if blocks['ACID'][0][0x200:0x204] != b'ACID' or blocks['ACI0'][0][:4] != b'ACI0':
        raise SystemExit('main.npdm: ACID / ACI0 not found')
    before, new = {}, {}
    for name, (blk, base, _o) in blocks.items():
        ko, ks = struct.unpack_from('<II', blk, base + 16)
        caps = _caps(blk, ko, ks)
        before[name] = _svcs(caps)
        kc = b''.join(struct.pack('<I', w) for w in _add_svcs(caps, HOOK_SVCS))
        new[name] = _with_caps(blk, base, kc)
        if name == 'ACID':
            struct.pack_into('<I', new[name], 0x204, len(new[name]) - 0x100)   # size covered by the signature
    # META, then the two blocks in their original order, 0x10-aligned
    out = bytearray(npdm[:min(acid_off, aci_off)])
    for name in sorted(blocks, key=lambda n: blocks[n][2]):
        out = out.ljust(_align(len(out)), b'\0')
        struct.pack_into('<II', out, 0x78 if name == 'ACID' else 0x70, len(out), len(new[name]))
        out += new[name]
    # check: read back, exactly the two syscalls added, everything else identical
    a2o, a2s, d2o, d2s = struct.unpack_from('<IIII', out, 0x70)
    for name, o, s in (('ACID', d2o, d2s), ('ACI0', a2o, a2s)):
        blk, base, _o = blocks[name]
        nb = bytes(out[o:o + s])
        fo, fs, so, ss, ko, ks = struct.unpack_from('<IIIIII', nb, base)
        ofo, ofs, oso, oss, oko, oks = struct.unpack_from('<IIIIII', blk, base)
        if _svcs(_caps(nb, ko, ks)) != before[name] | set(HOOK_SVCS):
            raise SystemExit('npdm rebuild check failed in %s (syscalls)' % name)
        if nb[fo:fo + fs] != blk[ofo:ofo + ofs] or nb[so:so + ss] != blk[oso:oso + oss]:
            raise SystemExit('npdm rebuild check failed in %s (fs / service lists changed)' % name)
        other = [w for w in _caps(nb, ko, ks) if (~w & (w + 1)).bit_length() - 1 != 4]
        if other != [w for w in _caps(blk, oko, oks) if (~w & (w + 1)).bit_length() - 1 != 4]:
            raise SystemExit('npdm rebuild check failed in %s (other kernel capabilities changed)' % name)
    if bytes(out[:0x70]) != bytes(npdm[:0x70]):
        raise SystemExit('npdm rebuild check failed (META header)')
    added = sorted(set(HOOK_SVCS) - before['ACI0'])
    return bytes(out), added


# ------------------------------------------------------------------ hook-make

def hook_make(tid, folder, module_path, out_root):
    tid = tid.upper()
    path, d = load_data(tid)
    if not os.path.isfile(path):
        raise SystemExit('no %s: run hook-add first' % os.path.relpath(path, REPO))
    main_p, npdm_p, _meta = dump_files(folder)
    if os.path.exists(os.path.join(folder, 'exefs', 'subsdk9')):
        raise SystemExit('this game already has a subsdk9: the hook can\'t be added next to it')
    build = uh.Nso(open(main_p, 'rb').read()).build_id[:16]
    if build not in d['builds']:
        print('warning: this dump (build %s) is not in %s; the module will leave it unhooked' % (
            build, os.path.relpath(path, REPO)))
    blob, n = config_blob(d)
    module = make_module(open(module_path, 'rb').read(), blob)
    npdm_src = open(npdm_p, 'rb').read()
    npdm, added = patch_npdm(npdm_src)
    exefs = os.path.join(out_root, 'atmosphere', 'contents', tid, 'exefs')
    os.makedirs(exefs, exist_ok=True)
    with open(os.path.join(exefs, 'subsdk9'), 'wb') as f:
        f.write(module)
    with open(os.path.join(exefs, 'main.npdm'), 'wb') as f:
        f.write(npdm)
    manifest = {
        'title_id': tid, 'made': datetime.date.today().isoformat(), 'builds': sorted(d['builds']),
        'dump_build': build, 'npdm_sha256_original': hashlib.sha256(npdm_src).hexdigest(),
        'npdm_sha256': hashlib.sha256(npdm).hexdigest(), 'syscalls_added': ['0x%02X' % s for s in added],
        'module_sha256': hashlib.sha256(module).hexdigest(),
    }
    with open(os.path.join(out_root, 'atmosphere', 'contents', tid, 'trophynx-hook.json'), 'w', newline='\n') as f:
        json.dump(manifest, f, indent=2)
        f.write('\n')
    print('%s: %d build(s), syscalls added to the npdm: %s' % (tid, n, ', '.join(manifest['syscalls_added']) or 'none'))
    print('copy %s to the root of the SD card' % os.path.join(out_root, 'atmosphere'))
    print('to remove the hook: delete atmosphere/contents/%s/exefs/subsdk9 and main.npdm' % tid)


def cli(argv):
    import argparse
    ap = argparse.ArgumentParser(prog='unityhook.py ' + argv[0], description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('title_id')
    ap.add_argument('dump')
    if argv[0] == 'hook-add':
        ap.add_argument('--method', required=True)
        ap.add_argument('--arg', required=True)
        ap.add_argument('--type', required=True, choices=sorted(ARG_TYPES))
        a = ap.parse_args(argv[1:])
        hook_add(a.title_id, a.dump, a.method, a.arg, a.type)
    else:
        ap.add_argument('--module', default=os.path.join(REPO, 'hook', 'deploy', 'subsdk9'))
        ap.add_argument('--out', default=os.path.join(REPO, 'out', 'hook'))
        a = ap.parse_args(argv[1:])
        hook_make(a.title_id, a.dump, a.module, a.out)
