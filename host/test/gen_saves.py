# Synthetic saves for the host build (compare full vs. streamed evaluation)
import json, os, random, struct, sys, zlib
R = sys.argv[1]; G = sys.argv[2]
random.seed(7)
def mm3(s):
    d=s.encode(); h=0; c1=0xcc9e2d51; c2=0x1b873593; n=len(d)//4
    rotl=lambda x,r: ((x<<r)|(x>>(32-r)))&0xffffffff
    for i in range(n):
        k=struct.unpack_from('<I',d,i*4)[0]; k=(k*c1)&0xffffffff; k=rotl(k,15); k=(k*c2)&0xffffffff
        h^=k; h=rotl(h,13); h=(h*5+0xe6546b64)&0xffffffff
    t=d[n*4:]; k=0
    if len(t)==3: k^=t[2]<<16
    if len(t)>=2: k^=t[1]<<8
    if len(t)>=1: k^=t[0]; k=(k*c1)&0xffffffff; k=rotl(k,15); k=(k*c2)&0xffffffff; h^=k
    h^=len(d); h^=h>>16; h=(h*0x85ebca6b)&0xffffffff; h^=h>>13; h=(h*0xc2b2ae35)&0xffffffff; h^=h>>16
    return h
crc=lambda s: zlib.crc32(s.encode())&0xffffffff
def names(rule, fn):
    out=[]
    for k in ('name','hash'):
        if k in rule: out.append(int(rule[k],16) if rule[k].startswith('0x') else fn(rule[k]))
    for n in rule.get('names',[]): out.append(int(n,16) if n.startswith('0x') else fn(n))
    if 'pattern' in rule:
        for i in range(rule['from'], rule['to']+1): out.append(fn(rule['pattern'] % i))
    return out
def d(t): return json.load(open(f'{G}/{t}.json'))
def w(t, rel, data):
    p=f'{R}/saves/{t}/{rel}'; os.makedirs(os.path.dirname(p), exist_ok=True); open(p,'wb').write(data)
# BotW: 2 slots, ~1 MB with noise
bd=d('01007EF00011E000'); hs=sorted({h for a in bd['achievements'] if 'rule' in a for h in names(a['rule'],crc)})
for slot,p in ((0,0.3),(1,0.7)):
    pairs=[(random.getrandbits(32), random.getrandbits(8)) for _ in range(128000)]
    for h in hs: pairs.insert(random.randrange(len(pairs)), (h, random.randrange(0,300) if random.random()<p else 0))
    w('01007EF00011E000', f'{slot}/game_data.sav', struct.pack('<III',0x47E5,0xFFFFFFFF,0)+b''.join(struct.pack('<II',a,b) for a,b in pairs))
# TotK
td=d('0100F2C0115B6000'); hs=sorted({h for a in td['achievements'] for h in names(a['rule'],mm3)})
clear=mm3('Clear')
for slot,p in ((0,0.2),(1,0.6)):
    pairs=[(random.getrandbits(32), random.getrandbits(16)) for _ in range(90000)]
    for h in hs: pairs.insert(random.randrange(len(pairs)), (h, clear if random.random()<p else random.randrange(0,500)))
    pairs.append((0xa3db7114, 1)); pairs += [(random.getrandbits(32),0)]*100
    w('0100F2C0115B6000', f'slot_0{slot}/progress.sav', struct.pack('<I',0x01020304)+b'\0'*0x24+b''.join(struct.pack('<II',a,b) for a,b in pairs))
# Smash
sd=d('01006A800016E000'); buf=bytearray(5982968)
for a in sd['achievements']:
    r=a['rule']
    for o in r.get('offsets',[])+([r['offset']] if 'offset' in r else []):
        o=int(o,0) if isinstance(o,str) else o
        if random.random()<0.6:
            v=1 if r.get('max')==1 else random.randrange(0,int(r.get('target',1))*2+1)
            buf[o:o+r.get('size',4)]=v.to_bytes(4,'little')[:r.get('size',4)]
w('01006A800016E000','save_data/system_data.bin',bytes(buf))
# MK8D
md=d('0100152000022000'); buf=bytearray(80736); buf[0:4]=b'SUTC'
for a in md['achievements']:
    r=a['rule']
    for o in r.get('offsets',[])+([r['offset']] if 'offset' in r else []):
        o=int(o,0) if isinstance(o,str) else o
        buf[o:o+r.get('size',4)]=random.randrange(0,int(r.get('target',1))*2+1).to_bytes(8,'little')[:r.get('size',4)]
