#!/usr/bin/env python3
"""Reads CHANGELOG.md (one "## X.Y.Z" section per version) for releases and the in-app updater.

usage:
  changelog.py check <version>                         exit 1 if CHANGELOG.md has no section for it
  changelog.py notes <version> <new version.json> [<previous version.json>]
                                                       GitHub release notes (markdown) on stdout

Section format: first non-empty line = one-sentence summary; then "### Heading" + "- bullet" lines.
make_version.py uses summary() and bullets() for version.json ("message" and "notes")."""
import json, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PATH = os.path.join(ROOT, 'CHANGELOG.md')


def section(version, path=PATH):
    """Lines of the "## <version>" section (heading excluded), or None."""
    try:
        lines = open(path, encoding='utf-8').read().splitlines()
    except OSError:
        return None
    out, inside = [], False
    for l in lines:
        m = re.match(r'^##\s+(\S+)', l)
        if m and not l.startswith('###'):
            if inside:
                break
            inside = m.group(1) == version
            continue
        if inside:
            out.append(l)
    if not inside and not out:
        return None
    while out and not out[-1].strip():
        out.pop()
    return out


def summary(lines):
    for l in lines or []:
        s = l.strip()
        if s and not s.startswith('#') and not s.startswith('- '):
            return s
    return ''


def plain(s):
    """Markdown -> plain text for the Switch screen."""
    s = re.sub(r'\[([^\]]+)\]\([^)]+\)', r'\1', s)
    return s.replace('`', '').replace('**', '').replace('__', '').strip()


def bullets(lines, limit=8, width=100):
    """Top-level bullets, prefixed by their heading when it isn't "New" ("Fixed: ...")."""
    out, head = [], ''
    for l in lines or []:
        if l.startswith('### '):
            head = plain(l[4:])
        elif l.startswith('- '):
            t = plain(l[2:])
            h = 'Module' if head.lower().startswith('live module') else head
            if h and h.lower() != 'new':
                t = h + ': ' + t
            out.append(t if len(t) <= width else t[:width - 3].rstrip() + '...')
    return out[:limit]


def files_changed(new, old):
    """Which files of the SD layout this update replaces (compared with the previous release)."""
    before = {f['path']: f['sha256'] for f in old.get('files', [])}
    changed = [f for f in new.get('files', []) if before.get(f['path']) != f['sha256']]
    return sorted(changed, key=lambda f: f['path'] != 'switch/TrophyNX.nro')   # the app first


def label(path):
    if path == 'switch/TrophyNX.nro':
        return 'the app (`TrophyNX.nro`)'
    if path.endswith('/exefs.nsp'):
        return 'the live module (`sys-trophynx`)'
    return '`' + path + '`'


def notes(version, new_json, old_json=None):
    lines = section(version)
    new = json.load(open(new_json, encoding='utf-8'))
    module = new.get('module_version', '')
    old = None
    if old_json and os.path.exists(old_json):
        try:
            old = json.load(open(old_json, encoding='utf-8'))
        except ValueError:
            old = None
    md = [f'**{summary(lines)}**', '']
    body = [l for l in lines if l.strip() != summary(lines)]
    while body and not body[0].strip():
        body.pop(0)
    md += body + ['', '### What this update replaces']
    if old:
        changed = files_changed(new, old)
        prev_mod = old.get('module_version', '')
        prev_app = old.get('app_version', '')
        if not changed:
            md.append(f'- Nothing on the SD card changed since {prev_app}.')
        for f in changed:
            t = '- ' + label(f['path'])
            if f['path'].endswith('/exefs.nsp') and prev_mod and prev_mod != module:
                t += f': {prev_mod} → {module}, restarted by the updater (no reboot needed)'
            elif f['path'] == 'switch/TrophyNX.nro' and prev_app:
                t += f': {prev_app} → {version}, the app restarts after the update'
            md.append(t)
        if not any(f['path'].endswith('/exefs.nsp') for f in changed):
            md.append(f'- The live module is unchanged ({module}): it keeps running.')
    else:
        md.append(f'- App {version} · live module {module} (no previous release to compare with).')
    md += ['', '### Install / update',
           '- **Update:** press **ZL** in the app.',
           '- **Fresh install:** unzip `TrophyNX-sd.zip` at the root of the SD card (app + live module), '
           'or copy `TrophyNX.nro` to `sdmc:/switch/` (app only).', '']
    return '\n'.join(md)


if __name__ == '__main__':
    if len(sys.argv) >= 3 and sys.argv[1] == 'check':
        lines = section(sys.argv[2])
        if not lines or not summary(lines):
            print(f'CHANGELOG.md has no "## {sys.argv[2]}" section with a summary line: add one before releasing.')
            sys.exit(1)
        print(f'CHANGELOG.md: {sys.argv[2]} - {summary(lines)} ({len(bullets(lines, 99))} bullets)')
    elif len(sys.argv) >= 4 and sys.argv[1] == 'notes':
        sys.stdout.write(notes(sys.argv[2], sys.argv[3], sys.argv[4] if len(sys.argv) > 4 else None))
    else:
        print(__doc__)
        sys.exit(2)
