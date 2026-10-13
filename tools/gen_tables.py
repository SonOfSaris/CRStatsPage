# gen_tables.py - builds the tables the StatsPage mod counts with, from the game's data (game 1.4.1):
# tables.json (read by evalsave.py and the tests) and ../tables.inc (compiled in).
#   python gen_tables.py
# It reads listings taken out of the game's packs through extraction tools two folders up (bobj, questdb,
# worldfacts, questentries, rewarddb2). Neither the tools nor the listings are part of this repository: the two
# tables are checked in as they came out, and this script shows how they were made.
# A category is a list of items; an item is "done" by one test:
#   fact   any of its world facts is set (a fact missing from the save has its default)
#   narr   its id is in the save's list of collected narrative objects
#   given  its reward set is in the save's list of rewards handed out
#   node   its node is in an unlock graph (talents, melee)
#   form   a weapon form: among the melee graph's nodes or in the list of forms before them
#   level  a district's level; the item is worth as many units as the district has levels
# An item has a kind: '' can be earned and is in its category's count; 'reach' cannot be earned in the unmodded
# game (together those are the last 1% of the bar); 'promo' comes with an edition, a pre-order or a promotion (not
# in the bar, the page shows how many are owned).
# A category shows one kind of detail next to its count: "missing" (names of what is left), "groups" (a count per
# district or collection) or "none".
import json, os, re, struct, sys
here = os.path.dirname(os.path.abspath(__file__)).replace('\\', '/') + '/'
R = here + '../../'
sys.path.insert(0, R)
import bobj, questdb, worldfacts, questentries, rewarddb2

def fnv(s):
    v = 0x811c9dc5
    for c in s.encode('latin-1', 'ignore'): v = ((c | 0x20) ^ v) * 0x1000193 & 0xffffffff
    return v

T9 = chr(9); NL = chr(10)
def tsv(name):
    p = R + 'extracted/' + name
    return [l.split(T9) for l in open(p, encoding='utf-8').read().split(NL)[1:] if l]

names = worldfacts.load()                                   # key -> (name, wf id, 14 raw bytes)
byname = {v[0]: k for k, v in names.items()}
def fdefault(n):
    raw = bytes.fromhex(names[byname[n]][2])
    return struct.unpack_from('<d', raw, 5)[0] if raw[0] == 1 else float(raw[4])
en = questdb.strings_en()
d = open(questdb.GLOBAL, 'rb').read()

ASCII = {0x2019: "'", 0x2018: "'", 0x201c: '"', 0x201d: '"', 0x2013: '-', 0x2014: '-', 0x2026: '...', 0xfc: 'u', 0xdc: 'U', 0xe9: 'e', 0xf6: 'o', 0xe4: 'a', 0xa0: ' ', 0xfffd: "'"}
def clean(t):
    t = re.sub(r'<[^>]*>', '', t or '')
    t = ''.join(ASCII.get(ord(c), c if ord(c) < 127 else '') for c in t)
    return re.sub(r'\s+', ' ', t).strip()
def loc(*keys):
    """(text key, English text) of the first key the string table has"""
    for k in keys:
        if k and k in en and clean(en[k]) and clean(en[k]) not in ('-', 'CUT'): return k, clean(en[k])
    return '', ''
def pretty(s): return ' '.join(w.capitalize() for w in re.sub(r'[^a-z0-9]+', ' ', s.lower()).split())

ZKEY = {'downtown': 'ZONE_DOWNTOWN', 'frontline': 'ZONE_FRONTLINE', 'twisted': 'ZONE_TWISTED', 'twistedcity': 'ZONE_TWISTED', 'undercity': 'ZONE_UNDERCITY',
        'pattern': 'ZONE_PATTERN', 'mold': 'ZONE_MOLD', 'hiss': 'ZONE_HISS'}
ZFALL = {'downtown': 'Downtown', 'frontline': 'Central', 'twisted': 'Unknown', 'twistedcity': 'Unknown', 'undercity': 'Underpass', 'pattern': 'West Incursion',
         'mold': 'Park', 'hiss': 'Evacuation Zone'}
# The zones in the order a player comes to them. From the saves at
# hand (tools/statspage/encounter_order.py): the fewest main missions done among the saves that know a zone is
# 1, 2, 3, 3, 7, 13, 19. Every list by zone and the zones' numbers follow this order.
ZORDER = ['downtown', 'frontline', 'hiss', 'pattern', 'mold', 'undercity', 'twisted']
def zone(z):
    """(text key, English name) of a zone"""
    z = {'twistedcity': 'twisted'}.get(z, z)
    k, t = loc(ZKEY.get(z, ''))
    return (k, t) if t else ('', ZFALL.get(z, pretty(z)))
def zone_in(text):
    m = re.search(r'zone_?([a-z]+)', text)
    return m.group(1) if m else ''

cats = []
def cat(key, label, about, detail='none', spoiler=0, group_order=None, pool=0):
    c = dict(key=key, label=label, about=about, detail=detail, spoiler=spoiler, pool=pool, items=[], groups=[]); cats.append(c); return c