w('0100152000022000','userdata.dat',bytes(buf))
# KINGDOM HEARTS HD 1.5+2.5 ReMIX: "slots" (one PNG per game, many save slots in it) and per-rule "file"; slot 0 of
# each file is a valid save, the next one is invalid (wrong magic / check 0), then valid ones with random values
kd=d('010090C022978000')
for f,L in kd['slots'].items():
    base,stride=int(L['base'],0),int(L['stride'],0); n=5
    buf=bytearray(base+stride*n)
    for i in range(n):
        st=base+i*stride; ok=i!=1
        if L.get('magic') and ok: buf[st:st+len(L['magic'])]=L['magic'].encode()
        if 'check' in L and ok: buf[st+int(L['check']['offset'],0)]=random.randrange(1,200)
        for a in kd['achievements']:
            r=a['rule']
            if r.get('file')!=f: continue
            offs=r.get('offsets',[])+([r['offset']] if 'offset' in r else [])
            for o in offs:
                o=int(o,0); sz=r.get('size',4)
                if random.random()<0.5: buf[st+o:st+o+sz]=random.randrange(0,int(r.get('target',1))*2+2).to_bytes(4,'little')[:sz]
    w('010090C022978000',f,bytes(buf))
# Hollow Knight: JSON
pd={'hasCharm':True,'charmsOwned':25,'grubsCollected':30,'killedZote':True,'geo':5000,'completionPercentage':80}
for i in range(1,6): pd[f'bossDoorStateTier{i}']={'completed': i<3}
# ~2 MB, with the rule values between big unrelated data (escapes, unicode, numbers in every form): read in pieces
scene={f'Scene_{i}':{'persistentBoolItems':[{'id':f'Bat\u00e9 \"{j}\"','activated':random.random()<0.5,'v':random.uniform(-1e6,1e6)} for j in range(12)],'name':'Ω\n\t\\/'*3} for i in range(1800)}
w('0100633007D48000','user1.dat',('\ufeff'+json.dumps({'sceneData':scene,'playerData':pd,'tail':[1e-7,-0.0,12345678901234,True,None]*2000},ensure_ascii=False)+'\0\0').encode())
w('0100633007D48000','user2.dat',json.dumps({'playerData':{'hasCharm':True,'charmsOwned':40}}).encode())
# SIGNALIS: XOR JSON
key=b'ALL WATCHED OVER BY MACHINES OF LOVING GRACE'
bl=[{'Key':k,'Value':random.random()<0.5} for k in 'pen det med res lab mem end die gun rfl bad hrd all'.split()]
bl=[{'Key':'ach_'+x['Key'],'Value':x['Value']} for x in bl]
txt=json.dumps({'StringData':[],'IntData':[],'FloatData':[],'BoolData':bl}).encode()
w('0100307018934000','Save/01/lstr.replika',bytes(c^key[i%len(key)] for i,c in enumerate(txt)))
# DELTARUNE: the synthetic deltarune.sav of tools/deltarune/make_test_save.py
import importlib.util
spec=importlib.util.spec_from_file_location('drsave', os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..','tools','deltarune','make_test_save.py'))
drsave=importlib.util.module_from_spec(spec); spec.loader.exec_module(drsave)
w('0100A0D022A68000','Deltarune/deltarune.sav',drsave.encode(drsave.save_partial()))
# THEATRHYTHM FINAL BAR LINE: the encrypted fixtures of the Save Lab (DUMMY password -> <root>/keys/theatrhythm.txt)
fx=os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..','tools','savelab','test','fixtures','theatrhythm')
for f in ('backup.inf','musicBackup.inf','proficaBackup.inf'): w('010081B01777C000',f,open(os.path.join(fx,f),'rb').read())
os.makedirs(f'{R}/keys', exist_ok=True); open(f'{R}/keys/theatrhythm.txt','wb').write(open(os.path.join(fx,'password.txt'),'rb').read())
# Super Mario Odyssey: BYML v3 LE after a 16-byte header, padded to the real 2,097,164 bytes; noise so the
# rule paths are spread over the whole file (the live module reads it in pages)
def byml(root):
    keys, strs = set(), set()
    def scan(v):
        if isinstance(v, dict):
            for k, x in v.items(): keys.add(k); scan(x)
        elif isinstance(v, list):
            for x in v: scan(x)
        elif isinstance(v, str): strs.add(v)
    scan(root); keys = sorted(keys); strs = sorted(strs)
    ki = {k: i for i, k in enumerate(keys)}; si = {s: i for i, s in enumerate(strs)}
    out = bytearray(16)
    def align():
        while len(out) % 4: out.append(0)
    def table(items):
        align(); o = len(out); enc = [s.encode() + b'\0' for s in items]
        offs = []; pos = 4 + 4 * (len(items) + 1)
        for e in enc: offs.append(pos); pos += len(e)
        offs.append(pos)
        out.extend(bytes([0xC2]) + len(items).to_bytes(3, 'little') + b''.join(struct.pack('<I', x) for x in offs) + b''.join(enc))
        return o
    def tv(v):
        if isinstance(v, bool): return 0xD0, int(v)
        if isinstance(v, int): return 0xD1, v & 0xffffffff
        if isinstance(v, str): return 0xA0, si[v]
        return (0xC1 if isinstance(v, dict) else 0xC0), None
    def node(v):
        align(); o = len(out); later = []
        if isinstance(v, dict):
            items = sorted(v.items(), key=lambda kv: ki[kv[0]])
            out.extend(bytes([0xC1]) + len(items).to_bytes(3, 'little'))
            for k, x in items:
                t, raw = tv(x); out.extend(ki[k].to_bytes(3, 'little') + bytes([t]))
                if raw is None: later.append((len(out), x)); out.extend(b'\0\0\0\0')
                else: out.extend(struct.pack('<I', raw))
        else:
            out.extend(bytes([0xC0]) + len(v).to_bytes(3, 'little'))
            tvs = [tv(x) for x in v]; out.extend(bytes(t for t, _ in tvs)); align()
            for x, (t, raw) in zip(v, tvs):
                if raw is None: later.append((len(out), x)); out.extend(b'\0\0\0\0')
                else: out.extend(struct.pack('<I', raw))
        for pos, x in later: struct.pack_into('<I', out, pos, node(x))
        return o
    ko = table(keys); so = table(strs) if strs else 0; ro = node(root)
    out[0:16] = b'YB' + struct.pack('<H', 3) + struct.pack('<III', ko, so, ro)
    return bytes(out)
def smo(level):
    rnd = lambda p: random.random() < p
    names_ = lambda pre, n, p: [f'{pre}{i:03d}' for i in range(n) if rnd(p)]
    return {
        'Noise': [{'A': random.getrandbits(31), 'B': rnd(0.5), 'Name': f'n{random.getrandbits(12)}'} for _ in range(int(20000 * level) + 14000)],
        'ShineData': [{'IsGet': rnd(level), 'UniqueId': i, 'Pad': [random.getrandbits(20) for _ in range(8)]} for i in range(880)],
        'GameProgressData': {'IsFirstTimeWorld': [rnd(level) for _ in range(17)], 'UnlockWorldNum': 9},
        'WorldInfo': [{'ScenarioNo': random.randrange(0, 4)} for _ in range(17)],
        'HackDictionary': names_('Hack', 52, level), 'GetCoinCollectData': [{'IsGet': rnd(level)} for _ in range(900)],
        'TotalCoinNum': random.randrange(0, 20000), 'ItemCloth': names_('Cloth', 44, level), 'ItemCap': names_('Cap', 42, level),
        'ItemGift': names_('Gift', 30, level), 'ItemSticker': names_('Sticker', 17, level),
        'Checkpoint': [{'ObjId': f'obj{i}'} for i in range(int(110 * level))],
        'CollectBgmList': [{'IsCollected': rnd(level)} for _ in range(60)], 'PlayerJumpCount': random.randrange(0, 20000),
        'PlayerThrowCapCount': random.randrange(0, 20000), 'IsRideSphinx': rnd(level), 'IsRideMotorcycle': rnd(level),
        'JumpingRopeBestCount': random.randrange(0, 150), 'VolleyballBestCount': random.randrange(0, 150),
        'AchievementSaveData': [{'IsGet': rnd(level), 'Name': f'Ach{i}'} for i in range(60)],
        'ZNoise': [{'A': random.getrandbits(31), 'C': [random.getrandbits(16) for _ in range(6)]} for _ in range(9000)],
    }
for slot, level in ((0, 0.35), (1, 0.8)):
    doc = byml(smo(level)); assert len(doc) + 16 <= 2097164, len(doc)
    w('0100000000010000', f'File{slot}.bin', (struct.pack('<IIII', 0x10, len(doc), 0, 0) + doc).ljust(2097164, b'\0'))

# Every other tree reader, with the Save Lab's fixtures: generic rules over the whole tree ([*] = every value), so the
# test checks that each reader gives the app's values in the module whatever its memory (definitions in <root>/defs)
fxr=os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..','tools','savelab','test','fixtures')
os.makedirs(f'{R}/defs', exist_ok=True)
# Poppy Playtime Chapter 2 (Unreal 5.4, "requires" rules): the reporter's last save, Checkpoint 36 at 2448 s, has died
w('0100D3801E6CE000','Chap2Checkpoint',open(os.path.join(fxr,'gvas','ue54_checkpoint.sav'),'rb').read())
for n,(reader,files) in enumerate([('gvas',['gvas/Slot1.sav','gvas/Options.sav']),('nrbf',['nrbf/save0.dat','nrbf/save1.dat']),
        ('rpgmaker',['rpgmaker/file1.rpgsave','rpgmaker/file1.rmmzsave']),('gamemaker',['gamemaker/save.ini','gamemaker/save_b64.sav','gamemaker/file0'])]):
    tid=f'0F000000000{n+1:02d}000'
    names_=[]
    for f in files:
        rel=os.path.basename(f); names_.append(rel); w(tid,rel,open(os.path.join(fxr,f),'rb').read())
    achs=[]
    for d_,path in enumerate(['[*]','[*]/[*]','[*]/[*]/[*]','[*]/[*]/[*]/[*]']):
        achs.append({'id':f'c{d_}','name':f'count {d_}','desc':'x','points':5,'rule':{'type':'count','path':path,'target':1}})
        achs.append({'id':f's{d_}','name':f'sum {d_}','desc':'x','points':5,'rule':{'type':'sum','path':path,'target':1}})
    json.dump({'title_id':tid,'name':f'Fixture {reader}','reader':reader,'save_files':names_,'achievements':achs},open(f'{R}/defs/{tid}.json','w'))
