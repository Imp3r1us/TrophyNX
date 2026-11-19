#!/usr/bin/env python3
"""Writes version.json for the in-app updater (source/update.cpp).
usage: make_version.py <sd dir> <out.json> <commit> <message>
Every file under <sd dir> is listed with its size and SHA-256; the app downloads the changed ones from
sd/<path> on the "builds" branch."""
import hashlib, json, os, re, sys, time
sd, out, commit, message = sys.argv[1:5]
root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
def grab(path, pattern):
    try:
        m = re.search(pattern, open(os.path.join(root, path), encoding='utf-8').read())
        return m.group(1) if m else ''
    except OSError:
        return ''
files = []
for d, _, names in os.walk(sd):
    for n in sorted(names):
        p = os.path.join(d, n)
        data = open(p, 'rb').read()
        files.append({'path': os.path.relpath(p, sd).replace(os.sep, '/'), 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
files.sort(key=lambda f: f['path'])
doc = {
    'commit': commit,
    'date': time.strftime('%Y-%m-%d %H:%M UTC', time.gmtime()),
    'message': message.splitlines()[0][:120] if message else '',
    'app_version': grab('include/version.h', r'#define APP_VERSION "([^"]+)"'),
    'module_version': grab('sysmodule/source/main.cpp', r'#define MODULE_VERSION "([^"]+)"'),
    'files': files,
}
json.dump(doc, open(out, 'w'), indent=1)
print(out + ':', len(files), 'files,', doc['app_version'], '/', doc['module_version'])