def group_of(c, key, name, zi=-1):
    for i, g in enumerate(c['groups']):
        if g['name'] == name: return i
    c['groups'].append(dict(key=key, name=name, zone=zi)); return len(c['groups']) - 1
def zgroup(z):
    """a zone as a group: (text key, English name, the zone's number in ZORDER)"""
    k, t = zone(z)
    return k, t, ZORDER.index({'twistedcity': 'twisted'}.get(z, z))
def item(c, ident, test, name, key='', kind='', note='', group=None, **kw):
    it = dict(id=fnv(c['key'] + ':' + ident), ident=ident, test=test, name=name, key=key, kind=kind, note=note, nz=-1, rv=-1, ns='',
              group=group_of(c, *group) if group else -1)
    it.update(kw); c['items'].append(it); return it
def facts(*ns): return [dict(key=byname[n], default=fdefault(n), name=n) for n in ns]

# ---------------------------------------------------------------- missions, side stories, tasks, World Feats
qs, bad = questentries.quests()
assert not bad
def endfacts(q): return sorted(set(e['completion']['name'] for e in q['entries'] if e['completeQuest'] and e['completion']['name'] in byname))
c_main = cat('main', 'Main Story Missions', 'Missions completed from the Main Story.', 'missing', 1)
c_side = cat('side', 'Side Stories', 'Side story missions completed around Manhattan.', 'missing', 1)
# The six side stories that come in parts (the game's "mini quests", facts quests/mini_quests/...): a row each.
# Every part is a hidden journal entry that hands out a reward, with a fact of its own; a story that has a step of
# its own after the last part also counts its ending (the quest's end fact). The taxi and map stories end with
# their last part. Together the six are ONE part of the bar (pool 1). Titles and descriptions are the mod's own;
# the game's titles of the dog and map stories begin with "Notes: ".
# quest: (row key, title, description, the entry's fact that is the part's own, how to tell a part entry, an ending of its own, items)
PARTS = {
 'mq_01_polaroid_camera': ('freeze', 'Freeze Frame', 'Close the shutters on altered cameras throughout Manhattan.', 'activation', r'^mq_01 ', True, 15),
 'mq_02_taxi_title': ('taxi', 'The Last Taxi', 'Solve the puzzle behind the paranatural taxis around Manhattan.', 'completion', r'^mq_02 ', False, 7),
 'mq_03_mysterious_dog': ('dog', 'Every Dog Has Her Day', 'Help a lost dog find her toy.', 'completion', r'^mq_03 [a-z]+ - ', True, 8),
 'cartography': ('mapkits', 'Mapping the New New York', 'Activate FBC Map Kits across patterned Manhattan.', 'completion', r'^mq_05 ', False, 7),
 'mq_06_testing_facility': ('science', 'Unsupervised Science', "Shut down the FBC's hidden test facilities.", 'completion', r'^reward_testing_facility_', True, 7),
 'mq_09_popes_research': ('pope', "Pope's Research", "Activate Pope's resonance monitoring balloons in the Unknown.", 'completion', r'^reward - mq_09_', True, 8),
}
c_parts = {q: cat(p[0], p[1], p[2], 'parts', 0, pool=1) for q, p in PARTS.items()}
def parts_of(q, ef):
    key, title, about, col, pat, ending, want = PARTS[q['name']]
    c = c_parts[q['name']]; found = []
    for e in q['entries']:
        if not e['isHidden'] or not re.search(pat, e['name']): continue
        fact = e[col]['name']
        z = [z for z in ZORDER if re.search(r'(^|[_/ ])%s([_/ ]|$)' % z, fact + ' ' + e['name'])]
        assert z and fact in byname, (q['name'], e['name'], fact)
        found.append((ZORDER.index(z[0]), fact))
    for zi, fact in sorted(found):
        zk, zt, zn = zgroup(ZORDER[zi])
        n = sum(1 for i in c['items'] if i['name'].startswith(zt + ' '))
        item(c, fact, 'fact', '%s %d' % (zt, n + 1), group=(zk, zt, zn), facts=facts(fact))
    if ending: item(c, 'ending', 'fact', 'Ending', group=('', 'Ending', -2), facts=facts(*ef))
    assert len(c['items']) == want, (q['name'], len(c['items']), want)
# The Complete Set (quest paracriminals) is such a story too: four Speakers and a Subwoofer, found in any order
# (saves have the museum's Speaker alone), then the ritual. Its parts are no hidden entries: each is the completion
# fact of an objective that hands out the piece.
# (zone or None, fact): the auction house, the construction site, the coffee shop, the Museum of Curiosities, the Oldest House
SPEAKERS = [('downtown', 'quests/paracriminals/retrieved_objects/retrieved_speaker_auction_house'),
            ('frontline', 'quests/paracriminals/retrieved_objects/retrieved_speaker_market_collector'),
            ('frontline', 'quests/paracriminals/retrieved_objects/retrieved_speaker_auction_buyer'),
            ('pattern', 'quests/paracriminals/retrieved_objects/retrieved_speaker_museum'),
            (None, 'quests/paracriminals/retrieved_objects/retrieved_subwoofer')]
