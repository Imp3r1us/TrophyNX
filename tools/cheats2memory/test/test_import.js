// node tools/cheats2memory/test/test_import.js — cheat code decoding and memory drafts (offline).
'use strict';
const C = require('../import.js'), E = require('../../savelab/engine.js');
let fails = 0;
const ok = (c, m) => { if (!c) { fails++; console.log('FAIL', m); } else console.log('ok  ', m); };

// pointer chain (Blasphemous "Max Gold", switch-cheats-db): 7 loads, an add, a 4-byte store of a float
const chain = `[Max Gold]
580F0000 047E81F8
580F1000 000000A0
580F1000 00000000
580F1000 00000020
580F1000 00000018
580F1000 00000078
580F1000 00000190
780F0000 00000048
640F0000 00000000 47C34FF0`;
const d = C.decode(chain);
ok(d && d.region === 'main' && d.width === 4 && C.formatAddress(d.region, d.offsets) === '[[[[[[[main+47E81F8]+A0]]+20]+18]+78]+190]+48', 'pointer chain decoded');
const v = C.valueOf(d);
ok(v.type === 'f32' && Math.abs(v.v - 99999.875) < 1e-3, 'float recognised from the written bits (a u32 read would unlock at once)');
ok(E.parseMemAddress(C.formatAddress(d.region, d.offsets)), 'address accepted by the Save Lab / app syntax');
// add between two loads folds into the next offset; sub works
const d2 = C.decode('580F0000 00001000\n780F0000 00000010\n580F1000 00000008\n780F1000 00000004\n620F0000 00000000 0000270F');
ok(d2 && C.formatAddress(d2.region, d2.offsets) === '[[main+1000]+18]-4' && d2.width === 2 && C.valueOf(d2).type === 'u16' && C.valueOf(d2).v === 9999, 'adds and subs folded, u16');
// single static store to the heap; code patches (main) and conditional codes skipped
const d3 = C.decode('04100000 61C77360 0098967F');
ok(d3 && d3.region === 'heap' && C.formatAddress(d3.region, d3.offsets) === 'heap+61C77360' && C.valueOf(d3).v === 9999999, 'static heap store');
ok(C.decode('04000000 0035BB00 52800020') === null, 'write to main (code patch) skipped');
ok(C.decode('80000040\n04100000 00000010 00000001\n20000000') === null, 'button / conditional code skipped');
ok(C.decode('580F0000 00001000\n640F0000 00000000 00000063\n640F0000 00000000 00000064') === null, 'several stores skipped');
ok(C.decode('583F0000 00001000\n640F0000 00000000 00000063') === null, 'aslr region skipped');
ok(C.decode('080F0000 00000010 00000000 0000FFFF') === null, 'register-relative static store with a register skipped unless plain');

// names
ok(C.classify('Max Gold').word === 'gold' && C.classify('99999 Money').kind === 'currency' && C.classify('Inf Rupees').word === 'rupees', 'currency names');
ok(C.classify('Gold Multiplier') === null && C.classify('Shop prices 0 gold') === null && C.classify('Enemy level 1') === null && C.classify('Max HP') === null, 'not a balance: skipped');
ok(C.classify('Max Level').kind === 'level' && C.classify('Level up') === null, 'level names');
ok(C.tiers('currency', 99999.875).join() === '100,1000,10000' && C.tiers('currency', 4294967295).join() === '10000,100000,1000000', 'currency tiers from the cap, huge values capped');
ok(C.tiers('level', 99).join() === '25,50,99' && C.tiers('level', 3).length === 0, 'level tiers');

// a whole game: two builds, two save slots in one build
const game = {
  AAAAAAAAAAAAAAAA: { '[Max Money Slot 1]': '04100000 00000100 05F5E0FF', '[Max Money Slot 2]': '04100000 00000200 05F5E0FF', '[Invincible]': '04000000 0035BB00 52800020' },
  BBBBBBBBBBBBBBBB: { '[Max Money]': '580F0000 00001000\n640F0000 00000000 05F5E0FF' },
  attribution: { 'Some Game v1.0 by Author.txt': '' },
};
const g = C.draftGame('0100000000ABC000', game);
ok(g.def && g.def.achievements.length === 3, 'three tiers drafted');
const r = g.def.achievements[2].rule;
ok(r.type === 'memory' && r.value_type === 'u32' && r.target === 1000000 && Array.isArray(r.builds.AAAAAAAAAAAAAAAA) && r.builds.AAAAAAAAAAAAAAAA.length === 2 &&
   r.builds.BBBBBBBBBBBBBBBB === '[main+1000]', 'both builds, both slots (any holds)');
ok(g.def.achievements.every(a => a._draft === 'unverified') && g.def._source[0] === 'Some Game v1.0 by Author', 'unverified, authors credited');
const errs = E.validate({ title_id: '0100000000ABC000', achievements: g.def.achievements });
ok(errs.length === 0, 'drafts pass Save Lab validation' + (errs.length ? ': ' + errs.join('; ') : ''));

console.log(fails ? fails + ' FAILED' : 'ALL PASSED');
process.exit(fails ? 1 : 0);
