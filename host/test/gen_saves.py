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
# Hollow Knight: JSON
pd={'hasCharm':True,'charmsOwned':25,'grubsCollected':30,'killedZote':True,'geo':5000,'completionPercentage':80}
for i in range(1,6): pd[f'bossDoorStateTier{i}']={'completed': i<3}
w('0100633007D48000','user1.dat',json.dumps({'playerData':pd,'sceneData':{'x':[1]*5000}}).encode())
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
for f in ('backup.inf','musicBackup.inf'): w('010081B01777C000',f,open(os.path.join(fx,f),'rb').read())
os.makedirs(f'{R}/keys', exist_ok=True); open(f'{R}/keys/theatrhythm.txt','wb').write(open(os.path.join(fx,'password.txt'),'rb').read())