c_speakers = cat('speakers', 'The Complete Set', 'Collect the pieces of an altered speaker set around Manhattan.', 'parts', 0, pool=1)
def speakers_of(q, ef):
    c = c_speakers
    handed = set(e['completion']['name'] for e in q['entries'] if e['guaranteed'] and not e['completeQuest'])
    house = loc('MAP_SIGNAGE_DOWNTOWN_OLDEST_HOUSE'); assert house[1] == 'Oldest House', house
    for z, fact in SPEAKERS:
        assert fact in handed and fact in byname, fact
        zk, zt, zn = zgroup(z) if z else (house[0], house[1], -1)
        n = sum(1 for i in c['items'] if i['name'].startswith(zt + ' '))
        item(c, fact, 'fact', '%s %d' % (zt, n + 1), group=(zk, zt, zn), facts=facts(fact))
    item(c, 'ending', 'fact', 'Ending', group=('', 'Ending', -2), facts=facts(*ef))
    assert len(c['items']) == 6 and len(ef) == 2, (len(c['items']), ef)
c_task = cat('tasks', 'FBC Tasks', 'FBC assignments from the task boards.', 'groups')
c_feat = cat('feats', 'World Feats', 'Small objectives completed in each zone.', 'groups')
(_o, _c, dblob), = bobj.blobs(d, 'contentgame::DistrictDefinitionDatabase')
pool = set(m.decode() for m in re.findall(rb'b_zone_[a-z]+_[a-z_]+_\d+', dblob))
zoned = []
for q in qs:
    ef = endfacts(q)
    k, t = loc(q['title'])
    if q['type'] == 0 and ef: item(c_main, q['name'], 'fact', t or pretty(q['name']), k, facts=facts(*ef))
    elif q['type'] == 1 and 'quest_tag_tasks' in q['tags'] and ef: zoned.append((c_task, q, k, t, ef))
    elif q['name'] in PARTS and ef: parts_of(q, ef)
    elif q['name'] == 'paracriminals' and ef: speakers_of(q, ef)
    elif q['type'] == 1 and ef and not t.startswith('[IGNORE'): item(c_side, q['name'], 'fact', t or pretty(q['name']), k, facts=facts(*ef))
    elif q['type'] == 2 and q['name'] in pool and ef: zoned.append((c_feat, q, k, t, ef))
for z in ZORDER:
    for c, q, k, t, ef in zoned:
        if zone_in(q['name']) == z: item(c, q['name'], 'fact', t or pretty(q['name']), k, group=zgroup(z), facts=facts(*ef))
assert len(c_task['items']) + len(c_feat['items']) == len(zoned)
assert len(c_side['items']) == 12 and all(c['items'] for c in c_parts.values()) and len(c_speakers['items']) == 6, (len(c_side['items']), [len(c['items']) for c in c_parts.values()])
# The names in the list of main missions, in the order players get them done. From the same saves:
# by the fewest main missions done among the saves that have one done, then by how many saves have it.
MAIN_ORDER = ['dq_s1p1_wake_up', 'dq_s3p1a_enter_the_nightmare', 'dq_s3p1c_rescue_mission', 'app_heat', 'dq_s3p1d_arish_debrief', 'app_gravity', 'app_reach_title',
              'wq_02_body_hopper', 'app_dancer', 'dq_s3p1b_the_subway', 'dq_s3p2_sinkhole', 'dq_s3p1e_cfd_preparations', 'app_mold', 'dq_s3p3_cfd_disaster', 'app_broken',
              'app_astral', 'dq_s3p4_anomaly', 'dq_s3p6_midpoint_collapse', 'search_for_jesse', 'dq_s3p7_midpoint_first_entity_link', 'the psychic',
              'dq_s4p0_sever_the_entity_links', 'dq_s4p1_pattern_attack', 'dq_s5p1_fight_core', 'final fight', 'dq_s6p1_the_epilogue']
def reorder(c, order):
    assert sorted(i['ident'] for i in c['items']) == sorted(order), (c['key'], sorted(set(i['ident'] for i in c['items']) ^ set(order)))
    c['items'].sort(key=lambda i: order.index(i['ident']))
reorder(c_main, MAIN_ORDER)
# Side Stories lists its names in alphabetical order: the completed ones and the missing ones are two lists
# over these items. By the English names; the page shows the game's text for a name.
c_side['items'].sort(key=lambda i: i['name'].lower())

# ---------------------------------------------------------------- SHUM scores: the three reward tiers of each machine, one per place on its
# high score list (third, second, first); the page shows the place
db = rewarddb2.parse()
# A machine outside the zones' own sets: a label for where it stands (no text key, the label is the mod's own)
# and zone -3, a place the page names only once the main story is done.
SHUM_PLACES = {'twisted_jrpg': ('', 'Unknown (Apartment)', -3)}
c_shum = cat('shum', 'SHÜM Scores', 'High scores on SHÜM machines. Earn more points playing SHÜM to place higher. Higher scores unlock more rewards up to 1st Place.', 'groups')
shum = [s['rewardID'] for s in db['sets'] if s['category'] == 4]
for z in ZORDER + ['other']:
    for rid in shum:
        m = re.fullmatch(r'challenge_([a-z_]+?)_(first|second|third)', rid)
        zz = m.group(1) if m else 'other'
        zz = zz if zz in ZORDER else 'other'
        if zz != z: continue
        g = zone(z) if z != 'other' else ('', pretty(m.group(1)) if m else 'Other')
        if z == 'other' and m and m.group(1) in SHUM_PLACES: g = SHUM_PLACES[m.group(1)]
        item(c_shum, rid, 'given', '%s, %s tier' % (g[1], m.group(2) if m else '?'), group=zgroup(z) if z != 'other' else g, hash=fnv(rid))
