#!/usr/bin/env python3
"""Builds tools/savelab/index.html (single self-contained page) from app.html + formats.js + engine.js + romfs/games."""
import json, glob, os
here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(os.path.dirname(here))
games = {}
for f in sorted(glob.glob(os.path.join(root, 'romfs', 'games', '*.json'))):
    d = json.load(open(f, encoding='utf-8'))
    games[d['title_id'].upper()] = d
html = open(os.path.join(here, 'app.html'), encoding='utf-8').read()
engine = open(os.path.join(here, 'engine.js'), encoding='utf-8').read()
formats = open(os.path.join(here, 'formats.js'), encoding='utf-8').read()
matcher = open(os.path.join(root, 'tools', 'pc2switch', 'match.js'), encoding='utf-8').read()
# PC -> Switch data (tools/pc2switch/catalog.js): compact catalogue rows + Steam lists of the games we care about
pcdata = {'catalog': [], 'steam': {}, 'generated': ''}
cat_path = os.path.join(root, 'data', 'pc2switch', 'catalog.json')
if os.path.exists(cat_path):
    cat = json.load(open(cat_path, encoding='utf-8'))
    pcdata['generated'] = cat.get('generated', '')
    pcdata['catalog'] = [[g['id'], g['name'], g['appid'], g.get('ach'), g.get('how', ''), g.get('status', '')] for g in cat.get('games', [])]
for f in sorted(glob.glob(os.path.join(root, 'data', 'pc2switch', 'steam', '*.json'))):
    d = json.load(open(f, encoding='utf-8'))
    pcdata['steam'][str(d['appid'])] = {'name': d.get('name', ''), 'achievements': [{k: a.get(k, '') for k in ('name', 'displayName', 'description', 'hidden')} for a in d.get('achievements', [])]}
safe = lambda o: json.dumps(o, ensure_ascii=False, separators=(',', ':')).replace('</', '<\\/')
html = html.replace('/*FORMATS*/', formats).replace('/*ENGINE*/', engine).replace('/*BUILTINS*/', safe(games)).replace('/*PCDATA*/', safe(pcdata)).replace('/*PC2SWITCH*/', matcher)
open(os.path.join(here, 'index.html'), 'w', encoding='utf-8').write(html)
print('index.html', len(html), 'bytes,', len(games), 'games,', len(pcdata['catalog']), 'catalogue rows,', len(pcdata['steam']), 'Steam lists')
