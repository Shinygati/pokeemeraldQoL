from pathlib import Path
from PIL import Image, ImageDraw
import struct,json,re
# Requires Pillow only when regenerating assets; the normal ROM build uses
# the checked-in PNGs and generated C table directly. Edit the day originals
# in graphics/day_night/sources, then run this script from the project root.
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/day_night_lights_audit';OUT.mkdir(parents=True,exist_ok=True)
def original(p):
 if p.name=='tiles.png':
  return ROOT/'graphics/day_night/sources'/(p.parent.name+'.png')
 if p.name=='metatiles.bin' and p.parent.name=='mauville':
  return ROOT/'graphics/day_night/sources/mauville_metatiles.bin'
 return p
def ids(s):
 out=[]
 for term in s.split():
  a=term.split('-');out.extend(range(int(a[0],16),int(a[-1],16)+1))
 return out
# Exterior glass/bulb metatiles, audited against the original tilesheets.
SPECS={
 'petalburg':('20F 217 23A 26A-26B 27D 287 28F',''),
 'rustboro':('23A 242 268 26B-26C 274-275 278-27C 2D0-2D2','212-213'),
 'slateport':('250-251 253-254 268 26C 290-293 2D0 2D2 2D6 2D8 2E6 310-313 317-31B 331 334 33A 362-363',''),
 'mauville':('23E-23F 260 269 26F 271 273-274 278 27C-27D 2A6 2A8-2AA 2E8 2EF 2F3-2F4 304-307 30A-30C 310-312 315 317 319 31C 31E 322-323 326 329 32B 32D-32E 330-333 335 337-339 33C 340-342 344-345 349 350-351 354-355 35D-35E 361-362 364-366 369 372 376-377 379 38A-38B 39B 3A0 3A2 3AA 3B7-3B8 3C8 3D0-3D1 3E8 3EF 3F0 3F2-3F3 3F9-3FB','2F9 2FB'),
 'lilycove':('200-201 204-205 20D-20F 211 214 217 225-226 22D-22F 234-236 23D-23E 245-247 249 24B-24D 253-255 257 25B-25F 264-266 271-272 284-285 28D 293-295 2A1-2A2 2A4 2A6 2A9-2AD 2B1 2B3 2B5 2B8 2D3-2D4 2E4 2EB 2F2-2F3 2F9 300-301 303 306 309-30B 30E 313 316 323 32B-32D 32F 334 338 342 348-349 34B 34D-34E 354 35B-35C','240 29A 2A0'),
 'dewford':('20B-20D 211-218 21D-21F 224 234-235 237 249 250 252 258 25C-25E 260 269-26B 271-272 279 27B 280-281 287 290-292 2A0 2A2 2AB 2AD-2AE 2B4 2B6 2B8 2C0-2C2 2C4 2C8 2CA-2CD 2D0-2D2 2D4-2D6 2D8 2E0-2E2 2E8-2EB 2ED 2F9-2FA 2FC',''),
 'fallarbor':('29E 2A5 2A9-2AA 2AB-2AD 2BB-2BC 2FF 307 34C 353-354 35C 35F 361-362 36C',''),
 'fortree':('2BB-2BC 2BF 2C1 2C6 2C9 2D0 2D8 2F2-2F3 300',''),
 'lavaridge':('238-23A 2A0-2A3 2A9 2AF 2FA 2FF 323 332-333 335 337 33A-33B 33D 33F 350-353 358 35F 365-366 369-36A 36C 36F-371 373-377 379-37B 37E-37F 387 389-38A 38C 38E-38F 395-396 39A',''),
 'mossdeep':('292 296 29A 29E 2CE-2CF 2DC 2E4-2E5 2EB-2ED 2B8-2BA 2BC-2BE 39F 3BE 3C0-3C4',''),
 'pacifidlog':('',''),
 'sootopolis':('21A 21D 23C 246 250 274-275',''),
 'ever_grande':('202-203 21B-21D 228',''),
 'battle_frontier_outside_west':('28A 29B 2A1 2A3 2CA-2CF 2D0-2D2 2D5 2D8 2DB 2DE-2DF 391-393 3A0 3A2 3F6','3F9'),
 'battle_frontier_outside_east':('2C5-2C6 2CE-2CF 2D5-2D6 2DC-2DD 2E1 2E6-2E7 2EB-2EC 2F2 2F5-2F6 2F8-2F9 300 30B-30C 30F 311-312 315 31B 321 32C 338 34E-34F 350-352 35D-35E 372 37D-37E 385-386 38E 3AC 3E0 3E5',''),
 'island_harbor':('218-219 21B-21D 21F-220 226-227 229 22B-22D 22F 237 23A 3C0-3C4 3C6-3CA 3CF-3D2 3D4-3D6',''),
}
COMMON=ids('00B 013 018 01A 020 022 1C0-1C4 1C5 1CD')
PALE={
 'slateport':ids('290-293'),
 'dewford':ids('20E-20F 216-217 2F9'),
 'fortree':ids('245 24D-24F 254-255'),
 'fallarbor':ids('27D-27F 284-287 28C-28D 2AC-2AE 307 322-324 342-345'),
 'pacifidlog':ids('211 213 219'),
 'sootopolis':ids('21A 21D 23C 246'),
}
EXTRA={
 'petalburg':ids('232 240-241 248'),
 'rustboro':ids('260 264-265 277 28F'),
 'lilycove':ids('269 278 296 2A5 2AE-2AF 2B7 2BF 2EE 2FD 324-325 326 333 335 337'),
 'slateport':ids('38C-38D 391-392'),
}
def pal(p):return [tuple(map(int,s.split())) for s in p.read_text().splitlines()[3:19]]
primary=ROOT/'data/tilesets/primary/general';pi=Image.open(primary/'tiles.png')
pb=struct.unpack('<4096H',(primary/'metatiles.bin').read_bytes())
def blue(c):
 r,g,b=c;return r<235 and g>=r+30 and b>=g+8 and b>=115