assert len(c_shum['items']) == len(shum)

# ---------------------------------------------------------------- flags the level data names (factkeyscan.py)
key_used = set(r[0] for r in tsv('fact_key_usage_141.tsv') if len(r) > 2 and r[2] != '0')
ever_set = set(r[0] for r in tsv('facts_ever_set_141.tsv') if r[-1] == '1')
exists = lambda n: n in key_used or n in ever_set
c_enc = cat('encounters', 'World Encounters', 'Distinct enemy encounters around Manhattan.', 'groups')
c_vend = cat('vending', 'Hiss Vending Machines', 'Hiss-possessed vending machines destroyed.', 'groups')
for z in ZORDER:
    for n in sorted(byname):
        m = re.fullmatch(r'encounters/zone_([a-z]+)/completed_([a-z0-9_]+)', n)
        if m and m.group(1) == z and exists(n): item(c_enc, n, 'fact', pretty(m.group(2)), group=zgroup(z), facts=facts(n))
    for n in sorted(byname, key=lambda x: (len(x), x)):
        m = re.fullmatch(r'vending_machines/zone_([a-z]+)/defeated_vending_machine_(\d+)', n)
        if m and m.group(1) == z and exists(n): item(c_vend, n, 'fact', '%s %s' % (zone(z)[1], m.group(2)), group=zgroup(z), facts=facts(n))

# ---------------------------------------------------------------- collectibles and enemy research
# A definition of the narrative object database holds objectID and loreCollectionID one after the other, each a string
# followed by its FNV hash; the save lists the objectID hashes of what was collected.
(_, _, nb), = bobj.blobs(d, 'contentgame::NarrativeObjectDatabaseComponent')
def lstr(o, lo, hi):
    if o + 4 > len(nb): return None
    n = struct.unpack_from('<I', nb, o)[0]
    if n < lo or n > hi or o + 4 + n > len(nb): return None
    t = nb[o + 4:o + 4 + n]
    if any(ch < 32 or ch > 126 for ch in t): return None
    return t.decode(), o + 4 + n
defs = []
for o in range(0, len(nb) - 12):
    r = lstr(o, 3, 120)
    if not r or r[1] + 4 > len(nb) or struct.unpack_from('<I', nb, r[1])[0] != fnv(r[0]): continue
    nxt = lstr(r[1] + 4, 20, 200)
    if nxt and nxt[0].startswith('/global/narrative_object_database/'): defs.append((r[0], nxt[0].split('/')[3], nxt[0].split('/')[4]))
assert len(defs) == len(set(x[0] for x in defs))
c_col = cat('collectibles', 'Collectibles', 'Documents, recordings, pictures and videos for the Archives.', 'groups')
for nm, grp, col in defs:
    if grp != 'collectibles': continue
    k, t = loc(nm.upper() + '_NAME', nm.upper())
    gk, gt = loc('NAR_COLLECTION_' + col.upper())
    item(c_col, nm, 'narr', t or pretty(nm.replace('opcon_', '')), k, group=(gk, gt or pretty(col)), hash=fnv(nm))
c_res = cat('research', 'Enemy Research', 'Research entries on the enemies you have met.', 'missing', 1)
for nm, grp, col in defs:
    if grp != 'enemy_collectibles': continue
    k, t = loc(nm.upper() + '_NAME', 'NARRATIVE_OBJECT_ENEMY_' + nm.upper(), nm.upper())
    item(c_res, nm, 'narr', t or pretty(nm), k, hash=fnv(nm))

# ---------------------------------------------------------------- Artifacts
aen = {}
for l in open(R + 'extracted/charm_effects_141.txt', encoding='utf-8').read().split(NL):
    m = re.match(r'== (.+?)  \((ch_[0-9a-z_]+)\)', l)
    if m: aen[m.group(2)] = clean(m.group(1))
def aname(a):
    rest = re.sub(r'^ch_\d+_', '', a).upper()
    k, t = loc(a.upper(), 'CHARM_' + rest)
    if t: return k, t
    c = sorted((k for k in en if k.startswith(a[:6].upper() + '_') and not k.endswith(('_BP', '_DESC', '_NAME', '_EFFECT', '_LORE', '_DESCRIPTION'))), key=len)
    if c: return c[0], clean(en[c[0]])
    return '', aen.get(a, pretty(a))
