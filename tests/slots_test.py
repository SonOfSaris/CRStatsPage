# slots_test.py - which save the page shows and what New Game Plus does to the bar, with made-up saves: a finished
# save and a mid-story save of the same game, dressed up as different slots and playthroughs by changing their
# header chunks. The bar is the shown save's own: nothing is kept from another save or an earlier playthrough.
#   python slots_test.py <folder with a finished save> <folder with a mid-story save>
import json, os, shutil, struct, subprocess, sys, zlib
here = os.path.dirname(os.path.abspath(__file__)).replace(chr(92), '/') + '/'
sys.path.insert(0, here + '../tools')
import savemodel, evalsave
T = evalsave.tables()
work = here + 'out/work/slots/'
fails = []
def check(ok, what):
    print('%s  %s' % ('PASS' if ok else 'FAIL', what))
    if not ok: fails.append(what)

def newest(folder):
    best = None
    for f in os.listdir(folder):
        if not f.endswith('-header') or not f.startswith('slot-'): continue
        h = savemodel.header(open(os.path.join(folder, f), 'rb').read())
        if best is None or h['saves'] > best[0]: best = (h['saves'], f[:-len('-header')])
    return os.path.join(folder, best[1])

def cats(base):
    """what the reference counts in a save, in the shape of the page's answer"""
    m = savemodel.parse(open(base + '-persi-global', 'rb').read())
    res, b = evalsave.evaluate(m, T)
    return [dict(key=x['key'], have=x['have'], total=x['total'], reach=x['reach'], promo=x['promo']) for x in res], b

def put(folder, src, slot, playthrough, saves, time):
    """a copy of the save <src> as the save of <slot>, with a header that says <playthrough>, <saves> and <time>"""
    os.makedirs(folder, exist_ok=True)
    name = 'slot-%d_auto-1' % slot
    for part in ('persi-global', 'player', 'bundle-container'):
        if os.path.isfile(src + '-' + part): shutil.copy(src + '-' + part, os.path.join(folder, name + '-' + part))
    h = bytearray(open(src + '-header', 'rb').read())
    struct.pack_into('<Q', h, 20, time); struct.pack_into('<I', h, 28, saves); h[40] = slot
    L, = struct.unpack_from('<I', h, 45)
    struct.pack_into('<H', h, 49 + L + 15, playthrough)
    struct.pack_into('<I', h, 12, zlib.crc32(bytes(h[16:])) & 0xffffffff)
    open(os.path.join(folder, name + '-header'), 'wb').write(bytes(h))

def page(folder):
    r = subprocess.run([here + 'out/tallytest.exe', 'folder', os.path.normpath(folder), '0'], capture_output=True, cwd=work)
    return json.loads(r.stdout.decode('utf-8'))
def lines(p): return ' | '.join(p['rows'][0]['d']['lines'])

done, mid = newest(sys.argv[1]), newest(sys.argv[2])
(C_done, B_done), (C_mid, B_mid) = cats(done), cats(mid)
print('finished save %.1f%%, mid-story save %.1f%%' % (B_done['shown'], B_mid['shown']))
shutil.rmtree(work, ignore_errors=True); os.makedirs(work)
T0 = 1790000000

# 1. a finished first playthrough, then New Game Plus saved over it in the same slot: the bar goes down
put(work + 'a1', done, 0, 0, 100, T0)
p = page(work + 'a1')
check(p['cats'] == C_done and abs(p['shown'] - B_done['shown']) < 0.002 and 'New Game Plus' not in lines(p), 'first playthrough: the bar is what the save holds (%.1f%%)' % p['shown'])
put(work + 'a1', mid, 0, 1, 130, T0 + 5000)
p = page(work + 'a1')
check(p['cats'] == C_mid and abs(p['shown'] - B_mid['shown']) < 0.002, 'New Game Plus saved over it: the bar is the new save\'s own and goes down (%.1f%%)' % p['shown'])
check('New Game Plus, playthrough 2' in lines(p) and 'earlier' not in lines(p) and 'not counted' not in lines(p), 'the page names the playthrough and nothing else: ' + lines(p)[-60:])

# 2. New Game Plus in another slot while the finished game stays in its own
put(work + 'b1', done, 0, 0, 100, T0)
put(work + 'b1', mid, 1, 1, 130, T0 + 5000)
p = page(work + 'b1')
check(p['cats'] == C_mid, 'New Game Plus in another slot, saved last: its own numbers, nothing from the finished slot (%.1f%%)' % p['shown'])
put(work + 'b1', done, 0, 0, 140, T0 + 9000)
p = page(work + 'b1')
check(p['cats'] == C_done and 'New Game Plus' not in lines(p), 'the finished slot is played on and saved last: the page shows it again (%.1f%%)' % p['shown'])

# 3. the most recent save is the one shown, whatever the slot numbers
put(work + 'c1', mid, 0, 0, 50, T0)
put(work + 'c1', done, 2, 0, 100, T0 + 9000)
p = page(work + 'c1')
check(p['cats'] == C_done, 'two slots: the page shows the one saved last')

# 4. nothing is written anywhere, and a record file left over from an earlier build is neither read nor touched
left = work + 'statspage_memory.txt'
open(left, 'wb').write(b'StatsPage memory 1\n[slot 0 playthrough 0]\ntime=1\nplay=1\nparent=-1\nitems=\n')
put(work + 'd1', mid, 0, 1, 130, T0 + 5000)
p = page(work + 'd1')
made = sorted(f for f in os.listdir(work) if not os.path.isdir(work + f))
check(p['cats'] == C_mid and made == ['statspage_memory.txt'] and open(left, 'rb').read().startswith(b'StatsPage memory 1\n[slot 0'), 'no file is written, an old record file is left alone')

print('ALL PASSED' if not fails else 'FAILED (%d)' % len(fails))
sys.exit(1 if fails else 0)