def warm(c):
 r,g,b=c;return r>=155 and g>=130 and b<=g-22 and -20<=r-g<=65
def light(c):
 r,g,b=c;return min(r,g,b)>=160 and max(r,g,b)-min(r,g,b)<70
def symbol(name):
 h=(ROOT/'src/data/tilesets/headers.h').read_text()
 m=re.search(r'const struct Tileset (\w+) =\n\{(?:(?!\n\};).)*?\.tiles = \w*'+re.escape('') ,h,re.S) if False else None
 # Names in this project match folder components, apart from EverGrande.
 return 'gTileset_'+''.join(x.title() for x in name.split('_'))
allsets=[];report={}
for name,(windows,lamps) in SPECS.items():
 d=ROOT/'data/tilesets/secondary'/name
 si=Image.open(original(d/'tiles.png'));data=original(d/'metatiles.bin').read_bytes()
 sb=struct.unpack('<'+'H'*(len(data)//2),data)
 palettes=[pal((primary if i<6 else d)/'palettes'/f'{i:02}.pal') for i in range(16)]
 # Reserve all existing metatile references and all secondary animation ranges.
 used={v&1023 for v in sb}
 anim=set(range(1008,1024)) # Runtime door animation workspace.
 anim.update(range(512+96,512+160) if name=='mauville' else [])
 anim.update(range(512+218,512+256) if name.startswith('battle_frontier') or name in ['ever_grande','slateport'] else [])
 anim.update(range(512+170,512+176) if name=='dewford' else [])
 anim.update(range(512+288,512+296) if name=='lavaridge' else [])
 anim.update(range(512+448,512+464) if name=='rustboro' else [])
 anim.update(range(512+128,512+160) if name=='rustboro' else [])
 anim.update(range(512+464,512+504) if name=='pacifidlog' else [])
 anim.update(range(512+240,512+336) if name=='sootopolis' else [])
 if name=='mauville':
  # Deduplicate identical static tile graphics to free space in this full set.
  # Animation destinations retain their original IDs and are never reused.
  canonical={};remap={}
  for tile in range(512,1024):
   if tile in anim:continue
   x=(tile-512)%16*8;y=(tile-512)//16*8
   px=list(si.crop((x,y,x+8,y+8)).getdata());key=bytes(px)
   if key in canonical:remap[tile]=canonical[key]
   else:
    remap[tile]=tile
    for flip in [0,0x400,0x800,0xC00]:
     variant=bytes(px[(7-y if flip&0x800 else y)*8+(7-x if flip&0x400 else x)] for y in range(8) for x in range(8))
     canonical.setdefault(variant,tile|flip)
  sb=tuple((v&~0x3ff)^remap.get(v&1023,v&1023) for v in sb)
  # Some static duplicates encode the same pixels with different palettes.
  canonical={};updated=[]
  for entry in sb:
   tile=entry&1023
   if tile<512 or tile in anim:
    updated.append(entry);continue
   x=(tile-512)%16*8;y=(tile-512)//16*8
   px=list(si.crop((x,y,x+8,y+8)).getdata())
   rgb=[(-1,-1,-1) if v==0 else palettes[entry>>12][v] for v in px]
   key=tuple(rgb)
   if key in canonical:updated.append((entry&0xC00)^canonical[key])
   else:
    updated.append(entry)
    for flip in [0,0x400,0x800,0xC00]:
     variant=tuple(rgb[(7-y if flip&0x800 else y)*8+(7-x if flip&0x400 else x)] for y in range(8) for x in range(8))
     canonical.setdefault(variant,(entry&~0xC00)|flip)
  sb=tuple(updated)
  (d/'metatiles.bin').write_bytes(struct.pack('<'+'H'*len(sb),*sb))
  used={v&1023 for v in sb}
 free=sorted(set(range(512,1024))-used-anim)
 def pixels(entry):
  t=entry&1023;im=pi if t<512 else si;t%=512
  x=t%(im.width//8)*8;y=t//(im.width//8)*8
  return list(im.crop((x,y,x+8,y+8)).getdata())
 specs={i:'window' for i in COMMON+ids(windows)}
 specs.update({i:'window' for i in EXTRA.get(name,[])})
 specs.update({i:'lamp' for i in ids(lamps)})
 specs.update({i:'pale' for i in PALE.get(name,[])})
 if name=='rustboro':specs.update({i:'window' for i in ids('2A0-2A2')})
 placed=set();walkable=set()
 for layout in json.loads((ROOT/'data/layouts/layouts.json').read_text())['layouts']:
  if layout['secondary_tileset']!=symbol(name) or layout['primary_tileset']!='gTileset_General':continue
  for field in ['blockdata_filepath','border_filepath']:
   if field not in layout:continue
   b=(ROOT/layout[field]).read_bytes()
   placed.update(v&1023 for v in struct.unpack('<'+'H'*(len(b)//2),b))
   walkable.update(v&1023 for v in struct.unpack('<'+'H'*(len(b)//2),b) if (v&0xC00)==0)
 specs={i:kind for i,kind in specs.items() if i in placed}
 # Physical wall-window templates confirmed on the town maps. Roof panels,
 # bridges, pools, doors and Contest Hall decoration are intentionally absent.
 confirmed={
  'fortree':[],
  'battle_frontier_outside_west':[],
  'battle_frontier_outside_east':[],
  'dewford':ids('226'),
  'pacifidlog':[],
  'slateport':ids('250-251 253-254 268-269 2BD 2BF'),
  'sootopolis':[],
  'ever_grande':[],
  'mossdeep':ids('29A-29B 2A2-2A3 2DD 2E5 3C0-3C1 3C4-3C5'),
  'lavaridge':[],
  'fallarbor':ids('29E 2A6 307'),
  'mauville':ids('26F 277 2A6 38B 393 2DB 2E9 2EB 2E3 2D2 2DA 2F9 2FB'),
  'lilycove':ids('23F 24D 247 255 2ED-2EE 34B 34E 2AD 2B7'),
 }
 if name in confirmed:
  specs={i:kind for i,kind in specs.items() if i<512}
  specs.update({i:'window' for i in confirmed[name] if i in placed})
  specs.update({i:'lamp' for i in ids(lamps) if i in placed})
 # Audit repeated glass glyphs even when towns assemble them into different
 # metatiles. Glyph identity and a pane-only mask preserve all surrounding art.
 for meta in placed:
  entries=pb[meta*8:meta*8+8] if meta<512 else sb[(meta-512)*8:(meta-512)*8+8]
  if any((e&1023) in [0xA6,0xB6,0x196,0x197] and e>>12==5 for e in entries):specs[meta]='window'
  if name=='lilycove' and any((e&1023) in [0x213,0x223,0x24A,0x24B,0x25A,0x25B,0x35B,0x36B] for e in entries):specs[meta]='window'
 tent_metas={
  'slateport':ids('371-375 379-37D 381-385 389-38D 391-395'),
  'fallarbor':ids('34A-34E 352-356 35A-35E 362-366 36A-36E'),
  'mauville':ids('3B2-3B6 3BA-3BE 3C2-3C6 3CA-3CE 3D2-3D6'),
 }
 for meta in placed:
  entries=pb[meta*8:meta*8+8] if meta<512 else sb[(meta-512)*8:(meta-512)*8+8]
  if name=='lilycove' and any(e>>12==5 and (e&1023) in [0x2ED,0x2EE] for e in entries):specs[meta]='window'
  if name=='lilycove' and any(e>>12==8 and (e&1023) in [0x296,0x299] for e in entries):specs[meta]='window'
  if name=='lilycove' and any(e>>12==1 and (e&1023)==0x372 for e in entries):specs[meta]='contest_triangles'
  if name=='lilycove' and any(e>>12==1 and (e&1023) in [0x227,0x228] for e in entries):specs[meta]='window'
  if name=='ever_grande' and any(e>>12==8 and (e&1023) in [0x304,0x305,0x314,0x315] for e in entries):specs[meta]='window'
  if name=='sootopolis' and any(e>>12==6 and (e&1023) in [0x267,0x268,0x269,0x26A,0x277,0x278,0x279,0x27A] for e in entries):specs[meta]='window'
 specs.update({i:'window' for i in COMMON})
 # These exact source tiles contain the Center/Mart Poke Ball faces and
 # the gym sign emblem. Include both buildings for connected map edges.
 for meta in set(placed)|set(range(512)):
  entries=pb[meta*8:meta*8+8] if meta<512 else sb[(meta-512)*8:(meta-512)*8+8]
  if any((e&1023) in [0x134,0x144] and e>>12 in [0,1] for e in entries):
   specs[meta]='emblem'
 # Tent-shaped halls remain entirely unlit, including their central emblem.
 specs={meta:kind for meta,kind in specs.items() if meta not in tent_metas.get(name,[])}
 groundKeys=set()
 for meta in walkable:
  entries=pb[meta*8:meta*8+8] if meta<512 else sb[(meta-512)*8:(meta-512)*8+8]
  if all(not any(pixels(e)) for e in entries[4:]):
   groundKeys.update(e&~0xC00 for e in entries[:4])
 trimKeys={0x5174}
 if name=='petalburg':
  for meta in ids('208-20A 210-212 218-21A 22C-22D'):
   trimKeys.update(e&~0xC00 for e in sb[(meta-512)*8:(meta-512)*8+8] if e&1023)
 candidates=[];colors=[]
 for meta,kind in sorted(specs.items()):
  entries=pb[meta*8:meta*8+8] if meta<512 else sb[(meta-512)*8:(meta-512)*8+8]
  for slot,e in enumerate(entries):
   if kind=='lamp' and slot<4:continue
   # Center/Mart doors are unlit; the blue Mart roof is painted roofing.
   if (e&1023) in [0xCA,0xDA,0xEA,0xD8,0xE8,0x110,0x111,0x120,0x121,0x130,0x131,0x133]:continue
   if meta>=512 and (e&~0xC00) in groundKeys and not (e>>12==5 and (e&1023) in [0xA6,0xB6,0x196,0x197]):continue
   if (e&~0xC00) in trimKeys:continue
   px=pixels(e);pn=e>>12
   # Remapping the pane keeps shared grass, roofs, paths and water unchanged.
   mask=[v!=0 and (blue(palettes[pn][v]) or (kind in ['lamp','pale'] and light(palettes[pn][v])) or (kind=='pale' and warm(palettes[pn][v])) or (meta in ids('00B 013 018 01A 020 022') and warm(palettes[pn][v]))) for v in px]
   if meta<512:
    tile=e&1023
    if meta in ids('00B 013 018 01A 020 022'):
     mask=[pn==5 and tile in [0xA6,0xB6,0xE2,0xE3,0xE4] and v in [2,3,4,9,10] for v in px]
    elif meta in ids('028-029 02B 030-033'):
     mask=[tile in [0x110,0x111,0x120,0x121,0x130,0x131] and blue(palettes[pn][v]) and v!=0 for v in px]
    elif meta in [0x41,0x61]:
     mask=[tile in [0xD8,0xE8] and blue(palettes[pn][v]) and v!=0 for v in px]
    else:
     mask=[pn==5 and (e&1023) in [0x196,0x197] and 2<=i//8<=4 and ((e&1023)==0x196 and i%8>=3 or (e&1023)==0x197 and i%8<=4) for i,v in enumerate(px)]
   if (e&1023) in [0xA6,0xB6] and pn==5:
    mask=[v in [2,3,4] for v in px]
   if meta>=512 and name=='petalburg' and pn in [6,8,9]:
    mask=[v in [9,10] for v in px]
   if meta>=512 and name=='rustboro' and pn in [9,10,11]:
    mask=[v in [9,10] for v in px]
   if kind=='pale' and name=='fortree':
    mask=[pn==9 and v in [11,12,13] for v in px]
   if kind=='pale' and name=='sootopolis':
    mask=[pn==6 and v in [11,12] for v in px]
   if kind=='pale' and name=='slateport':
    mask=[pn==6 and v in [2,3,4,11] for v in px]
   if kind=='pale' and name=='pacifidlog':
    mask=[pn==9 and v in [6,7] for v in px]
   if kind=='lamp' and name=='rustboro':
    # Only the enclosed bulb (lamp rows 8-10). Four highlights on the cap,
    # outer rim and pole are metal, and must remain unlit.
    mask=[slot>=6 and i//8<=2 and v in [2,4] for i,v in enumerate(px)]
   if kind=='emblem':
    tile=e&1023
    if tile in [0x134,0x144] and pn in [0,1]:
     mask=[v in [2,3] for v in px]
    elif tile in [0xCE,0xCF,0xDE,0xDF] and pn==1:
     # The gray Poke Ball stroke, excluding the pale backing and GYM text.
     mask=[3<=i%8+(8 if tile in [0xCF,0xDF] else 0)<=13 and 4<=i//8+(8 if tile in [0xDE,0xDF] else 0)<=14 and v in [3,6] for i,v in enumerate(px)]
    else:mask=[False]*64
   elif meta>=512 and name=='dewford':
    mask=[pn==5 and (e&1023)==0x265 and i//8<=3 and 2<=i%8<=6 for i,v in enumerate(px)]
   elif meta>=512 and name=='sootopolis':
    mask=[pn==6 and (((e&1023) in [0x269,0x26A,0x279,0x27A] and v in [5,13]) or ((e&1023) in [0x267,0x268,0x277,0x278] and v in [12,13])) for v in px]
   elif meta>=512 and name=='mossdeep':
    tile=e&1023
    if pn==9:mask=[tile in [0x384,0x394] and v==7 for v in px]
    elif pn==8:
     mask=[((tile in [0x286,0x287,0x296,0x297] and v in [9,10]) or (tile in [0x289,0x28A] and i//8>=6 and v in [1,9,10]) or (tile==0x2D6 and 1<=i//8<=6 and 1<=i%8<=6)) for i,v in enumerate(px)]
    else:mask=[False]*64
   elif meta>=512 and name=='slateport' and (e&1023) in [0x31B,0x32B]:
    mask=[pn==1 and v in [1,2,9,10] for v in px]
   elif meta>=512 and name=='fallarbor':
    mask=[pn==7 and v in [9,10] for v in px]
   elif meta>=512 and name=='mauville':
    tile=e&1023
    if meta==0x38B:mask=[pn==5 and v in [3,4,9] for v in px]
    elif meta==0x393:mask=[pn==5 and slot in [4,5] and v in [3,4] for v in px]
    elif meta in [0x26F,0x277,0x2A6]:mask=[pn==7 and v in [1,2,15] for v in px]
    elif kind!='lamp':
     mask=[pn==10 and ((tile==0x315 and i//8>=2 and v in [1,2]) or (tile==0x325 and v in [2,3]) or (tile==0x326 and i//8<=3 and v in [2,3])) for i,v in enumerate(px)]
   elif meta>=512 and name=='lilycove':
    tile=e&1023
    if tile in [0x227,0x228] and pn==1:mask=[2<=i//8<=5 and (i%8>=4 if tile==0x227 else i%8<=3) for i,v in enumerate(px)]
    elif tile==0x296 and pn==8:mask=[4<=i%8<=6 and i//8 in [0,2,3,5,6] for i,v in enumerate(px)]
    elif tile==0x299 and pn==8:mask=[3<=i%8<=6 and (i//8==1 or i//8>=3) for i,v in enumerate(px)]
    elif tile==0x2ED and pn==5:mask=[i//8>=6 and 1<=i%8<=6 for i,v in enumerate(px)]
    elif tile==0x2EE and pn==5:mask=[i//8<=3 and 1<=i%8<=6 for i,v in enumerate(px)]
    elif tile==0x213:mask=[pn==8 and i//8>=5 and 3<=i%8<=6 for i,v in enumerate(px)]
    elif tile==0x223:mask=[pn==8 and 1<=i//8<=4 and 3<=i%8<=6 for i,v in enumerate(px)]
    elif tile in [0x24A,0x24B]:mask=[pn==5 and v in [6,9,10] for v in px]
    elif tile==0x35B:mask=[pn==1 and v in [9,10] for v in px]
    elif tile in [0x25A,0x25B]:mask=[pn==5 and (i%8>=4 if tile==0x25A else i%8<=3) and i//8<=3 and v in [1,2,3,9,10] for i,v in enumerate(px)]
    elif tile==0x36B:mask=[pn==1 and i%8>=4 and i//8<=3 and v in [1,2,3,9,10] for i,v in enumerate(px)]
    elif meta in [0x2AD,0x2B7]:mask=[pn==9 and v in [9,10] for v in px]
    else:mask=[False]*64
   if kind=='contest_triangles':mask=[pn==1 and (e&1023)==0x372 and v in [2,3,11,12] for v in px]
   elif name=='ever_grande' and meta>=512:
    mask=[pn==8 and (e&1023) in [0x304,0x305,0x314,0x315] and v in [9,10] for v in px]
   if name=='slateport' and kind=='window' and pn==10:
    tile=e&1023
    if tile in [0x3D1,0x3E1]:mask=[2<=i%8<=5 and (i//8>=2 if tile==0x3D1 else True) for i,v in enumerate(px)]
    elif tile in [0x3D0,0x3D4]:mask=[v==10 or (v==5 and i//8<7) for i,v in enumerate(px)]
   if kind!='emblem' and name=='petalburg' and (e&1023)==0x296 and pn==8:
    mask=[i//8<=3 and 2<=i%8<=6 for i,v in enumerate(px)]
   if kind!='emblem' and name=='rustboro' and pn==10:
    tile=e&1023
    if tile==0x245:mask=[i//8>=5 and 4<=i%8<=6 for i,v in enumerate(px)]
    elif tile==0x246:mask=[i//8>=5 and 1<=i%8<=3 for i,v in enumerate(px)]
    elif tile==0x255:mask=[i//8 in [0,2,3,4,5] and 4<=i%8<=6 for i,v in enumerate(px)]
    elif tile==0x256:mask=[i//8 in [0,2,3,4,5] and 1<=i%8<=3 for i,v in enumerate(px)]
   if kind!='emblem' and pn==5 and (e&1023) in [0xA6,0xB6]:mask=[v in [2,3,4] for v in px]
   if kind!='emblem' and pn==5 and (e&1023) in [0x196,0x197]:
    mask=[2<=i//8<=4 and (i%8>=3 if (e&1023)==0x196 else i%8<=4) for i,v in enumerate(px)]
   if sum(mask)<1:continue
   candidates.append((meta,slot,e,px,mask))
   colors.extend(palettes[pn][v] for v,m in zip(px,mask) if v and not m)
 assert candidates,name
 # Keep each patch tied to its audited metatile. Similar-looking tiles elsewhere
 # can be roof trim or wall decoration, rather than another window.
 # Preserve all frame pixels and their original palette indices exactly.
 usage=[set() for _ in range(16)]
 for entry in pb+sb:
  pn=entry>>12;usage[pn].update(v for v in pixels(entry) if v)
 animUsage={}
 def animatedIndices(folder):
  folder=str(folder)
  if folder not in animUsage:
   values=set()
   for file in Path(folder).rglob('*.png'):
    values.update(Image.open(file).getdata())
   animUsage[folder]=values-{0}
  return animUsage[folder]
 for meta in placed:
  entries=pb[meta*8:meta*8+8] if meta<512 else sb[(meta-512)*8:(meta-512)*8+8]
  for entry in entries:
   pn=entry>>12;tile=entry&1023
   if 432<=tile<512:
    kind='flower' if tile>=508 else 'waterfall' if tile>=496 else 'land_water_edge' if tile>=480 else 'sand_water_edge' if tile>=464 else 'water'
    usage[pn].update(animatedIndices(primary/'anim'/kind))
   elif tile in anim and tile<1008:
    usage[pn].update(animatedIndices(d/'anim'))
 spare=[i for i in range(13,-1,-1) if not usage[i]]
 assignments=[];paletteMap={};frameMaps={};skipped=[];customSources={}
 groups={}
 for record in candidates:groups.setdefault(record[2]>>12,[]).append(record)
 for pn,records in sorted(groups.items()):
  freeIndices=sorted(set(range(1,16))-usage[pn],reverse=True)
  dest=pn
  if not freeIndices:
   frameIndices={v for _,_,e,px,mask in records for v,m in zip(px,mask) if v and not m}
   freeIndices=sorted(set(range(1,16))-frameIndices,reverse=True)
   if not spare or not freeIndices:
    # Share an existing light palette only when every frame color has an
    # exact RGB match. Never approximate a frame, roof or wall color.
    shared=False
    for base,destination,lit,shade,reference in assignments:
     available={color:i for i,color in enumerate(palettes[base]) if i not in [0,lit,shade]}
     if all(palettes[pn][v] in available for v in frameIndices):
      paletteMap[pn]=(destination,lit,shade)
      frameMaps[pn]={v:available[palettes[pn][v]] for v in frameIndices};frameMaps[pn][0]=0
      shared=True;break
    if not shared:skipped.append(pn)
    continue
   dest=spare.pop(0)
  lit=freeIndices[0];shade=freeIndices[1] if len(freeIndices)>1 else lit
  reference=next(pn*16+v for _,_,e,px,mask in records for v,m in zip(px,mask) if m)
  assignments.append((pn,dest,lit,shade,reference))
  paletteMap[pn]=(dest,lit,shade)
  frameMaps[pn]={v:v for v in range(16)}
 if skipped:
  aliasPns=[pn for pn,(dest,_,_) in paletteMap.items() if dest!=pn]+skipped
  destinations=sorted({dest for pn,(dest,_,_) in paletteMap.items() if dest!=pn},reverse=True)
  colorSets={pn:{palettes[pn][v] for _,_,_,px,mask in groups[pn] for v,m in zip(px,mask) if v and not m} for pn in aliasPns}
  bins=[]
  for pn in sorted(aliasPns,key=lambda pn:-len(colorSets[pn])):
   for members,colors in bins:
    if len(colors|colorSets[pn])<=13:
     members.append(pn);colors.update(colorSets[pn]);break
   else:bins.append(([pn],set(colorSets[pn])))
  if len(bins)<=len(destinations):
   assignments=[a for a in assignments if a[0]==a[1]]
   for destination,(members,colors) in zip(destinations,bins):
    ordered=sorted(colors);colorMap={color:i+1 for i,color in enumerate(ordered)}
    base=members[0]
    reference=next(base*16+v for _,_,_,px,mask in groups[base] for v,m in zip(px,mask) if m)
    sources=[0]*16
    for color,index in colorMap.items():
     sources[index]=next(pn*16+i for pn in members for i,c in enumerate(palettes[pn]) if c==color)
    sources[14]=sources[15]=reference;customSources[destination]=sources
    assignments.append((base,destination,15,14,reference))
    for pn in members:
     paletteMap[pn]=(destination,15,14)
     frameMaps[pn]={v:colorMap[palettes[pn][v]] for v in range(1,16) if palettes[pn][v] in colorMap};frameMaps[pn][0]=0
   skipped=[]
 candidates=[record for record in candidates if record[2]>>12 in paletteMap]
 source=assignments
 canvas=Image.new('P',(128,max(256,si.height)),0);canvas.putpalette(si.getpalette());canvas.paste(si,(0,0))
 patches=[];cache={};allocated=0
 for meta,slot,e,px,mask in candidates:
  pn=e>>12;dest,lit,shade=paletteMap[pn]
  indices=[(lit if name=='mauville' or sum(palettes[pn][v])>=500 else shade) if m else frameMaps[pn][v] for v,m in zip(px,mask)]
  base=next(base for base,destination,_,_,_ in assignments if destination==dest)
  destinationColors=[palettes[index//16][index%16] for index in customSources[dest]] if dest in customSources else palettes[base]
  assert all((v==0 and new==0) or palettes[pn][v]==destinationColors[new] for v,new,m in zip(px,indices,mask) if not m)
  key=bytes(indices)
  if key not in cache:
   assert allocated<len(free),(name,allocated,len(free))
   tile=free[allocated];allocated+=1
   for flip in [0,0x400,0x800,0xC00]:
    variant=bytes(indices[(7-y if flip&0x800 else y)*8+(7-x if flip&0x400 else x)] for y in range(8) for x in range(8))
    cache.setdefault(variant,tile|flip)
   x=(tile-512)%16*8;y=(tile-512)//16*8
   tiny=Image.new('P',(8,8));tiny.putdata(indices);canvas.paste(tiny,(x,y))
  patches.append((meta*8+slot,(dest<<12)|((e&0xC00)^cache[key])))
 for meta in placed:
  entries=pb[meta*8:meta*8+8] if meta<512 else sb[(meta-512)*8:(meta-512)*8+8]
  for slot,e in enumerate(entries):
   if e>>12==5 and (e&1023) in [0xA6,0xB6]:
    assert meta*8+slot in dict(patches),(name,hex(meta),'missing common window half')
 # Do not modify any original or animated tiles. Only unreferenced tile slots.
 for tile in used:
  if tile<512:continue
  x=(tile-512)%16*8;y=(tile-512)//16*8
  assert list(canvas.crop((x,y,x+8,y+8)).getdata())==list(si.crop((x,y,x+8,y+8)).getdata()),(name,tile)
 canvas.save(d/'tiles.png')
 ident=symbol(name)
 allsets.append((name,ident,source,patches,customSources))
 report[name]={'lit_metatiles':sorted(set(k//8 for k,v in patches)),'window_metatiles':sorted(set(meta for meta,slot,e,px,mask in candidates if specs[meta] in ['window','pale'])),'patches':len(patches),'new_tiles':allocated,'free_tiles':len(free),'skipped_palettes':skipped}
 print(name,len(patches),'entries,',allocated,'/',len(free),'spare tiles; unsupported full palettes:',skipped)
 # Render a compact audit sheet of every selected metatile before/after.
 patches.sort()
 metas=sorted(set(specs)|set(k//8 for k,v in patches));patchmap=dict(patches)
 sheet=Image.new('RGB',(16*96,((len(metas)+15)//16)*68),(25,28,40));draw=ImageDraw.Draw(sheet)
 nightpalettes=[[(r//2,g//2,b*11//16) for r,g,b in pal] for pal in palettes]
 for pn,dest,lit,shade,reference in assignments:
  if dest in customSources:nightpalettes[dest]=[(palettes[index//16][index%16][0]//2,palettes[index//16][index%16][1]//2,palettes[index//16][index%16][2]*11//16) for index in customSources[dest]]
  elif dest!=pn:nightpalettes[dest]=list(nightpalettes[pn])
  nightpalettes[dest][lit]=(222,198,115)
  if shade!=lit:nightpalettes[dest][shade]=(189,156,74)
 for pos,meta in enumerate(metas):
  entries=pb[meta*8:meta*8+8] if meta<512 else sb[(meta-512)*8:(meta-512)*8+8]
  for night in [False,True]:
   image=Image.new('RGB',(16,16),(20,30,55))
   for slot,e in enumerate(entries):
    replacement=patchmap.get(meta*8+slot) if night else None
    if replacement:
     t=(replacement&1023)-512;x=t%16*8;y=t//16*8;px=list(canvas.crop((x,y,x+8,y+8)).getdata());palette=nightpalettes[replacement>>12]
    else:
     px=pixels(e);palette=palettes[e>>12]
     if night:palette=[(r//2,g//2,b*11//16) for r,g,b in palette]
    for yy in range(8):
     for xx in range(8):
      flipentry=replacement if replacement else e
      v=px[(7-yy if flipentry&0x800 else yy)*8+(7-xx if flipentry&0x400 else xx)]
      if slot>=4 and v==0:continue
      image.putpixel(((slot%2)*8+xx,((slot%4)//2)*8+yy),palette[v])
   x=pos%16*96+int(night)*48;y=pos//16*68
   sheet.paste(image.resize((48,48),Image.Resampling.NEAREST),(x,y+16))
  draw.text((pos%16*96,pos//16*68),f'{meta:03X}',fill='white')
 sheet.save(OUT/(name+'.png'))
header=['// Generated by tools/generate_outdoor_lights.py. Exterior glass and bulbs only.']
for name,ident,source,patches,customSources in allsets:
 header.append(f'extern const struct Tileset {ident};')
 for dest,indices in customSources.items():header.append(f'static const u16 sLightColors_{name}_{dest}[16] = {{'+','.join(str(i) for i in indices)+'};')
 header.append(f'static const struct OutdoorLightPalette sLightPalettes_{name}[] = {{')
 header.extend('    {'+','.join(str(v) for v in assignment)+','+(f'sLightColors_{name}_{assignment[1]}' if assignment[1] in customSources else 'NULL')+'},' for assignment in source);header.append('};')
 header.append(f'static const struct OutdoorLightPatch sLightPatches_{name}[] = {{')
 header.extend(f'    {{{k}, 0x{v:04X}}},' for k,v in patches);header.append('};')
header.append('static const struct OutdoorLightSet sOutdoorLightSets[] = {')
for name,ident,source,patches,customSources in allsets:header.append(f'    {{&{ident}, sLightPalettes_{name}, sLightPatches_{name}, ARRAY_COUNT(sLightPatches_{name}), ARRAY_COUNT(sLightPalettes_{name})}},')
header.append('};')
(ROOT/'src/data/outdoor_lights.h').write_text('\n'.join(header)+'\n')
(OUT/'coverage.json').write_text(json.dumps(report,indent=2))