# Three Artifacts have no working source in the 1.4.1 data. What the page does with them:
#   Trusted Spanner         counts. The dog side story did not hand it out up to game 1.4.1; game 1.4.2 (2026-10-08)
#                           does, also to who finished the story before and has not started New Game Plus. Its
#                           note is that story's, like any other reward of a story in parts.
#   The Weekend Crossword   counts. Players report it as a zone level reward. In the 1.4.1 data its blueprint sits
#                           on a reward set nothing names, so the note says no more than "source unconfirmed".
#   Water Pistol            left out of the tables: a recipe, but no blueprint anywhere.
COUNTED = {'ch_101_wrench', 'ch_105_stapler'}
LEFT_OUT = {'ch_028_dumbbell'}
ZLEVEL = {'du_downtown_charm': ('downtown', 13), 'du_frontline_charm': ('frontline', 12), 'du_twisted_charm': ('twisted', 11),
          'du_downtown_outfit': ('downtown', 10), 'du_frontline_outfit': ('frontline', 14), 'du_twisted_outfit': ('twisted', 12)}
def level_note(text):
    """(note, zone number). "{z}" in the note stands for the zone's name; the page holds it back while the zone is not known"""
    for k, (z, lv) in ZLEVEL.items():
        if k in text: return '{z} zone level %d' % lv, ZORDER.index(z)
    return '', -1
# Every thing of the Dylan tab has a note that says where it comes from. A note names no mission: it gives the
# kind of thing and its zone ("{z}", held back like every zone's name). A reward of a side story in parts names
# that story ("{s}": the row's title, ??? while the story is not found). Sources: which quest's journal entry hands
# out which reward set (questentries), and the zone in the set's name.
SET_QUEST = {}; QKIND = {}
for q in qs:
    for e in q['entries']:
        for s in e['guaranteed'] + e['loot']:
            if s: SET_QUEST.setdefault(s, q['name'])
    n = q['name']
    QKIND[n] = ('main' if any(i['ident'] == n for i in c_main['items']) else 'side' if any(i['ident'] == n for i in c_side['items']) else
                PARTS[n][0] if n in PARTS else 'speakers' if n == 'paracriminals' else 'sketch' if n == 'mq_08_sketchbook' else '')
STORY_ROWS = set(p[0] for p in PARTS.values()) | {'speakers'}
ZTOK = {'downtown': 'downtown', 'frontline': 'frontline', 'zonehiss': 'hiss', 'hiss': 'hiss', 'pattern': 'pattern', 'moldtown': 'mold', 'mold': 'mold',
        'undercity': 'undercity', 'twistedcity': 'twisted', 'twisted': 'twisted'}
def zone_of(text):
    """the number of the zone a reward set's name speaks of, -1 for none"""
    for tok in re.split(r'[_ ]', text):
        if tok in ZTOK: return ZORDER.index(ZTOK[tok])
    return -1
def quest_zone(q):
    for s, qq in SET_QUEST.items():
        if qq == q and zone_of(s) >= 0: return zone_of(s)
    return -1
PLACE = {'first': '1st Place', 'second': '2nd Place', 'third': '3rd Place'}
def quest_note(setname, seq=1):
    """(note, zone number, story row) of what a quest's reward set hands out. A set gives its next record with every
    hand-out, so a record after the first comes with a later playthrough."""
    kind = QKIND.get(SET_QUEST.get(setname, ''), '')
    again = ' in New Game Plus' if seq > 1 else ''
    if kind in ('main', 'side'):
        z = zone_of(setname)
        return ('{z} ' if z >= 0 else '') + ('main mission' if kind == 'main' else 'side story') + again, z, ''
    if kind == 'sketch': return 'every Sketch completed', -1, ''          # the hidden quest behind the map's Sketch markers
    assert kind in STORY_ROWS and not again, (setname, kind, seq)
    return 'side story: {s}', -1, kind
def artifact_note(a, text):
    if a == 'ch_105_stapler': return 'source unconfirmed', -1, ''
    ln, nz = level_note(text)
    if ln: return ln, nz, ''
    if 'sold by a vendor' in text: return 'blueprint sold by a vendor', -1, ''
    m = re.search(r'challenge_([a-z_]+?)_(first|second|third)', text)
    if m:           # the machine outside the zones' own sets is not named: its place is held back until the story's end
        z = -1 if 'jrpg' in m.group(1) else zone_of(m.group(1))
        return ('{z} SH\u00dcM, ' if z >= 0 else 'a SH\u00dcM machine, ') + PLACE[m.group(2)], z, ''
    for s, rec in re.findall(r'(?:blueprint|artifact): (\S+) record (\d+)', text):
        if s in SET_QUEST: return quest_note(s, int(rec))
    m = re.search(r'world_encounter_\S+', text)
    assert m, (a, text)
    return '{z} world encounter', zone_of(m.group(0)), ''
cosmetics = {}
for r in tsv('reward_sets_141.tsv'):
    for c in (r[12].split() if len(r) > 12 else []): cosmetics.setdefault(c, []).append(r)
