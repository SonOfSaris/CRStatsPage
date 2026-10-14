# run_tests.py - the offline tests of StatsPage. Needs out/statscompletiontracker.dll (build.bat), tests/out/*.exe
# (build_tests.bat), node, the game's two UI files and a folder of saves.
#   python run_tests.py <folder with save files> [second save folder ...]
# 1. fakegame.exe: the DLL in a stand-in game (alone, behind another mod, without the handler in the exe)
# 2. the script that comes out equals what tools/gen_ui.py --apply makes and parses (node --check); the Options
#    page comes out as the game has it
# 3. the page's numbers equal the Python reference (tools/evalsave.py)
# 4. archive_test.js: the Archives script against a stand-in page
# 5. slots_test.py (needs a second save folder: a finished save first, a mid-story save second), and cmm_sim.py
#    where that file is at hand (it is not part of the repository)
# The game's two UI files are looked for in the folder STATSPAGE_UI names (default: ../extracted/ui_u1), under
# the names below.
import json, os, shutil, subprocess, sys
here = os.path.dirname(os.path.abspath(__file__)).replace('\\', '/') + '/'
src = here + '../'
sys.path.insert(0, src + 'tools')
import savemodel, evalsave
UI = os.environ.get('STATSPAGE_UI', src + '../extracted/ui_u1').replace('\\', '/').rstrip('/') + '/'
SCRIPT = UI + 'uiresources__game__ui__globals_island.bundle.js'
TEMPLATE = UI + 'uiresources__game__ui__optionscomponent.template.html'
work = here + 'out/work/'
fails = []
def check(ok, what):
    print('%s  %s' % ('PASS' if ok else 'FAIL', what))
    if not ok: fails.append(what)
def win(p): return os.path.normpath(p)
def stage(name, saves, more=''):
    d = work + name + '/'
    shutil.rmtree(d, ignore_errors=True); os.makedirs(d)
    shutil.copy(src + 'out/statscompletiontracker.dll', d)
    with open(d + 'statscompletiontracker_config.ini', 'w') as f: f.write('[General]\nEnabled=1\n[Page]\nMissingNames=0\n[Saves]\nFolder=%s\n' % win(saves) + more)
    return d
def run_host(name, saves, *flags):
    d = stage(name, saves, '[Archives]\nTrace=1\n' if '--trace' in flags else '')
    r = subprocess.run([here + 'out/fakegame.exe', win(d), win(SCRIPT), win(TEMPLATE), win(d + 'files')] + list(flags), capture_output=True, text=True, timeout=120)
    print('--- fakegame %s %s' % (name, ' '.join(flags)))
    for l in r.stdout.splitlines():
        if not l.startswith('PASS'): print('   ' + l)
    check(r.returncode == 0 and 'ALL PASSED' in r.stdout, 'stand-in game, %s: %d checks' % (name, r.stdout.count('PASS')))
    return d

saves = sys.argv[1]
d = run_host('alone', saves)
d2 = run_host('behind', saves, '--mod-first')
run_host('nopattern', saves, '--no-pattern')
run_host('traced', saves, '--trace')

# what tools/gen_ui.py makes of the same script
ref = work + 'ref'
r = subprocess.run([sys.executable, src + 'tools/gen_ui.py', '--apply', SCRIPT, ref], capture_output=True, text=True)
check(r.returncode == 0, 'gen_ui.py --apply: ' + r.stdout.strip())
rb = lambda p: open(p, 'rb').read()
check(rb(d + 'files/script.js') == rb(ref + '/globals_island.bundle.js'), 'the DLL\'s script equals the reference, byte for byte')
check(rb(d + 'files/template.html') == rb(TEMPLATE), 'the Options page goes out as the game has it, byte for byte')
# behind another mod: the Archives script behind that mod's note
s2 = rb(d2 + 'files/script.js'); t2 = rb(d2 + 'files/template.html')
archive = json.load(open(src + 'tools/ui.json', encoding='utf-8'))['archive'].encode('utf-8')
plain = rb(ref + '/globals_island.bundle.js')
check(plain == rb(SCRIPT) + archive, 'the reference is the game\'s script with the Archives script behind it (%d bytes)' % len(archive))
check(s2 == rb(SCRIPT) + b'/*other mod*/' + archive and t2 == rb(TEMPLATE) + b'<!--other mod-->', 'behind another mod the addition is the same and its notes are kept')
r = subprocess.run(['node', '--check', win(d + 'files/script.js')], capture_output=True, text=True, shell=True)
check(r.returncode == 0, 'the edited script parses (node --check)' + (': ' + r.stderr[:300] if r.returncode else ''))

# the page's numbers against the reference
T = evalsave.tables()
def newest(folder):
    best = None
    for f in os.listdir(folder):
        if not f.endswith('-persi-global') or not f.startswith('slot-'): continue
        try: h = savemodel.header(open(os.path.join(folder, f[:-len('-persi-global')] + '-header'), 'rb').read())
        except OSError: continue
        if best is None or (h['saves'], h['time']) > (best[0]['saves'], best[0]['time']): best = (h, f)
    return best
