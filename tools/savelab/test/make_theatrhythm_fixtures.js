// Writes synthetic THEATRHYTHM FINAL BAR LINE saves for the tests (Save Lab + app), encrypted with a DUMMY
// password (the game's real one is never stored in this repo):
//   node tools/savelab/test/make_theatrhythm_fixtures.js [outdir]   (default: test/fixtures/theatrhythm)
// backup.inf      Switch layout, DEFLATE level 9 (dynamic Huffman), the counters used by romfs/games/010081B01777C000.json
// musicBackup.inf PS4 layout (u32 length prefix), DEFLATE level 1, per-song records ([Key(n)] arrays)
// proficaBackup.inf Switch layout, stored DEFLATE block (level 0)
// edge.inf        every MessagePack type (keys of every kind, ext, timestamps, 64-bit ints, duplicates)
const fs = require('fs'), path = require('path'), crypto = require('crypto'), zlib = require('zlib');
const PASSWORD = 'dummy-password-for-tests';

// ---- minimal MessagePack encoder (only what the fixtures need; values wrapped to force a type)
const B = (...a) => Buffer.from(a);
const raw = (t, buf) => ({ __raw: Buffer.concat([Buffer.isBuffer(t) ? t : B(t), buf]) });
const f32 = (v) => { const b = Buffer.alloc(5); b[0] = 0xca; b.writeFloatBE(v, 1); return { __raw: b }; };
const u64 = (v) => { const b = Buffer.alloc(9); b[0] = 0xcf; b.writeBigUInt64BE(BigInt(v), 1); return { __raw: b }; };
const i64 = (v) => { const b = Buffer.alloc(9); b[0] = 0xd3; b.writeBigInt64BE(BigInt(v), 1); return { __raw: b }; };
const ts12 = (sec, ns = 0) => { const b = Buffer.alloc(15); b[0] = 0xc7; b[1] = 12; b[2] = 0xff; b.writeUInt32BE(ns, 3); b.writeBigInt64BE(BigInt(sec), 7); return { __raw: b }; };
const ts4 = (sec) => { const b = Buffer.alloc(6); b[0] = 0xd6; b[1] = 0xff; b.writeUInt32BE(sec, 2); return { __raw: b }; };
const ts8 = (sec, ns) => { const b = Buffer.alloc(10); b[0] = 0xd7; b[1] = 0xff; const v = (BigInt(ns) << 34n) | BigInt(sec); b.writeBigUInt64BE(v, 2); return { __raw: b }; };
const ext = (type, bytes) => { const b = Buffer.alloc(3 + bytes.length); b[0] = 0xc7; b[1] = bytes.length; b.writeInt8(type, 2); Buffer.from(bytes).copy(b, 3); return { __raw: b }; };
const bin = (bytes) => raw(B(0xc4, bytes.length), Buffer.from(bytes));
// map with explicit entries (keys may be numbers, null, booleans, floats...)
const M = (entries) => ({ __map: entries });
function enc(v) {
  if (v && v.__raw) return v.__raw;
  if (v === null) return B(0xc0);
  if (v === true) return B(0xc3);
  if (v === false) return B(0xc2);
  if (typeof v === 'number') {
    if (!Number.isInteger(v)) { const b = Buffer.alloc(9); b[0] = 0xcb; b.writeDoubleBE(v, 1); return b; }
    if (v >= 0 && v < 128) return B(v);
    if (v < 0 && v >= -32) return B(v & 0xff);
    if (v >= 0 && v <= 0xffffffff) { const b = Buffer.alloc(5); b[0] = 0xce; b.writeUInt32BE(v, 1); return b; }
    if (v < 0 && v >= -0x80000000) { const b = Buffer.alloc(5); b[0] = 0xd2; b.writeInt32BE(v, 1); return b; }
    return i64(v).__raw;
  }
  if (typeof v === 'string') {
    const s = Buffer.from(v, 'utf8');
    if (s.length < 32) return Buffer.concat([B(0xa0 | s.length), s]);
    if (s.length < 256) return Buffer.concat([B(0xd9, s.length), s]);
    const h = Buffer.alloc(3); h[0] = 0xda; h.writeUInt16BE(s.length, 1); return Buffer.concat([h, s]);
  }
  if (Array.isArray(v)) {
    const h = v.length < 16 ? B(0x90 | v.length) : (() => { const b = Buffer.alloc(3); b[0] = 0xdc; b.writeUInt16BE(v.length, 1); return b; })();
    return Buffer.concat([h, ...v.map(enc)]);
  }
  const entries = v.__map || Object.entries(v);
  const h = entries.length < 16 ? B(0x80 | entries.length) : (() => { const b = Buffer.alloc(3); b[0] = 0xde; b.writeUInt16BE(entries.length, 1); return b; })();
  return Buffer.concat([h, ...entries.flatMap(([k, x]) => [enc(k), enc(x)])]);
}
const MIN_DATE = ts12(-62135596800);   // DateTime.MinValue, as the game writes it