def outfit_note(rows):
    if any(r[5] == 'default' for r in rows): return 'owned from the start', -1, ''
    if any('shop item' in r[7] for r in rows): return 'sold by a vendor', -1, ''
    for r in rows:
        for s in cosmetics.get(r[0], []):
            name, category, record, seq = s[0], s[1], s[2], int(s[3] or 0)
            if category == 'District': return level_note(name) + ('',)
            if category == 'Challenge':
                m = re.fullmatch(r'challenge_([a-z_]+?)_(first|second|third)', record)
                return '{z} SH\u00dcM, ' + PLACE[m.group(2)], zone_of(m.group(1)), ''
            if category == 'Chest':
                assert 'fbc_testing_facility' in name, name
                return '{z} FBC test facility crate', zone_of(name), ''
            if category == 'Quest': return quest_note(name, seq)
    raise AssertionError(rows[0][0])
c_art = cat('artifacts', 'Artifacts', 'Artifacts owned and tapped.', 'missing')
for r in tsv('artifact_sources_141.tsv'):
    a, state, good = r[0], r[1], (r[2] if len(r) > 2 else '')
    if state == 'stub' or a in LEFT_OUT: continue
    assert state.startswith('earnable') or a in COUNTED, (a, state)
    k, t = aname(a)
    note, nz, ns = artifact_note(a, good or (r[3] if len(r) > 3 else ''))        # the Spanner's source is in the "broken" column
    item(c_art, a, 'fact', t, k, note=note, nz=nz, ns=ns, facts=facts('charm/' + a))
assert all(i['note'] and (i['nz'] >= 0) == ('{z}' in i['note']) and bool(i['ns']) == ('{s}' in i['note']) for i in c_art['items'])

# ---------------------------------------------------------------- outfit pieces
oflags = {}
for r in tsv('outfit_items_141.tsv'):
    if len(r) > 7 and r[2]: oflags.setdefault(r[2], []).append(r)
K9 = 'outfits/accessories/accessories_dog_ears'
c_out = cat('outfits', 'Clothes', 'Unlocked clothing for Dylan.', 'missing')
for n in sorted(oflags):
    rows = oflags[n]
    earn = any(r[5] in ('default', 'in-game') for r in rows) and n != K9
    k, t = loc('OUTFIT_ITEM_' + rows[0][0].upper())
    kind = '' if earn else ('reach' if n == K9 else 'promo')
    note, nz, ns = ('reward never arrives', -1, '') if n == K9 else outfit_note(rows) if earn else ('', -1, '')
    item(c_out, n, 'fact', t or clean(rows[0][1]), k, kind=kind, note=note, nz=nz, ns=ns, facts=facts(n))
assert all(i['note'] and (i['nz'] >= 0) == ('{z}' in i['note']) for i in c_out['items'] if not i['kind'])

# ---------------------------------------------------------------- abilities
ABIL = {'air_dodge': ('', 'Air Dodge'), 'astral_construct_summon': ('ABILITY_ASTRAL_SUMMON', 'Spike'), 'barrage_ability': ('ABILITY_BARRAGE', 'Barrage'),
        'collapse_push': ('ABILITY_PUSH', 'Push'), 'dash': ('', 'Dash'), 'dodge': ('', 'Dodge'), 'flame_wave': ('ABILITY_FLAME_WAVE', 'Inferno'),
        'gravity_anomaly_ability': ('', 'Gravity Anomaly'), 'ground_slam': ('ABILITY_GROUND_SLAM', 'Ground Slam'), 'heat_aura': ('ABILITY_HEAT_AURA', 'Ignite'),
        'jump_ability': ('', 'Jump'), 'needles': ('ABILITY_NEEDLES', 'Spore Burst'), 'perfect_dodge': ('', 'Perfect Dodge'), 'reach': ('', 'Reach'),
        'seekers': ('ABILITY_SEEKERS', 'Volatile Anomaly'), 'seize': ('ABILITY_SEIZE', 'Command'), 'shield': ('ABILITY_SHIELD', 'Shield'),
        'sunder': ('ABILITY_SUNDER', 'Astral Rebuke'), 'surge': ('', 'Surge'), 'turret_summon': ('ABILITY_MOLD_TURRET', 'Growth')}
# Twelve abilities come from the ability selections that open when a Resonant is defeated; each selection has a fact
# "ability_select_unlocks/<boss>_boss_ability_unlocked". Which ability is in which selection is not in the data found
# so far; it is read off the saves of three players (which ability arrived with which selection's count): the first
# selection offers three, the others two, the last one a single ability. Spike is placed by elimination. The page
# names a missing ability only once its selection is open; the eight basic moves belong to none.
ASET = {'barrage_ability': 'barrage', 'seekers': 'barrage', 'shield': 'barrage', 'collapse_push': 'dancer', 'ground_slam': 'dancer',
        'flame_wave': 'heat', 'heat_aura': 'heat', 'needles': 'mold', 'turret_summon': 'mold', 'sunder': 'astral', 'astral_construct_summon': 'astral',
        'seize': 'psychic'}
