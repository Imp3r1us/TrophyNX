#!/usr/bin/env python3
"""Writes version.json for the in-app updater (source/update.cpp).
usage: make_version.py <sd dir> <out.json> <commit> <message> [<release assets dir>]
Every file under <sd dir> is listed with its size, SHA-256 and release asset name. The app downloads the
changed ones from the latest GitHub release (assets) or, on the "builds" channel, from sd/<path> on the
"builds" branch. With a 5th argument, the non-empty files are also copied there under their asset names
(+ version.json) for the release.
<message> "@changelog" = the summary line of this version's CHANGELOG.md section. "notes" (the section's
bullets, shown by the app's update screen) is added whenever CHANGELOG.md has a section for the version."""
import hashlib, json, os, re, shutil, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import changelog
sd, out, commit, message = sys.argv[1:5]
assets_dir = sys.argv[5] if len(sys.argv) > 5 else None
MODULE = 'atmosphere/contents/420000000000AC01/'
def asset_name(path):
    if path == MODULE + 'exefs.nsp': return 'sys-trophynx.nsp'
    if path.startswith(MODULE): return 'sys-trophynx-' + path[len(MODULE):].replace('/', '-')
    return os.path.basename(path)
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
        rel = os.path.relpath(p, sd).replace(os.sep, '/')
        files.append({'path': rel, 'asset': asset_name(rel), 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
        if assets_dir and data:   # GitHub refuses empty assets: the app creates empty files itself
            os.makedirs(assets_dir, exist_ok=True)
            shutil.copyfile(p, os.path.join(assets_dir, asset_name(rel)))
files.sort(key=lambda f: f['path'])
app_version = grab('include/version.h', r'#define APP_VERSION "([^"]+)"')
section = changelog.section(app_version)
if message == '@changelog':
    message = changelog.summary(section)
doc = {
    'commit': commit,
    'date': time.strftime('%Y-%m-%d %H:%M UTC', time.gmtime()),
    'message': message.splitlines()[0][:120] if message else '',
    'notes': changelog.bullets(section),
    'app_version': app_version,
    'module_version': grab('sysmodule/source/main.cpp', r'#define MODULE_VERSION "([^"]+)"'),
    'files': files,
}
names = [f['asset'] for f in files]
assert len(names) == len(set(names)), 'two files with the same asset name'
json.dump(doc, open(out, 'w'), indent=1)
if assets_dir: shutil.copyfile(out, os.path.join(assets_dir, 'version.json'))
print(out + ':', len(files), 'files,', doc['app_version'], '/', doc['module_version'])
