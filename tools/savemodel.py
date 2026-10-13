# savemodel.py - reference reader for a CONTROL Resonant save's global chunk ("...-persi-global"), the layout the
# StatsPage mod reads in C++ (save.h). Everything is little endian; ids are FNV-1a 32 of the lower-cased name.
#   python savemodel.py <persi-global> ...        prints a summary per file
#   python savemodel.py --all                     every developer save and the local copies: which ones parse
#
# File: "RMDB", 3 x u32, (crc32 of the bytes from +16 at +12), 8 bytes, then at +24:
#   u32 n, n x { u64 key, f64 value }           number facts
#   u32 n, n x { u64 key, u8 value }            flag facts
#   u32 world level (the Resonant Cycle)
#   unlock graphs:  u32 1, u32 graphs, per graph { u32 1, u32 id, u32 a, a x u32, u32 n, n x u32 node }
#   ability loadout: u32 1, u32 n, n x { u32 slot, u32 ability }
#   districts:      u32 1, u32 n, per district { u32 3, u32 a, a x u32, u32 offers, offers x u32,
#                   u32 rewards, rewards x { u32 0, u32 id }, u32 upgrades, upgrades x { u32 0, u32 id },
#                   u32 level, u32 xp, u32 district id, u32 level }
#   outfit:         u32 1, u32 n, n x { u32 3, u32 slot, u32 item, u32 colour, u32 -1 }
#   narrative:      u32 3, u32 a, a x ?, u32 b, b x ?, u32 n, n x { u32 1, u32 0, u32 id, u32 0 }
#   ... shops, Artifacts (not read) ...
#   rewards:        u32 3, u32 decks, per deck { u32 1, u32 cards, cards x { u32 1, u32 0, u32 card, u32 used },
#                   u32 0, u32 reward id, u32 0, u32 deck id, u32 0, u32 category, u32 empty cards used },
#                   u32 given, per entry { u32 1, u32 n, n x { u32 0, u32 record }, u32 0, u32 reward id, u32 0, u32 category }
# The reward section is found by its shape and its ids (the shops before it hold single bytes, so it is not word
# aligned): every reward id and category in it must be known.
import glob, os, struct, sys, zlib

def fnv(s):
    v = 0x811c9dc5
    for c in s.encode('latin-1', 'ignore'): v = ((c | 0x20) ^ v) * 0x1000193 & 0xffffffff
    return v

CATEGORIES = set(fnv(n) for n in ('QUEST', 'Chest', 'Bounty', 'Enemy', 'Encounter', 'Challenge', 'District'))

class Bad(Exception): pass

class R:
    def __init__(self, d, o): self.d = d; self.o = o
    def u32(self):
        if self.o + 4 > len(self.d): raise Bad('past the end at %d' % self.o)
        v = struct.unpack_from('<I', self.d, self.o)[0]; self.o += 4; return v
    def count(self, limit, what):
        v = self.u32()
        if v > limit: raise Bad('%s count %d at %d' % (what, v, self.o - 4))
        return v
    def expect(self, want, what):
        v = self.u32()
        if v not in (want if isinstance(want, tuple) else (want,)): raise Bad('%s is %d at %d' % (what, v, self.o - 4))
        return v

def facts(d):
    if d[:4] != b'RMDB' or len(d) < 32: raise Bad('not a save chunk')
    if zlib.crc32(d[16:]) & 0xffffffff != struct.unpack_from('<I', d, 12)[0]: raise Bad('checksum')
    n = struct.unpack_from('<I', d, 24)[0]; o = 28
    if o + 16 * n + 4 > len(d): raise Bad('number facts')
    val = {}
    for i in range(n):
        k, v = struct.unpack_from('<Qd', d, o + 16 * i); val[k] = v
    o += 16 * n
    b = struct.unpack_from('<I', d, o)[0]; o += 4
    if o + 9 * b > len(d): raise Bad('flag facts')
    for i in range(b):
        k, v = struct.unpack_from('<QB', d, o + 9 * i); val[k] = float(v)
    return val, o + 9 * b