function encrypt(plain, level, ps4) {
  const data = zlib.deflateRawSync(plain, { level });
  const q = [0, 1, 2, 3].map(i => crypto.createHash('sha1').update('q' + i + plain.length).digest().subarray(0, 8));
  const salt = Buffer.concat([q[0], q[3]]), iv = Buffer.concat([q[2], q[1]]);
  const key = crypto.pbkdf2Sync(PASSWORD, salt, 1000, 16, 'sha1');
  const c = crypto.createCipheriv('aes-128-cbc', key, iv);   // PKCS#7 padding of 1..16 bytes
  const body = Buffer.concat([...q, c.update(data), c.final()]);
  if (!ps4) return body;
  const len = Buffer.alloc(4); len.writeUInt32LE(body.length); return Buffer.concat([len, body]);
}

// ---- the saves
const playStyle = (hs, play, clear, fc, mark, dmg) => [hs, MIN_DATE, 3, 40, mark, Array.from({ length: 20 }, (_, i) => f32(i * 0.05)), play, clear, fc, dmg];
const world = (cleared, completed) => ({ Opened: true, Cleared: cleared, GetClearReward: cleared, Completed: completed, GetCompleteReward: false, PlayedStartProduction: true,
  StageDictionary: M([[60000001, { Cleared: cleared, QuestCleared: completed, GetQuestClearReward: false, LastClearedDifficulty: 1, LastClearedPlayStyle: 0 }]]), CurrentPositionX: -1, CurrentPositionY: -1 });
const playInfo = (kills, items) => ({ TotalInputSuccessCount: 100, TotalCriticalHitTriggerInputSuccessCount: 50, TotalMonsterKillCount: kills, TotalFMSMoveDistance: 1.5, TotalItemGetCount: items, TotalDamage: 99999 });
const backup = {
  UserInfo: { PlayTimeDouble: 3600.25, CompletedInitialSetting: true, Version: '1.00' },
  MusicStageInfo: { PartyNumber: 0, TotalScore: i64(5000000123), TotalAbilityCount: 1200, TotalSummonCount: 80,
    StandardPlayInfo: playInfo(4000, 700), SimplePlayInfo: playInfo(900, 200), PairPlayInfo: playInfo(150, 150), FullChainStreakCount: 2, Version: '1.00' },
  RhythpoInfo: { TotalRhythpoValue: 123456, AlreadyRewardRhythpoValue: 0, LastRewardID: 4294967295, EverydayPlayBonus: { Days: 3, LastDateTime: MIN_DATE }, Version: '1.00' },
  MusicSelectInfo: { LastSelectedMusicID: 4294967295, PickupLastLottedDateUniversal: ts12(1795262771, 500), TotalPickupClearCount: 12, PickupFullChainCount: 5, Version: '1.00' },
  SeriesQuestInfo: { KeyCount: 2, WorldDictionary: M([...Array.from({ length: 12 }, (_, i) => [50000000 + i, world(true, i < 11)]), [50000029, world(false, false)]]),
    EndlessWorldUniqueInfo: { BestStageClearCount: 21, BestFullChainContinuationCount: 1600, PickedupFlags: M([[70000002, true], [70000001, false]]) },
    LookedEndingWorldUnlock: true, PlayedStaffCredit: true, TotalPlayTime: 33.5, TotalMSClearCount: 300, Version: '1.00' },
  CharacterInfo: { Characters: M([230000003, 230000004, 230000005, 230000006, 230000007].map((id, i) => [id, { UnlockedFlag: true, Level: i < 4 ? 99 : 50, StarLevel: i === 0 ? 9 : 2,
    AbilityData: M([[240000229, { UnlockedFlag: true }], [4294967295, { UnlockedFlag: false }]]), LastSetAbilities: [4294967295, 4294967295, 4294967295], UsedCount: 7 }])), Version: '1.00' },
  AirShipInfo: { AirShips: M(Array.from({ length: 15 }, (_, i) => [370000000 + i, { UnlockedFlag: i !== 3, CursorSelectedFlag: false }])), Version: '1.00' },
  MyMoogleInfo: { MyMoogles: M(Array.from({ length: 35 }, (_, i) => [360000000 + i, { UnlockedFlag: true, CursorSelectedFlag: false }])), Version: '1.00' },
  SummonStoneInfo: { TotalGetCount: 31, GetCountFromRivalProfica: 1, Version: '1.00', SummonStones: [] },
  ItemInfo: { Items: M(Array.from({ length: 4 }, (_, i) => [270000000 + i, { PossessionNumber: 1, ObtainedFlag: true, TotalUseCount: 30 }])), Version: '1.00' },
  CollecaInfo: { Collecas: M(Array.from({ length: 320 }, (_, i) => [300000000 + i, { ObtainedFlag: i % 16 !== 0, PossessionNumber: 1, CursorSelectedFlag: false }])), Version: '1.00' },
  Version: '1.00',
};
const music = { MusicInfo: { MusicRecordDictionary: M([160000005, 160000000, 160000001].map((id, n) => [id, { Unlocked: true, LookedMusicSelect: true,
  Playlist: { Playlist1SortOrder: -1, Playlist2SortOrder: -1, Playlist3SortOrder: -1 }, TotalMultiBattleSelectCount: 0,
  DifficultyRecordDictionary: M((n === 0 ? [0, 1, 2, 3] : [0, 1, 2]).map(d => [d, { PlayStyleRecordDictionary: M([0, 1, 2].map(s => [s, n === 0 && d === 1 && s === 0 ? playStyle(9900000, 11, 10, 6, 0, 3456) : playStyle(1000 * d, 15, 14, 4, 1, 100)])) }])) }])),
  Version: '1.00' }, Version: '1.00' };
