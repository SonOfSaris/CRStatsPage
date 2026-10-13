# compare_all.py - runs out/tallytest.exe over every save chunk at hand and compares each category's count with the
# Python reference (tools/evalsave.py + tools/savemodel.py).   python compare_all.py [extra persi-global files...]
import glob, json, os, subprocess, sys
here = os.path.dirname(os.path.abspath(__file__)).replace('\\', '/') + '/'
sys.path.insert(0, here + '../tools')
import savemodel, evalsave
T = evalsave.tables()
known = set(T['known_rewards'])
root = here + '../../extracted_savegames/data/savegames'
files = sorted(glob.glob(root + '/**/*persi-global*', recursive=True)) + sys.argv[1:]
exe = here + 'out/tallytest.exe'
bad = 0; done = 0; best = (0, ''); headfail = 0; norewards = 0
for i in range(0, len(files), 40):
    part = files[i:i + 40]
    r = subprocess.run([exe, 'file'] + part, capture_output=True, text=True)
    lines = [json.loads(l) for l in r.stdout.splitlines() if l.startswith('{')]
    assert len(lines) == len(part), (len(lines), len(part), r.stderr[:300])
    for f, c in zip(part, lines):
        m = savemodel.parse(open(f, 'rb').read())
        res, b = evalsave.evaluate(m, T); pct = b['pct']
        want = [[x['have'], x['reach'], x['promo']] for x in res]
        done += 1
        if 'error' in c or c['cats'] != want or abs(c['pct'] - pct) > 0.002 or abs(c['shown'] - b['shown']) > 0.002 or c['reach'] != b['reach'] or c['promo'] != b['promo'] or c['level'] != m['world_level'] or bool(c['rewards']) != (m['rewards_at'] is not None):
            bad += 1
            if bad <= 5: print('DIFFERENT', f[-70:], c, want, round(pct, 3))
        headfail += not c.get('head', 0); norewards += not c.get('rewards', 0)
        if pct > best[0]: best = (pct, f.replace('\\', '/').split('/')[-3][:50] + '/' + os.path.basename(f))
print('saves compared: %d | different: %d | head not read: %d | no reward section: %d' % (done, bad, headfail, norewards))
print('highest bar: %.1f%% in %s' % best)
sys.exit(1 if bad else 0)
