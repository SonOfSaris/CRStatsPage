# evalsave.py - reference evaluation of a save against tables.json: the numbers the mod's page must show.
#   python evalsave.py <persi-global> [json]         one save: every category and the bar
#   python evalsave.py --coverage <persi-global>...  ids of the saves' unlock graphs that no table item names
import json, os, sys
here = os.path.dirname(os.path.abspath(__file__)).replace('\\', '/') + '/'
sys.path.insert(0, here)
import savemodel

def tables(): return json.load(open(here + 'tables.json', encoding='utf-8'))

def done_units(it, m, given, narr):
    """units of an item the save has (0 or 1, a level for district items)"""
    t = it['test']
    if t == 'fact': return int(any((m['facts'].get(f['key'], f['default']) != 0) for f in it['facts']))
    if t == 'narr': return int(it['hash'] in narr)
    if t == 'given': return int(it['hash'] in given)
    if t == 'node': return int(it['hash'] in m['graphs'].get(it['graph_id'], ((), ()))[1])
    if t == 'form':
        g = m['graphs'].get(it['graph_id'], ((), ()))
        return int(it['hash'] in g[0] or it['hash'] in g[1])
    if t == 'level':
        return min(it['units'], max([x['level'] for x in m['districts'] if x['id'] == it['hash']] or [0]))
    raise ValueError(t)

def known_zones(m, T):
    """per zone of the tables: does the save know it (its district has a level, or its map is unlocked)"""
    out = []
    for z in T['zones']:
        level = max([x['level'] for x in m['districts'] if x['id'] == z['district']] or [0])
        out.append(level >= 1 or m['facts'].get(z['fact']['key'], z['fact']['default']) != 0)
    return out

def revealed(m, T):
    """per ability selection of the tables: is it open in the save (its Resonant is defeated)"""
    return [m['facts'].get(r['fact']['key'], r['fact']['default']) != 0 for r in T['reveals']]

def note_of(it, T):
    """an item's note with the zone's name in it"""
    return it['note'].replace('{z}', T['zones'][it['nz']]['name']) if it['nz'] >= 0 else it['note']

def evaluate(m, T):
    given = set(rid for rid, cat, lv in m['given'] if lv); narr = set(m['narrative'])
    res = []
    for c in T['categories']:
        have = 0; apart = {'reach': [0, 0], 'promo': [0, 0]}; groups = [[0, 0] for g in c['groups']]; missing = []; others = []
        for it in c['items']:
            u = done_units(it, m, given, narr); top = it.get('units', 1)
            if it['kind']:
                apart[it['kind']][0] += u; apart[it['kind']][1] += top; others.append((it['name'], bool(u), it['kind'], note_of(it, T))); continue
            have += u
            if it['group'] >= 0: groups[it['group']][0] += u; groups[it['group']][1] += top
            if u < top: missing.append(it['name'] + (' (%s)' % note_of(it, T) if it['note'] else ''))
        res.append(dict(key=c['key'], label=c['label'], have=have, total=c['total'], pool=c.get('pool', 0), reach=apart['reach'][0], reach_total=apart['reach'][1],
                        promo=apart['promo'][0], promo_total=apart['promo'][1],
                        groups=[(g['name'], a, b) for g, (a, b) in zip(c['groups'], groups)], missing=missing, others=others))
    return res, bar(res)

REACH_POINTS = 1.0      # of the bar's 100: the share of the items the game cannot hand out as it is
def bar(res):
    """the bar of a list of category results: pct = what the save has of all that can be earned (0..100),
    shown = the number on the page (99 points for that, the last one for the 'reach' items)"""
    # the bar's equal parts: every category of its own, every pool of categories once
    pools = {}
    for r in res:
        if r.get('pool'): p = pools.setdefault(r['pool'], [0, 0]); p[0] += r['have']; p[1] += r['total']
    own = [r for r in res if not r.get('pool')]
    parts = len(own) + len(pools)
    pct = sum(100.0 / parts * r['have'] / r['total'] for r in own) + sum(100.0 / parts * a / b for a, b in pools.values())
    reach = sum(r['reach'] for r in res); reach_total = sum(r['reach_total'] for r in res); promo = sum(r['promo'] for r in res)
    last = REACH_POINTS if reach_total else 0.0
    if all(r['have'] >= r['total'] for r in res) and reach >= reach_total: shown = 100.0
    else: shown = min((100.0 - last) * pct / 100.0 + (last * reach / reach_total if reach_total else 0.0), 99.9)
    return dict(pct=pct, shown=shown, reach=reach, reach_total=reach_total, promo=promo)

if __name__ == '__main__':
    T = tables()
    if sys.argv[1] == '--coverage':
        nodes = {}
        for c in T['categories']:
            for it in c['items']:
                if it['test'] == 'node': nodes.setdefault(it['graph_id'], set()).add(it['hash'])
        forms = set(savemodel.fnv(f) for f in T['melee_forms'])
        unknown = {}; n = 0
        for f in sys.argv[2:]:
            try: m = savemodel.parse(open(f, 'rb').read())
            except savemodel.Bad: continue
            n += 1
            for gid, (a, ns) in m['graphs'].items():
                for x in ns:
                    if x not in nodes.get(gid, ()) : unknown.setdefault((gid, x), 0); unknown[(gid, x)] += 1
        print('saves read: %d | node ids no table item names: %d' % (n, len(unknown)))
        for (gid, x), k in sorted(unknown.items(), key=lambda v: -v[1])[:40]: print('   graph %08x node %08x in %d saves%s' % (gid, x, k, ' (a weapon form)' if x in forms else ''))
        sys.exit()
    m = savemodel.parse(open(sys.argv[1], 'rb').read())
    res, b = evaluate(m, T)
    if len(sys.argv) > 2 and sys.argv[2] == 'json':
        print(json.dumps(dict(pct=round(b['pct'], 3), shown=round(b['shown'], 3), reach=b['reach'], reach_total=b['reach_total'], promo=b['promo'],
                              cats=[dict(key=r['key'], have=r['have'], total=r['total'], reach=r['reach'], promo=r['promo']) for r in res])))
        sys.exit()
    for r in res:
        print('%-24s %4d / %-4d %5.1f%%' % (r['label'].encode('ascii', 'replace').decode(), r['have'], r['total'], 100.0 * r['have'] / r['total']))
        if '-v' in sys.argv:
            if r['groups']: print('      ' + ', '.join('%s %d/%d' % g for g in r['groups'])[:400])
            if r['missing'] and r['key'] in ('main', 'side', 'research', 'artifacts', 'outfits', 'abilities'): print('      missing: ' + '; '.join(r['missing'])[:600])
            if r['others']: print('      apart: ' + '; '.join('%s [%s]%s' % (n, k, ', have' if h else '') for n, h, k, w in r['others'])[:900])
    print('bar %.1f%% = 99%% of the %.1f%% earned + %d of the %d unobtainable items | promotion pieces owned: %d | world level %d' % (
          b['shown'], b['pct'], b['reach'], b['reach_total'], b['promo'], m['world_level']))