const profica = { ProficaInfo: { TotalProficaCount: 1, GivenCount: 0, GivenCountWithSummonStone: 0,
  MyProfica: ['2f911e53-c980-40ff-8394-4047f5b8cf27', 150000031, 140000004, 2, -1, [160000005], false], RivalProficaList: [], Version: '1.00' }, Version: '1.00' };
const edge = M([
  ['str', 'héllo'], ['nul\u0000cut', 'a\u0000b'], ['long', 'x'.repeat(300)], ['bin', bin([0, 1, 0xab, 0xff])],
  ['f32', f32(0.1)], ['f64', 0.1], ['nan', f32(NaN)], ['inf', Infinity], ['u64', u64('18446744073709551615')], ['i64', i64(-9007199254740993)],
  ['neg', -5], ['negbig', -100000], [5, 'int key'], [-1, 'neg key'], [4294967295, 'max u32 key'], [u64('12345678901234567890'), 'u64 key'],
  [null, 'nil key'], [true, 'true key'], [f32(1.5), 'float key'], [['a'], 'array key'], [M([['k', 1]]), 'map key'], [bin([1]), 'bin key'], [ext(3, [1]), 'ext key'],
  ['dup', 1], ['ts4', ts4(1700000000)], ['ts8', ts8(1700000001, 999)], ['ts12', ts12(-62135596800)], ['ext', ext(7, [0xde, 0xad])], ['dup', 2],
  ['__proto__', { polluted: true }], ['10', 'ten'], ['2', 'two'], ['01', 'leading zero'], ['nested', [[[[1]]]]], ['empty', M([])], ['emptyList', []],
]);

const out = process.argv[2] || path.join(__dirname, 'fixtures', 'theatrhythm');
fs.mkdirSync(out, { recursive: true });
const big = (o) => { const b = enc(o); return b; };
fs.writeFileSync(path.join(out, 'backup.inf'), encrypt(big(backup), 9, false));
fs.writeFileSync(path.join(out, 'musicBackup.inf'), encrypt(big(music), 1, true));
fs.writeFileSync(path.join(out, 'proficaBackup.inf'), encrypt(big(profica), 0, false));
fs.writeFileSync(path.join(out, 'edge.inf'), encrypt(big(edge), 6, false));
fs.writeFileSync(path.join(out, 'password.txt'), PASSWORD + '\n');
console.log('fixtures written to', out);
