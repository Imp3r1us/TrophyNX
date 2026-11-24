// Writes the synthetic RPG Maker MV/MZ and GameMaker fixtures (tools/savelab/test/fixtures/rpgmaker, gamemaker).
// The files are committed; run again only to change them:
//   npm i lz-string@1.5.0 (anywhere on NODE_PATH), then node tools/savelab/test/make_engine_fixtures.js
'use strict';
const fs = require('fs'), path = require('path'), zlib = require('zlib');
const LZString = require('lz-string');
const fx = path.join(__dirname, 'fixtures');

// ---- RPG Maker: a JsonEx-style save (class markers "@", object ids "@c", arrays with properties "@a", a "@r" reference)
const sw = new Array(21).fill(false); sw[0] = null; sw[3] = true; sw[20] = true;
const vars = new Array(11).fill(0); vars[0] = null; vars[5] = 42; vars[10] = 'Pépé ★';
const save = {
  system: { '@': 'Game_System', '@c': 1, _battleCount: 12, _winCount: 11, _saveCount: 3, _framesOnSave: 216000, _versionId: 123456 },
  screen: { '@': 'Game_Screen', '@c': 2, _brightness: 255 },
  timer: { '@': 'Game_Timer', '@c': 3, _frames: 0, _working: false },
  switches: { '@': 'Game_Switches', '@c': 4, _data: { '@a': sw } },
  variables: { '@': 'Game_Variables', '@c': 5, _data: { '@a': vars } },
  selfSwitches: { '@': 'Game_SelfSwitches', '@c': 6, _data: { '1,4,A': true, '2,7,B': true } },
  actors: { '@': 'Game_Actors', '@c': 7, _data: { '@a': [null, { '@': 'Game_Actor', '@c': 8, _actorId: 1, _name: 'Harold', _level: 17, _exp: { 1: 5400 } }, null] } },
  party: { '@': 'Game_Party', '@c': 9, _gold: 9876, _steps: 4321, _items: { 7: 3, 12: 1 }, _weapons: { 2: 1 }, _armors: {}, _actors: { '@a': [1] }, _leader: { '@r': 8 } },
  map: { '@': 'Game_Map', '@c': 10, _mapId: 4 },
  player: { '@': 'Game_Player', '@c': 11, _x: 8, _y: 12 },
};
const json = JSON.stringify(save);
fs.mkdirSync(path.join(fx, 'rpgmaker'), { recursive: true });
fs.writeFileSync(path.join(fx, 'rpgmaker/file1.rpgsave'), LZString.compressToBase64(json));   // MV
// MZ: pako.deflate(json, {to: "string", level: 1}) written with fs.writeFileSync -> UTF-8 of the binary string
const z = zlib.deflateSync(Buffer.from(json, 'utf8'), { level: 1 });
fs.writeFileSync(path.join(fx, 'rpgmaker/file1.rmmzsave'), Buffer.from(z.toString('latin1'), 'utf8'));
fs.writeFileSync(path.join(fx, 'rpgmaker/global.rpgsave'), LZString.compressToBase64(JSON.stringify([null, { globalId: 'RPGMV', title: 'Test', playtime: '01:00:00' }])));

// ---- GameMaker: ds_list_write / ds_map_write hex
const u32 = (v) => { const b = Buffer.alloc(4); b.writeUInt32LE(v >>> 0); return b; };
const val = (v) => {
  if (v === null) return u32(5);
  if (typeof v === 'string') { const s = Buffer.from(v, 'utf8'); return Buffer.concat([u32(1), u32(s.length), s]); }
  if (typeof v === 'boolean') { const b = Buffer.alloc(8); b.writeDoubleLE(v ? 1 : 0); return Buffer.concat([u32(13), b]); }
  if (typeof v === 'object' && v.int32 !== undefined) { const b = Buffer.alloc(4); b.writeInt32LE(v.int32); return Buffer.concat([u32(7), b]); }
  const b = Buffer.alloc(8); b.writeDoubleLE(v); return Buffer.concat([u32(0), b]);
};
const dsList = (items) => Buffer.concat([u32(0x12F), u32(items.length), ...items.map(val)]).toString('hex').toUpperCase();
const dsMap = (pairs) => Buffer.concat([u32(0x191), u32(pairs.length), ...pairs.flatMap(([k, v]) => [val(k), val(v)])]).toString('hex').toUpperCase();
const inner = dsList([1, 2, 3]);
const map = dsMap([['coins', 250], [3, 'three'], ['boss_dead', true], [1, { int32: -7 }], ['nested', inner], ['coins', 300], ['nothing', null]]);
const ini = [
  '; written by ini_close()',
  '[Progress]',
  'Level="12.000000"',
  'Name="Frisk"',
  'Chapter=3',
  'Flags="' + map + '"',
  '',
  '[Stats]',
  'Deaths="4.000000"',
  'Kills = "0.000000"',
  'Deaths="5.000000"',
  '[10]',
  'Best="1e3"',
  '',
].join('\r\n');
fs.mkdirSync(path.join(fx, 'gamemaker'), { recursive: true });
fs.writeFileSync(path.join(fx, 'gamemaker/save.ini'), '﻿' + ini);
fs.writeFileSync(path.join(fx, 'gamemaker/save_b64.sav'), Buffer.from(ini, 'utf8').toString('base64').replace(/(.{76})/g, '$1\n'));
fs.writeFileSync(path.join(fx, 'gamemaker/file0'), ['Kris', '120', dsList([0, 1, 1, 0, 'item']), '-3.5', 'room_town', ''].join('\n') + '\0\0');
console.log('fixtures written');