def head(d, o):
    """the word-aligned sections after the facts: graphs, loadout, districts, outfit, narrative list"""
    r = R(d, o); m = {}
    m['world_level'] = r.count(1000, 'world level'); r.expect(1, 'unlock graphs version')
    graphs = {}
    for g in range(r.count(8, 'graphs')):
        r.expect(1, 'graph version'); gid = r.u32()
        a = [r.u32() for i in range(r.count(64, 'graph list'))]
        nodes = [r.u32() for i in range(r.count(4096, 'graph nodes'))]
        graphs[gid] = (a, nodes)
    m['graphs'] = graphs
    r.expect(1, 'loadout version')
    m['loadout'] = [(r.u32(), r.u32()) for i in range(r.count(64, 'loadout'))]
    r.expect(1, 'districts version')
    ds = []
    for i in range(r.count(64, 'districts')):
        r.expect((2, 3), 'district version')
        a = [r.u32() for k in range(r.count(256, 'district list'))]
        offers = [r.u32() for k in range(r.count(256, 'offers'))]
        rewards = []
        for k in range(r.count(256, 'level rewards')): r.expect(0, 'reward id pad'); rewards.append(r.u32())
        upgrades = []
        for k in range(r.count(256, 'upgrades')): r.expect(0, 'upgrade id pad'); upgrades.append(r.u32())
        level = r.u32(); xp = r.u32(); did = r.u32(); level2 = r.u32()
        ds.append(dict(id=did, level=level, xp=xp, level2=level2, rewards=rewards, upgrades=upgrades, offers=offers, a=a))
    m['districts'] = ds
    r.expect(1, 'outfit version')
    slots = []
    for i in range(r.count(64, 'outfit slots')):
        r.expect(3, 'slot version'); slots.append((r.u32(), r.u32(), r.u32(), r.u32()))
    m['outfit'] = slots
    m['narrative_version'] = r.expect((2, 3), 'narrative version')
    m['narrative_a'] = [r.u32() for i in range(r.count(4096, 'narrative list a'))]
    m['narrative_b'] = [r.u32() for i in range(r.count(4096, 'narrative list b'))]
    ids = []
    for i in range(r.count(8192, 'narrative objects')):
        r.expect(1, 'narrative entry'); r.expect(0, 'narrative id pad'); ids.append(r.u32()); r.u32()
    m['narrative'] = ids
    m['head_end'] = r.o
    return m

def rewards_at(d, o, known):
    r = R(d, o)
    r.expect((2, 3), 'reward version')
    decks = []
    for i in range(r.count(2000, 'decks')):
        r.expect(1, 'deck version'); cards = []
        for k in range(r.count(256, 'cards')):
            r.expect(1, 'card version'); r.expect(0, 'card id pad'); cards.append((r.u32(), r.u32()))
        r.expect(0, 'pad'); rid = r.u32(); r.expect(0, 'pad'); did = r.u32(); r.expect(0, 'pad'); cat = r.u32(); empty = r.u32()
        if cat not in CATEGORIES or (known is not None and rid not in known): raise Bad('deck ids')
        decks.append((rid, did, cat, empty, cards))
    given = []
    for i in range(r.count(4000, 'given')):
        r.expect(1, 'given version'); lv = []
        for k in range(r.count(64, 'given records')): r.expect(0, 'pad'); lv.append(r.u32())
        r.expect(0, 'pad'); rid = r.u32(); r.expect(0, 'pad'); cat = r.u32()
        if cat not in CATEGORIES or (known is not None and rid not in known): raise Bad('given ids')
        given.append((rid, cat, lv))
    if not decks and not given: raise Bad('empty')
    return decks, given, r.o

def rewards(d, start, known=None):
    """scans from the end of the word-aligned head for the reward section; (decks, given, offset) or None"""
    for o in range(start, len(d) - 12):
        if d[o] not in (2, 3) or d[o + 1:o + 4] != b'\0\0\0': continue
        try: return rewards_at(d, o, known) + (o,)
        except Bad: continue
    return None

def header(d):
    """the ...-header chunk: time saved, saves made, play time in seconds, location"""
    if len(d) < 49: raise Bad('header too short')
    ts, = struct.unpack_from('<Q', d, 20); nsaves, = struct.unpack_from('<I', d, 28); play, = struct.unpack_from('<I', d, 41)
    L, = struct.unpack_from('<I', d, 45)
    return dict(time=ts, saves=nsaves, play=play, location=d[49:49 + L].decode('latin-1') if 49 + L <= len(d) else '')

def parse(d, known=None):
    val, o = facts(d)
    m = head(d, o)
    m['facts'] = val
    rw = rewards(d, m['head_end'], known)
    m['decks'], m['given'], m['rewards_end'], m['rewards_at'] = rw if rw else ([], [], None, None)
    return m

if __name__ == '__main__':
    here = os.path.dirname(os.path.abspath(__file__)).replace('\\', '/')
    if sys.argv[1] == '--all':
        root = here + '/../../extracted_savegames/data/savegames'
        files = sorted(glob.glob(root + '/**/*persi-global*', recursive=True)) + sys.argv[2:]
    else: files = sys.argv[1:]
    ok = 0; versions = {}
    for f in files:
        d = open(f, 'rb').read()
        try:
            m = parse(d)
            ok += 1
            key = (m['narrative_version'], len(m['narrative_a']), len(m['narrative_b']), m['rewards_at'] is not None)
            versions[key] = versions.get(key, 0) + 1
            if len(files) < 12:
                print(os.path.basename(f), '| facts', len(m['facts']), '| graphs', {('%08x' % k): (len(a), len(n)) for k, (a, n) in m['graphs'].items()},
                      '| districts', [(x['level'], x['level2'], x['xp']) for x in m['districts']], '| narrative', len(m['narrative']),
                      '| decks', len(m['decks']), 'given', len(m['given']), '| head ends', m['head_end'], 'rewards at', m['rewards_at'])
        except Bad as e:
            print('FAIL %s: %s' % (f.replace('\\', '/').split('savegames/')[-1][:90], e))
    print('parsed %d of %d' % (ok, len(files)))
    print('(narrative version, list a, list b, reward section found):', versions)