REVEALS = ['barrage', 'dancer', 'heat', 'mold', 'astral', 'psychic']
reveals = [dict(id=b, fact=facts('ability_select_unlocks/%s_boss_ability_unlocked' % b)[0]) for b in REVEALS]
# Where an ability comes from. The twelve of the selections: the Resonant whose defeat opens the selection, by its
# zone. The eight basic moves, from the saves at hand: five are there from the first save on; Perfect Dodge, Gravity
# Anomaly and Reach are set in exactly the saves that have one main mission done (The Central Resonant, The
# Incursion Fault, The Subway Fault).
BOSS_ZONE = {'barrage': 'downtown', 'heat': 'frontline', 'dancer': 'pattern', 'mold': 'mold', 'astral': 'undercity', 'psychic': 'twisted'}
BASIC = {'air_dodge': '', 'dash': '', 'dodge': '', 'jump_ability': '', 'surge': '', 'perfect_dodge': 'app_heat', 'gravity_anomaly_ability': 'app_gravity', 'reach': 'app_reach_title'}
def ability_note(a):
    if a in ASET: return '{z} Resonant', ZORDER.index(BOSS_ZONE[ASET[a]])
    if not BASIC[a]: return 'owned from the start', -1
    assert QKIND[BASIC[a]] == 'main' and quest_zone(BASIC[a]) >= 0, a
    return '{z} main mission', quest_zone(BASIC[a])
c_abi = cat('abilities', 'Abilities', 'Resonant abilities unlocked.', 'missing')
for n in sorted(byname):
    if n.startswith('ability_unlocks/'):
        a = n.split('/')[1].replace('_unlocked', '')
        k, t = loc(ABIL[a][0])
        note, nz = ability_note(a)
        item(c_abi, n, 'fact', t or ABIL[a][1], k, note=note, nz=nz, rv=REVEALS.index(ASET[a]) if a in ASET else -1, facts=facts(n))
assert sum(1 for i in c_abi['items'] if i['rv'] >= 0) == len(ASET) == 12

# ---------------------------------------------------------------- talents and weapon upgrades
(_, _, ublob), = bobj.blobs(d, 'contentgame::UnlocksGraphDatabaseComponent')
c_tal = cat('talents', 'Talents', 'Talents unlocked.')
for t in sorted(set(m.decode() for m in re.findall(rb'(?<![a-z0-9_])t_[0-9]+_[a-z0-9_]+', ublob)), key=lambda x: (int(x.split('_')[1]), x)):
    item(c_tal, t, 'node', pretty(re.sub(r'^t_\d+_', '', t)), graph_id=fnv('Talents'), hash=fnv(t))
c_wpn = cat('weapons', 'Weapon Upgrades', 'Weapon forms and their upgrades.')
mel = sorted(set((m[1].decode(), m[2].decode()) for m in re.findall(rb'/unlocks_graph_database/melee/(main|secondary|combo_ender)/([a-z0-9_]+)/([a-z0-9_]+)', d)))
seen = set()
for form, node in mel:
    if node in seen or node.startswith('wf_'): continue
    seen.add(node)
    item(c_wpn, node, 'node', '%s (%s)' % (pretty(node), pretty(form)), graph_id=fnv('Melee'), hash=fnv(node))
forms = sorted(set(f for f, n in mel))
# a weapon form is owned when the save lists it among the graph's nodes (equipped) or in the list before them
for f in forms:
    k, t = loc('WEAPON_FORM_' + f.upper())
    item(c_wpn, 'form:' + f, 'form', t or pretty(f), k, graph_id=fnv('Melee'), hash=fnv(f))

# ---------------------------------------------------------------- zone levels
c_lvl = cat('levels', 'Zone Levels', 'Level of each zone dividing Manhattan.', 'groups')
for z, did, top in sorted((('downtown', 'downtown_district', 14), ('frontline', 'frontline_district', 14), ('twisted', 'twisted_district', 14), ('undercity', 'undercity_district', 9),
                    ('pattern', 'pattern_district', 9), ('mold', 'mold_district', 9), ('hiss', 'zone_hiss', 9)), key=lambda t: ZORDER.index(t[0])):
    item(c_lvl, did, 'level', zone(z)[1], zone(z)[0], group=zgroup(z), hash=fnv(did), units=top)

# ---------------------------------------------------------------- the zones, and how a save says one is known
# A zone is known once its district has a level (1 or more) or its map is unlocked. In the saves at hand the level
# comes first for some zones and the map for others; a zone never visited has neither.
ZDISTRICT = {'downtown': 'downtown_district', 'frontline': 'frontline_district', 'twisted': 'twisted_district', 'undercity': 'undercity_district',
             'pattern': 'pattern_district', 'mold': 'mold_district', 'hiss': 'zone_hiss'}
zones = [dict(id=z, key=zone(z)[0], name=zone(z)[1], district=fnv(ZDISTRICT[z]), fact=facts('map_unlocks/map_unlocked_zone_' + z)[0]) for z in ZORDER]

# ---------------------------------------------------------------- reward ids the save's reward section may name
known = set()
for s in db['sets']: known.add(fnv(s['rewardID']))
for s in db['stacks']: known.add(fnv(s['rewardID']))

for c in cats:
    ids = [i['id'] for i in c['items']]
    assert len(ids) == len(set(ids)), c['key']
    c['total'] = sum(i.get('units', 1) for i in c['items'] if not i['kind'])