def compare(folder, page, label):
    h, f = newest(folder)
    m = savemodel.parse(open(os.path.join(folder, f), 'rb').read())
    res, b = evalsave.evaluate(m, T); pct = b['pct']
    want = [dict(key=x['key'], have=x['have'], total=x['total'], reach=x['reach'], promo=x['promo']) for x in res]
    check(page['cats'] == want and abs(page['pct'] - pct) < 0.002 and abs(page['shown'] - b['shown']) < 0.002 and
          (page['reach'], page['reachTotal'], page['promo']) == (b['reach'], b['reach_total'], b['promo']),
          '%s: the page shows %s as the reference counts it (%.1f%% of what can be earned, %d of %d unobtainable items, %d promotion pieces)' % (label, f, pct, b['reach'], b['reach_total'], b['promo']))
    rows = {r['l']: r for r in page['rows']}
    check(rows['Completion']['v'] == '%.1f%%' % b['shown'] and abs(b['shown'] - (0.99 * pct + 1.0 * b['reach'] / b['reach_total'])) < 0.002 and len(page['rows']) == 25,
          '%s: 25 rows, the bar reads %s: 99%% of the %.1f%% earned plus %d of %d for the last 1%%' % (label, rows['Completion']['v'], pct, b['reach'], b['reach_total']))
    check(rows['Completion']['about'] == 'Overall game completion, including collectibles and enemy encounters.' and
          not any('playthrough' in l and 'New Game Plus' not in l for l in rows['Completion']['d']['lines']), '%s: the bar\'s description is the written one; nothing about earlier playthroughs' % label)
    pr = rows['Promotional Outfits']
    check(pr['v'] == str(b['promo']) and pr.get('count') == 1 and pr['f'] == 0 and len(pr['d']['owned']) == b['promo'] and ' / ' not in pr['v'],
          '%s: the promotion pieces are a plain count (%s owned), no total, no part of the bar' % (label, pr['v']))
    check('Played %d h %02d min' % (h['play'] // 3600, h['play'] // 60 % 60) in rows['Completion']['d']['lines'], '%s: the play time of the save header is shown' % label)
    return res, rows
# what the page holds back while a game is in progress: story totals, zones not visited, names of locked things
def expect_abilities(m, hide=True):
    """the names the page should give the missing abilities: ??? until the selection an ability belongs to is open"""
    given = set(rid for rid, cat, lv in m['given'] if lv); narr = set(m['narrative'])
    rev = evalsave.revealed(m, T)
    c = [c for c in T['categories'] if c['key'] == 'abilities'][0]
    return sorted(it['name'] if not hide or (it['rv'] >= 0 and rev[it['rv']]) else '???' for it in c['items'] if not evalsave.done_units(it, m, given, narr))
def main_done(d, ref):
    """the names Main Story Missions lists as done, and the ones the reference has as done (in the tables' order, the order of the story)"""
    miss = [x.split(' (')[0] for x in ref['missing']]
    return [n for k, n, note in d.get('done', [])], [i['name'] for i in [c for c in T['categories'] if c['key'] == 'main'][0]['items'] if i['name'] not in miss]
def folder_page(folder, *args):
    r = subprocess.run([here + 'out/tallytest.exe', 'folder', win(folder)] + [str(a) for a in args], capture_output=True)
    p = json.loads(r.stdout.decode('utf-8')); return p, {x['k']: x for x in p['rows']}
def held_back(folder, label):
    h, f = newest(folder)
    m = savemodel.parse(open(os.path.join(folder, f), 'rb').read())
    res, b = evalsave.evaluate(m, T); by = {x['key']: x for x in res}
    known = evalsave.known_zones(m, T); zn = [z['name'] for z in T['zones']]
    hidden = [z for z, k in zip(zn, known) if not k]
    p, rows = folder_page(folder, 0)                                   # the mod's defaults
    story = [k for k in ('main', 'side') if by[k]['have'] < by[k]['total']]
    check(len(story) == 2 and all(rows[k]['v'] == '%d / ??' % by[k]['have'] and 'still to go' not in json.dumps(rows[k]) and 'missing' not in rows[k]['d'] for k in story),
          '%s: unfinished story rows hold their total back: %s' % (label, ', '.join('%s %s' % (rows[k]['l'], rows[k]['v']) for k in story)))
    # the side stories in parts while a game is in progress
    stories = [k for k in ('freeze', 'taxi', 'dog', 'mapkits', 'science', 'pope', 'speakers', 'sketch')]
    unfound = [k for k in stories if by[k]['have'] == 0]; found = [k for k in stories if by[k]['have'] > 0]
    check(unfound and found and all(rows[k]['l'] == '???' and rows[k]['v'] == '0 / ??' and rows[k]['about'] == 'A story you have not found yet.' and rows[k]['d'] == {} and rows[k]['f'] == 0 and
                                    rows[k]['w'] == [0, by[k]['total']] for k in unfound) and
          all(rows[k]['l'] == by[k]['label'] and rows[k]['v'] == '%d / %d' % (by[k]['have'], by[k]['total']) for k in found),
          '%s: a side story in parts with nothing found reads ??? (%d of them), the others show their name and count: %s' % (label, len(unfound), ', '.join('%s %s' % (rows[k]['l'], rows[k]['v']) for k in found)))
    texts = [(g[1], g[5]) for k in found for g in rows[k]['d']['groups'] if len(g) == 6]
    check(any(t == 'Found' for n, t in texts) and any(t == 'Not found' for n, t in texts) and not any(n == 'Ending' and t != 'Done' for n, t in texts) and
          all(not (len(g) == 5 and g[4]) or g[1] == '???' for k in found for g in rows[k]['d']['groups']) and
          all([len(g) == 5 for g in rows[k]['d']['groups']] == sorted(len(g) == 5 for g in rows[k]['d']['groups']) for k in found) and all(z not in [g[1] for g in rows[k]['d']['groups']] for k in found for z in hidden),
          '%s: their lines read Found / Not found per zone; a zone not visited is ??? there too, below every other line' % label)
    # an ending that is not done is inside the ???: no line of its own, and one ??? line where no zone reads ???
    und = [k for k in found if by[k]['groups'][-1][0] == 'Ending' and by[k]['groups'][-1][1] < by[k]['groups'][-1][2]]
    def marks(rr, k): return sum(1 for g in rr[k]['d']['groups'] if len(g) == 5 and g[4])
    def zone_marks(k): return sum(1 for name, a, b in by[k]['groups'] if name in hidden and a == 0)
    check(und and all('Ending' not in [g[1] for g in rows[k]['d']['groups']] and marks(rows, k) == max(zone_marks(k), 1) and rows[k]['d']['groups'][-1][1] == '???' for k in und) and
          all(marks(rows, k) == zone_marks(k) for k in found if k not in und) and all(rows[k]['v'] == '%d / %d' % (by[k]['have'], by[k]['total']) for k in und),
          '%s: an ending not done has no line, it is inside the ??? of its list (%s); the count still holds it' % (label, ', '.join(rows[k]['l'] for k in und)))
    check(rows['side']['d'].get('hidden') == 'Undiscovered side stories will be listed here after completing the main story.' and 'hidden' not in rows['main']['d'],
          '%s: Side Stories says when its missing ones will be listed; Main Story Missions has no sentence' % label)
    got_main, want_main = main_done(rows['main']['d'], by['main'])
    check(got_main == want_main and 0 < len(got_main) == by['main']['have'] < 26 and list(rows['main']['d']) == ['done'] and got_main[0] == 'Orientation' and
          all(k and note == '' for k, n, note in rows['main']['d']['done']),
          '%s: Main Story Missions lists the %d completed missions in the story\'s order and nothing about the rest: %s ... %s' % (label, len(got_main), got_main[0], got_main[-1]))
    got_done, want_done = side_done(rows['side']['d'], by['side'])
    check(got_done == want_done and 0 < len(got_done) == by['side']['have'] < 12 and 'missing' not in rows['side']['d'] and list(rows['side']['d']) == ['done', 'hidden'],
          '%s: Side Stories lists the %d completed ones by name, above its sentence about the rest: %s' % (label, len(got_done), ', '.join(got_done)))
    pn, rn = folder_page(folder, 1)                                    # names always
    check(all(rn[k]['d']['groups'][-1 - marks(rn, k)][1:] == ['Ending', 0, 1, 0, 'Not done'] and marks(rn, k) == zone_marks(k) for k in und),
          '%s: with names always shown the ending has its line again: Ending, Not done' % label)
    got_done, want_done = side_done(rn['side']['d'], by['side'])
    check(got_done == want_done and list(rn['side']['d'])[:2] == ['done', 'missing'] and not set(got_done) & set(n for k, n, note in rn['side']['d']['missing']) and
          len(got_done) + len(rn['side']['d']['missing']) == 12, '%s: with names always shown it lists the completed ones and the missing ones, 12 together' % label)
    miss_n = [n for k, n, note in rn['side']['d']['missing']]
    check(len(got_done) > 1 and len(miss_n) > 1 and got_done == sorted(got_done, key=str.lower) and miss_n == sorted(miss_n, key=str.lower),
          '%s: both lists are in alphabetical order (completed: %s; missing: %s)' % (label, ', '.join(got_done), ', '.join(miss_n)))
    check(all(rn[k]['l'] == by[k]['label'] and rn[k]['v'] == '0 / %d' % by[k]['total'] and rn[k]['about'] != 'A story you have not found yet.' for k in unfound),
          '%s: with names always shown such a story has its name and its total' % label)
    groups = rows['levels']['d']['groups']
    want = [z for z, k in zip(zn, known) if k] + ['???'] * known.count(False)
    check(hidden and [g[1] for g in groups] == want and all(len(g) == 4 for g in groups[:known.count(True)]) and all(g[0] == '' and g[2:] == [0, 0, 1] for g in groups[known.count(True):]),
          '%s: a zone not visited yet reads ??? without a count, at the bottom of the list: %s' % (label, ', '.join(want)))
    check(all(z not in [g[1] for g in rows[key]['d']['groups']] for key in ('tasks', 'feats', 'shum', 'encounters', 'vending', 'levels') for z in hidden) and
          all(any(g[1] == '???' for g in rows[key]['d']['groups']) for key in ('tasks', 'feats', 'encounters', 'levels')),
          '%s: so in every list by zone (%s is named in none)' % (label, ', '.join(hidden)))
    # the SHUM machine of the apartment: a place of its own that is held back until the main story is done
    apt = 'Unknown (Apartment)'; sref = dict((n, a) for n, a, b in by['shum']['groups'])
    sgn = [g[1] for g in rows['shum']['d']['groups']]
    p0, r0 = folder_page(folder, 0, 0, 0, 0); p2, r2 = folder_page(folder, 0, 1, 2, 1)
    check(by['main']['have'] < by['main']['total'] and sref[apt] == 0 and apt not in sgn and sgn[-1] == '???' and
          sgn.count('???') == 1 + sum(1 for n, a, b in by['shum']['groups'] if n in hidden and a == 0) and
          r0['shum']['d']['groups'][-1] == ['', apt, 0, 3, 0, 'Need higher score'] and '???' not in [g[1] for g in r0['shum']['d']['groups']] and
          all(g[1] not in (apt, '???') for g in r2['shum']['d']['groups']) and rows['shum']['v'] == '%d / %d' % (by['shum']['have'], by['shum']['total']),
          '%s: the SH\u00dcM machine of the apartment reads ??? at the bottom until the main story is done (%s); zones setting 0 names it, 2 leaves it out' % (label, ', '.join(sgn)))
    story_shown = set(by[k]['label'] for k in stories if by[k]['have'] > 0)
    def held(ref_missing):
        """the notes of the reference's missing things as the page must show them: a zone not visited reads ???, and so does a side story in parts not found yet"""
        out = []
        for x in ref_missing:
            w = x.split(' (')[1][:-1]
            for z in hidden:
                if w.startswith(z + ' '): w = '???' + w[len(z):]
            if w.startswith('Side story: ') and w[12:] not in story_shown: w = 'Side story: ???'
            out.append(w)
        return sorted(out)
    want_notes = held(by['artifacts']['missing'])
    got = rows['artifacts']['d']['missing']
    check(sorted(note for k, n, note in got) == want_notes and any(w.startswith('??? zone level') for w in want_notes) and any(w.startswith('??? main mission') for w in want_notes) and
          any(w.startswith('Side story: ') and w != 'Side story: ???' for w in want_notes) and all(n == '???' and note for k, n, note in got),
          '%s: missing Artifacts read ???, each says where it comes from, and a zone not visited is held back there: %s' % (label, '; '.join(sorted(set(want_notes)))[:260]))
    check(not any((z + ' ' + what) in json.dumps(p) for z in hidden for what in ('zone level', 'main mission', 'side story', 'SH\u00dcM', 'world encounter', 'FBC test facility')),
          '%s: nor do the notes of other rows name such a zone' % label)
    clothes = rows['outfits']['d']['missing']
    check(len(clothes) == len(by['outfits']['missing']) and all(n == '???' and note for k, n, note in clothes) and sorted(note for k, n, note in clothes) == held(by['outfits']['missing']),
          '%s: the %d locked clothes read ???, each with where it comes from: %s' % (label, len(clothes), '; '.join(sorted(set(note for k, n, note in clothes)))[:200]))
    want_ab = expect_abilities(m)
    got_ab = [n for k, n, note in rows['abilities']['d']['missing']]
    check(sorted(got_ab) == want_ab and '???' in want_ab and any(n != '???' for n in want_ab) and
          sorted(note for k, n, note in rows['abilities']['d']['missing']) == held(by['abilities']['missing']) and all(note for k, n, note in rows['abilities']['d']['missing']),
          '%s: a missing ability is named once its selection is open, the others read ???; each says where it comes from: %s' % (label, ', '.join('%s (%s)' % (n, note) for k, n, note in rows['abilities']['d']['missing'])))
    p, rows = folder_page(folder, 0, 0, 0, 0)                          # the three settings off
    check(all(rows[k]['v'] == '%d / %d' % (by[k]['have'], by[k]['total']) for k in ('main', 'side')) and [g[1] for g in rows['levels']['d']['groups']] == zn and
          sorted(n for k, n, note in rows['artifacts']['d']['missing']) == sorted(x.split(' (')[0] for x in by['artifacts']['missing']) and '???' not in json.dumps([r for r in p['rows'] if r['k'] not in unfound + und]) and all(rows[k]['l'] == '???' for k in unfound) and
          all(marks(rows, k) == 1 and rows[k]['d']['groups'][-1] == ['', '???', 0, 0, 1] and 'Ending' not in [g[1] for g in rows[k]['d']['groups']] for k in und) and
          sorted(n for k, n, note in rows['outfits']['d']['missing']) == sorted(x.split(' (')[0] for x in by['outfits']['missing']) and sorted(n for k, n, note in rows['abilities']['d']['missing']) == expect_abilities(m, False) and
          'hidden' not in rows['main']['d'] and main_done(rows['main']['d'], by['main'])[0] == want_main, '%s: with the three settings off everything is counted and named in full (a side story in parts not found yet and an ending not done follow the names setting and stay ???, the ending as one line of its own)' % label)
    p, rows = folder_page(folder, 0, 1, 2, 1)                          # zones not visited left out
    check([g[1] for g in rows['levels']['d']['groups']] == [z for z, k in zip(zn, known) if k] and all(len(g) == 4 for key in ('tasks', 'feats', 'levels') for g in rows[key]['d']['groups']),
          '%s: zones setting 2 leaves the zones not visited out of the lists (%d of %d shown)' % (label, len(rows['levels']['d']['groups']), len(zn)))
    p, rows = folder_page(folder, 1)                                   # names always: the missing story things are listed, the total still reads ??
    check(all(rows[k]['v'] == '%d / ??' % by[k]['have'] and len(rows[k]['d']['missing']) == by[k]['total'] - by[k]['have'] for k in story), '%s: with names always shown the story rows list what is missing and still read ?? for the total' % label)

page = json.loads(rb(d + 'files/page.json').decode('utf-8'))
res, rows = compare(saves, page, 'first folder')
by = {x['key']: x for x in res}
miss = [n for k, n, note in rows['Clothes']['d'].get('missing', [])]
clothes = rows['Clothes']['d'].get('missing', [])
check(len(clothes) == len(by['outfits']['missing']) and all(n == '???' and k == '' for k, n, note in clothes) and
      sorted(note for k, n, note in clothes if note) == sorted(x.split(' (')[1][:-1] for x in by['outfits']['missing'] if ' (' in x),
      'the %d locked clothes read ??? and keep where they come from: %s' % (len(clothes), '; '.join(note for k, n, note in clothes if note)[:120]))
abil = [n for k, n, note in rows['Abilities']['d'].get('missing', [])]
check(abil and sorted(abil) == sorted(x.split(' (')[0] for x in by['abilities']['missing']) and '???' not in abil and
      sorted(note for k, n, note in rows['Abilities']['d']['missing']) == sorted(x.split(' (')[1][:-1] for x in by['abilities']['missing']),
      'every selection is open in this save, so its missing abilities are named, each with where it comes from: %s' % ', '.join('%s (%s)' % (n, note) for k, n, note in rows['Abilities']['d']['missing']))
arts = rows['Artifacts']['d'].get('missing', [])
notes = sorted(note for k, n, note in arts if note)
check(len(arts) == len(by['artifacts']['missing']) and all(n == '???' and k == '' for k, n, note in arts) and notes == sorted(x.split(' (')[1][:-1] for x in by['artifacts']['missing'] if ' (' in x) and
      'Side story: Every Dog Has Her Day' in notes and 'Source unconfirmed' in notes and by['artifacts']['total'] == 42 and all(note for k, n, note in arts) and '1.4.2' not in json.dumps(page),
      'the %d missing Artifacts (of 42) read ??? and each says where it comes from, with no game version in it: %s' % (len(arts), '; '.join(notes)[:260]))
# every thing of the Dylan tab has a note, and the notes hold back what the page holds back
for key in ('artifacts', 'outfits', 'abilities'):
    its = [i for i in [c for c in T['categories'] if c['key'] == key][0]['items'] if not i['kind']]
    check(all(i['note'] and '(' not in i['note'] and len(evalsave.note_of(i, T)) <= 38 and not i['note'][0].islower() for i in its),
          'tables: each of the %d %s has a note of at most 38 characters that starts with a capital letter: %s' % (len(its), key, '; '.join(sorted(set(i['note'] for i in its)))[:230]))
r = subprocess.run([here + 'out/tallytest.exe', 'synthetic', '100000', '0', '0'], capture_output=True)
none = {x['k']: x for x in json.loads(r.stdout.decode('utf-8'))['rows']}
nn = [note for k, n, note in none['artifacts']['d']['missing']] + [note for k, n, note in none['outfits']['d']['missing']] + [note for k, n, note in none['abilities']['d']['missing']]
check(nn.count('Side story: ???') == 6 and not any(c['label'] in ' '.join(nn) for c in T['categories'] if c.get('pool')) and nn.count('Defeat Resonant entities') == 12 and 'Every Sketch completed' in nn,
      'with nothing found the notes name no side story in parts: six rewards read "Side story: ???"; twelve abilities read "Defeat Resonant entities"')
got_main, want_main = main_done(rows['Main Story Missions']['d'], by['main'])
check(got_main == want_main and len(got_main) == 26 and list(rows['Main Story Missions']['d']) == ['done'] and got_main[0] == 'Orientation',
      'a finished story: Main Story Missions lists all 26 missions in the story\'s order, from %s to %s' % (got_main[0], got_main[-1]))
PLACES = {3: '1st Place', 2: '2nd Place', 1: '3rd Place', 0: 'Need higher score'}
sg = rows['SH\u00dcM Scores']['d']['groups']
check(sg and all(len(g) == 6 and g[3] == 3 and g[4] == 0 and g[5] == PLACES[g[2]] for g in sg) and [(g[1], g[2]) for g in sg] == [(n, a) for n, a, b in by['shum']['groups']],
      'each SH\u00dcM machine reads its place, from the reward tiers handed out: %s' % ', '.join('%s %s' % (g[1], g[5]) for g in sg))
check(sg[-1][:2] == ['', 'Unknown (Apartment)'] and by['main']['have'] == by['main']['total'] and '???' not in [g[1] for g in sg],
      'the machine of the apartment is named once the main story is done, under its own label, last in the list: %s' % sg[-1][5])
# the six side stories in parts: a row each with its own title and description, together one part of the bar
PARTS = [('freeze', 'Freeze Frame', 15, 'Close the shutters on altered cameras throughout Manhattan.'),
         ('taxi', 'The Last Taxi', 7, 'Solve the puzzle behind the paranatural taxis around Manhattan.'),
         ('dog', 'Every Dog Has Her Day', 8, 'Help a lost dog find her toy.'),
         ('mapkits', 'Mapping the New New York', 7, 'Activate FBC Map Kits across patterned Manhattan.'),
         ('science', 'Unsupervised Science', 7, "Shut down the FBC's hidden test facilities."),
         ('pope', "Pope's Research", 8, "Activate Pope's resonance monitoring balloons in the Unknown."),
         ('speakers', 'The Complete Set', 6, 'Collect the pieces of an altered speaker set around Manhattan.'),
         ('sketch', "Dylan's Sketchbook", 11, 'Set up the scenes Dylan wants to sketch around Manhattan.')]
bk = {r['k']: r for r in page['rows']}
check(all(bk[k]['l'] == t and bk[k]['about'] == a and by[k]['total'] == n and bk[k]['v'] == '%d / %d' % (by[k]['have'], n) and bk[k]['pool'] == 1 and bk[k]['w'] == [by[k]['have'], n]
          for k, t, n, a in PARTS) and sum(n for k, t, n, a in PARTS) == 69,
      'the eight side stories in parts have a row each, with their titles and descriptions: %s' % ', '.join('%s %s' % (bk[k]['l'], bk[k]['v']) for k, t, n, a in PARTS))
own = [x for x in res if x['key'] not in [k for k, t, n, a in PARTS]]
ph = sum(by[k]['have'] for k, t, n, a in PARTS)
check(len(own) == 15 and by['side']['total'] == 12 and not set(n for k, n, note in rows['Side Stories']['d'].get('missing', [])) & set(t for k, t, n, a in PARTS) and
      abs(page['pct'] - 100.0 / 16 * (sum(x['have'] / x['total'] for x in own) + ph / 69.0)) < 0.002,
      'Side Stories counts the other 12; the bar has 16 equal parts, the eight stories together being one (%d of 69)' % ph)
side_items = [c for c in T['categories'] if c['key'] == 'side'][0]['items']
def side_done(d, ref):
    """the names Side Stories lists as done, and the ones the reference has as done (in the tables' order)"""
    miss = [x.split(' (')[0] for x in ref['missing']]
    return [n for k, n, note in d.get('done', [])], [i['name'] for i in side_items if i['name'] not in miss]
got_done, want_done = side_done(rows['Side Stories']['d'], by['side'])
check(got_done == want_done and len(got_done) == by['side']['have'] > 0 and all(k and note == '' for k, n, note in rows['Side Stories']['d']['done']),
      'Side Stories names the ones that are done, each with its text key: %d of 12' % len(got_done))
check(got_done == sorted(got_done, key=str.lower) and [i['name'] for i in side_items] == sorted((i['name'] for i in side_items), key=str.lower) and got_done[0] == 'A New Directive',
      'Side Stories lists its names in alphabetical order: %s' % ', '.join(got_done))
check([z['name'] for z in T['zones']] == ['Downtown', 'Central', 'Evacuation Zone', 'West Incursion Zone', 'The Park', 'Underpass', 'Unknown'] and
      [g[1] for g in rows['Zone Levels']['d']['groups']] == [z['name'] for z in T['zones']] and
      got_done == ['A New Directive', 'Carbon Copy', 'Carbon Copy: Part II', 'Carbon Copy: Part III', 'Into the Valley of Death', 'Maintenance Protocol', 'New Growth',
                   'Notes: Defeat the Resonant Entities', 'Panopticon Service Request', "The Party Won't Stop", 'Urban Survival', "Zoe's Briefing"],
      'zones are listed in the order a player comes to them (%s), side stories by name' % ', '.join(z['name'] for z in T['zones']))
def story_lines(row, ref):
    """a story row's lines against the reference's groups: a zone with one part reads found or not, a zone with several its
    count, the ending done or not"""
    got = [g for g in row['d']['groups'] if not (len(g) == 5 and g[4])]
    want = []
    for name, a, b in ref:
        if name == 'Ending': want.append(['', 'Ending', a, b, 0, 'Done' if a >= b else 'Not done'])
        elif b == 1: want.append([name, a, b, 'Found' if a else 'Not found'])
        else: want.append([name, a, b, None])
    return [[g[1], g[2], g[3], g[5] if len(g) > 5 else None] if g[1] != 'Ending' else g for g in got], want
ok = True
for k, t, n, a in PARTS:
    got, want = story_lines(bk[k], by[k]['groups']); ok = ok and got == want
check(ok and [g[1] for g in bk['freeze']['d']['groups']][-1] == 'Ending' and bk['freeze']['d']['groups'][0][2:4] == [2, 2] and len(bk['freeze']['d']['groups'][0]) == 4 and
      [g[1] for g in bk['taxi']['d']['groups']] == [z['name'] for z in T['zones']] and [g[1] for g in bk['pope']['d']['groups']] == ['Unknown', 'Ending'] and
      [g[1] for g in bk['speakers']['d']['groups']] == ['Downtown', 'Central', 'West Incursion Zone', 'Oldest House', 'Ending'] and bk['speakers']['d']['groups'][1][2:] == [2, 2] and
      bk['speakers']['d']['groups'][3][0] == 'MAP_SIGNAGE_DOWNTOWN_OLDEST_HOUSE' and bk['speakers']['d']['groups'][3][5] == 'Found' and bk['speakers']['d']['groups'][4][5] == 'Done',
      'their lines: a zone reads Found, the cameras 2 / 2 per zone, an ending only where the story has a step of its own (%s)' % ', '.join(g[1] + ' ' + (g[5] if len(g) > 5 else '%d / %d' % (g[2], g[3])) for g in bk['dog']['d']['groups']))
want_labels = ['Completion', 'Main Story Missions', 'Side Stories', 'Mapping the New New York', "Dylan's Sketchbook", 'Unsupervised Science', 'Freeze Frame', 'The Last Taxi', 'Every Dog Has Her Day', 'The Complete Set', "Pope's Research",
               'Zone Levels', 'World Encounters', 'Hiss Vending Machines', 'World Feats', 'SH\u00dcM Scores', 'FBC Tasks', 'Collectibles', 'Enemy Research',
               'Artifacts', 'Clothes', 'Abilities', 'Talents', 'Weapon Upgrades', 'Promotional Outfits']
check([r['l'] for r in page['rows']] == want_labels, 'the titles are in title case, outfit pieces are Clothes')
abouts = {r['l']: r['about'] for r in page['rows']}
check(abouts['FBC Tasks'] == 'FBC assignments from the task boards.' and abouts['SH\u00dcM Scores'] == 'High scores on SH\u00dcM machines. Earn more points playing SH\u00dcM to place higher. Higher scores unlock more rewards up to 1st Place.' and
      abouts['Promotional Outfits'] == 'Clothing unlocked as part of an out-of-game promotional event. Does not count toward completion.' and abouts['Weapon Upgrades'] == 'Weapon forms and their upgrades.',
      'the descriptions are the new texts')
check(page['reachTotal'] == 1 and page['reach'] == 0 and 'K9 Frequency' not in json.dumps(page) and 'nobtainable' not in json.dumps(page) and 'K9 Frequency Receiver' not in miss,
      'the last 1% is one item (the K9 Frequency Receiver in the tables); the answer has no row for it and does not name it')
check('Water Pistol' not in json.dumps(page) and all(it['name'] != 'Water Pistol' for c in T['categories'] for it in c['items']), 'the Water Pistol is tracked nowhere: not in the tables, not in the answer')
owned = [n for k, n, note in rows['Promotional Outfits']['d']['owned']]
check(len(owned) == sum(x['promo'] for x in res) and not set(owned) & set(miss) and sum(x['promo_total'] for x in res) == 17, 'the promotion pieces owned are named (%d of the 17 there are; the 17 is shown nowhere): %s' % (len(owned), ', '.join(owned)[:90]))
check(not os.path.exists(d + 'statspage_memory.txt') and not os.path.exists(d + 'statspage_memory.txt.new'), 'no record file is written next to the DLL')
for i, extra in enumerate(sys.argv[2:]):
    dd = stage('folder%d' % i, extra)
    r = subprocess.run([here + 'out/tallytest.exe', 'folder', win(extra), '0'], capture_output=True)
    compare(extra, json.loads(r.stdout.decode('utf-8')), 'folder %d' % (i + 2))
    if i == 0: held_back(extra, 'folder 2')

# the Archives script against a stand-in page and a stand-in game with three tabs
r = subprocess.run(['node', win(here + 'archive_test.js'), win(src + 'ui/statspage_archive.js'), win(d + 'files/page.json')], capture_output=True, text=True, shell=True)
for l in r.stdout.splitlines():
    if not l.startswith('PASS'): print('   ' + l)
check(r.returncode == 0, 'Archives script: %d checks%s' % (r.stdout.count('PASS'), (' | ' + r.stderr[:400]) if r.returncode else ''))

# the bar's last point and the promotion pieces (made-up states)
def synthetic(missing, reach, promo):
    r = subprocess.run([here + 'out/tallytest.exe', 'synthetic', str(missing), str(reach), str(promo)], capture_output=True)
    p = json.loads(r.stdout.decode('utf-8')); return p, p['rows'][0]
p, top = synthetic(0, 0, 0)
check(top['v'] == '99.0%' and abs(top['f'] - 0.99) < 1e-9 and p['shown'] == 99 and p['pct'] == 100, 'everything that can be earned, nothing else: 99.0%')
p, top = synthetic(0, 1, 0)
check(top['v'] == '100.0%' and top['f'] == 1 and p['shown'] == 100 and p['reachTotal'] == 1 and p['reach'] == 1, 'plus the one unobtainable item: 100.0%, the most the bar can read')
q, qtop = synthetic(0, 999, 999)
check(qtop['v'] == '100.0%' and q['shown'] == 100 and q['promo'] == 17 and [r for r in q['rows'] if r['k'] == 'promo'][0]['v'] == '17', 'with every promotion piece on top: still 100.0%, and their row reads 17')
q, qtop = synthetic(0, 0, 999)
check(qtop['v'] == '99.0%' and q['shown'] == 99, 'promotion pieces alone move nothing: 99.0%')
p, top = synthetic(1, 999, 999)
check(p['shown'] < 100 and abs(p['shown'] - min(0.99 * p['pct'] + 1, 99.9)) < 0.002 and not top['v'].startswith('100') and top['f'] < 1,
      'one thing short, the unobtainable item owned: %s' % top['v'])
check(float(top['v'].rstrip('%')) < 100, 'short of everything the bar never reads 100.0 (%s for %.3f)' % (top['v'], 0.99 * p['pct'] + 1))
p, top = synthetic(400, 0, 5)
check(abs(p['shown'] - 0.99 * p['pct']) < 0.002 and top['v'] == '%.1f%%' % (0.99 * p['pct']), 'a game in progress: %s is 99%% of the %.1f%% earned' % (top['v'], p['pct']))

# the store route: the same folder through a stand-in steam_api64.dll whose interface is not there at the first asking
env = dict(os.environ, FAKESTEAM_DIR=win(saves), FAKESTEAM_LATE='1')
shutil.rmtree(work + 'steam', ignore_errors=True); os.makedirs(work + 'steam')
r = subprocess.run([here + 'out/tallytest.exe', 'steam', win(here + 'out/steam/steam_api64.dll'), '2'], capture_output=True, env=env)
answers = [json.loads(l) for l in r.stdout.decode('utf-8').splitlines() if l.startswith('{')]
check(len(answers) == 2 and 'note' in answers[0] and answers[0]['rows'][1]['v'] == '-', 'store library not started yet: dashes and a note, no numbers made up')
if len(answers) == 2: compare(saves, answers[1], 'through the store library')

# which save is shown and what New Game Plus does to the bar (made-up saves), and the page next to Mod Settings Menu (an imitation of its edits)
extra = [('slots_test.py', sys.argv[1:3], 'which save is shown, New Game Plus')] if len(sys.argv) > 2 else []
if os.path.isfile(here + 'cmm_sim.py'): extra.append(('cmm_sim.py', sys.argv[1:2], 'next to Mod Settings Menu, both orders'))
for script, args, label in extra:
    r = subprocess.run([sys.executable, here + script] + args, capture_output=True, text=True)
    for l in r.stdout.splitlines():
        if not l.startswith('PASS'): print('   ' + l)
    check(r.returncode == 0, '%s: %d checks%s' % (label, r.stdout.count('PASS'), (' | ' + r.stderr[-300:]) if r.returncode else ''))

print('\n%s (%d failed)' % ('ALL PASSED' if not fails else 'FAILED', len(fails)))
sys.exit(1 if fails else 0)
