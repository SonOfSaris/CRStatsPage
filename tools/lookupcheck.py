# lookupcheck.py - does the mod's start-up lookup work on a game exe? The same steps as statspage.cpp's
# resolve(): both patterns exactly once in .text, then the one vtable entry that holds the handler with the
# neighbour behind it and a code pointer in front. Prints the exe's PE timestamp for the known builds table.
# usage: python lookupcheck.py <exe> [<exe> ...]
import re, struct, sys, time

HANDLER = "48 89 5C 24 08 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 40 FE FF FF 48 81 EC C0 02 00 00 4D 8B F0 4C 8B E9"
NEXT = ("48 89 5C 24 08 48 89 74 24 18 55 57 41 54 41 56 41 57 48 8D 6C 24 A0 48 81 EC 60 01 00 00 4D 8B F8 "
        "48 8B D9 45 33 E4 44 89 A5 A8 00 00 00")

def rx(pattern, skip=0):
    out = b''
    for t in pattern.split()[skip:]: out += b'.' if t == '??' else re.escape(bytes([int(t, 16)]))
    return re.compile(out, re.S)

def check(path):
    d = open(path, 'rb').read()
    pe = struct.unpack_from('<I', d, 0x3C)[0]
    nsec, stamp = struct.unpack_from('<HI', d, pe + 6)
    opt = pe + 24
    base = struct.unpack_from('<Q', d, opt + 24)[0]
    size_image = struct.unpack_from('<I', d, opt + 56)[0]
    sec0 = opt + struct.unpack_from('<H', d, pe + 20)[0]
    secs = []
    for i in range(nsec):
        name, vsize, va, rsize, raw = struct.unpack_from('<8sIIII', d, sec0 + 40 * i)
        ch = struct.unpack_from('<I', d, sec0 + 40 * i + 36)[0]
        secs.append((name.rstrip(b'\0').decode(), va, vsize, raw, rsize, ch))
    text = [s for s in secs if s[0] == '.text'][0]
    t = d[text[3]:text[3] + min(text[2], text[4])]
    print('%s\n  timestamp 0x%08X (%s)' % (path, stamp, time.strftime('%d %B %Y', time.gmtime(stamp))))
    found = {}
    for what, pat in (('handler', HANDLER), ('neighbour', NEXT)):
        hits = [m.start() for m in rx(pat).finditer(t)]
        tails = [m.start() - 16 for m in rx(pat, 16).finditer(t)]
        print('  %-9s pattern %d time(s), from byte 16 on %d time(s)%s' % (what, len(hits), len(tails), ', rva 0x%x' % (text[1] + hits[0]) if len(hits) == 1 else ''))
        if len(hits) == 1: found[what] = base + text[1] + hits[0]
    if len(found) < 2: print('  LOOKUP FAILS'); return False
    in_text = lambda p: base + text[1] <= p < base + text[1] + text[2]
    in_exe = lambda p: base <= p < base + size_image
    count = 0; holds = 0
    for name, va, vsize, raw, rsize, ch in secs:
        if ch & 0x20000000 or not ch & 0x40000000 or not ch & 0x40: continue     # not executable, readable, initialized data
        n = min(vsize, rsize) // 8
        p = struct.unpack_from('<%dQ' % n, d, raw)
        for k in range(1, n - 1):
            if p[k] == found['handler']: holds += 1
            if p[k + 1] != found['neighbour'] or not in_text(p[k - 1]): continue
            if p[k] != found['handler'] and in_exe(p[k]): continue
            count += 1; print('  vtable entry at rva 0x%x in %s' % (va + 8 * k, name))
    print('  entries found %d; the handler\'s address stands in %d place(s) of the data' % (count, holds))
    print('  LOOKUP WORKS' if count == 1 else '  LOOKUP FAILS')
    return count == 1

if __name__ == '__main__':
    ok = [check(p) for p in sys.argv[1:]]
    sys.exit(0 if all(ok) else 1)