out = dict(game='1.4.1', categories=cats, zones=zones, reveals=reveals, known_rewards=sorted(known), melee_forms=forms)
json.dump(out, open(here + 'tables.json', 'w', encoding='utf-8'), indent=0)

# ---------------------------------------------------------------- ../tables.inc
def cq(t):
    """a C++ string literal for UTF-8 text"""
    if t is None: return 'nullptr'
    BS = chr(92)
    out = []
    for ch in t.encode('utf-8'):
        c = chr(ch)
        if c == '"' or c == BS: out.append(BS + c)
        elif 32 <= ch < 127: out.append(c)
        else: out.append(BS + 'x%02x""' % ch)
    return '"' + ''.join(out) + '"'
TESTS = {'fact': 0, 'narr': 1, 'given': 2, 'node': 3, 'form': 4, 'level': 5}
DETAIL = {'none': 0, 'missing': 1, 'groups': 2, 'parts': 3}
KINDS = {'': 0, 'reach': 1, 'promo': 2}
fl = []; il = []; gl = []; cl = []
CATI = {c['key']: i for i, c in enumerate(cats)}
for c in cats:
    fi, gi = len(il), len(gl)
    for g in c['groups']: gl.append('    { %s, %s, %d },' % (cq(g['name']), cq(g['key']), g['zone']))
    for it in c['items']:
        t = it['test']
        if t == 'fact':
            a_, b_ = len(fl), len(it['facts'])
            for f in it['facts']: fl.append('    { 0x%016XULL, %d },   // %s' % (f['key'], 1 if f['default'] else 0, f['name']))
        elif t in ('node', 'form'): a_, b_ = it['graph_id'], it['hash']
        else: a_, b_ = it['hash'], 0
        il.append('    { 0x%08Xu, %d, %d, %d, %d, 0x%Xu, 0x%Xu, %s, %s, %s, %d, %d, %d },' % (it['id'], TESTS[t], it.get('units', 1), it['group'], KINDS[it['kind']],
                  a_, b_, cq(it['name']), cq(it['key']), cq(it['note']), it['nz'], it['rv'], CATI[it['ns']] if it['ns'] else -1))
    cl.append('    { %s, %s, %s, %d, %d, %d, %d, %d, %d, %d, %d },' % (cq(c['key']), cq(c['label']), cq(c['about']), DETAIL[c['detail']], c['spoiler'], c['total'], fi, len(c['items']), gi, len(c['groups']), c['pool']))
zl = []
for z in zones:
    zl.append('    { %s, %s, 0x%08Xu, %d },' % (cq(z['name']), cq(z['key']), z['district'], len(fl)))
    fl.append('    { 0x%016XULL, %d },   // %s' % (z['fact']['key'], 1 if z['fact']['default'] else 0, z['fact']['name']))
rl = []
for r in reveals:
    rl.append('%d' % len(fl))
    fl.append('    { 0x%016XULL, %d },   // %s' % (r['fact']['key'], 1 if r['fact']['default'] else 0, r['fact']['name']))
with open(here + '../tables.inc', 'w', encoding='utf-8', newline='\n') as f:
    f.write('// Generated by tools/gen_tables.py from the data of game %s. Do not edit.\n' % out['game'])
    f.write('const FactDef kFacts[] = {\n%s\n};\n' % '\n'.join(fl))
    f.write('// name, text key, the zone it is (a number in kZones), -1 for none, -2 for the ending of a story in parts\n')
    f.write('const GroupDef kGroups[] = {\n%s\n};\n' % '\n'.join(gl))
    f.write('// name, text key, district, the fact that says its map is unlocked (a number in kFacts)\n')
    f.write('const ZoneDef kZones[] = {\n%s\n};\n' % '\n'.join(zl))
    f.write('// the facts that open an ability selection (numbers in kFacts); an item\'s "rv" is a number in this list\n')
    f.write('const uint16_t kReveals[] = { %s };\n' % ', '.join(rl))
    f.write('// id, test, units, group, kind, a, b, name, text key, note, the zone the note names or -1, the selection that reveals the name or -1\n')
    f.write('const ItemDef kItems[] = {\n%s\n};\n' % '\n'.join(il))
    f.write('// key, label, about, detail, spoiler, total, first item, items, first group, groups, pool\n')
    f.write('const CatDef kCats[] = {\n%s\n};\n' % '\n'.join(cl))
print('tables.inc: %d facts, %d items, %d groups' % (len(fl), len(il), len(gl)))
print('categories: %d' % len(cats))
for c in cats:
    unnamed = sum(1 for i in c['items'] if not i['key'])
    print('  %-13s %-22s in the bar %4d | last 1%% %2d | promotion %2d | no text key %3d | groups %2d %s' % (c['key'], c['label'].encode('ascii', 'replace').decode(), c['total'],
          sum(1 for i in c['items'] if i['kind'] == 'reach'), sum(1 for i in c['items'] if i['kind'] == 'promo'), unnamed, len(c['groups']), [g['name'] for g in c['groups']][:8] if c['key'] in ('feats', 'shum', 'levels') else ''))
print('melee forms:', forms)
