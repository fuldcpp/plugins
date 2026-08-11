import os, struct, sys, re, bisect
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mdmp

def sections(d):
    pe = struct.unpack_from('<I', d, 0x3C)[0]
    nsec = struct.unpack_from('<H', d, pe + 6)[0]
    opt = struct.unpack_from('<H', d, pe + 20)[0]
    base = pe + 24 + opt
    out = {}
    for i in range(nsec):
        o = base + i * 40
        name = d[o:o + 8].rstrip(b'\0').decode('ascii', 'replace')
        vsize, vaddr, rawsize, rawptr = struct.unpack_from('<IIII', d, o + 8)
        out[name] = (vaddr, vsize, rawptr, rawsize)
    return out

def text_bytes(path):
    d = open(path, 'rb').read()
    vaddr, vsize, rawptr, rawsize = sections(d)['.text']
    return d[rawptr:rawptr + rawsize]

def load_map(path):
    syms = []
    base = None
    for line in open(path, 'r', errors='replace'):
        m = re.search(r'Preferred load address is ([0-9A-Fa-f]+)', line)
        if m:
            base = int(m.group(1), 16)
            continue
        m = re.match(r'\s+([0-9A-Fa-f]{4}):([0-9A-Fa-f]{8})\s+(\S+)\s+([0-9A-Fa-f]{16})\s', line)
        if m and base is not None:
            va = int(m.group(4), 16)
            syms.append((va - base, m.group(3)))
    syms.sort()
    return syms

def resolve(syms, keys, rva):
    i = bisect.bisect_right(keys, rva) - 1
    if i < 0:
        return None
    return syms[i][1], rva - syms[i][0]

def main(dumppath, dllbuilt, dllinstalled, mappath):
    a, b = text_bytes(dllbuilt), text_bytes(dllinstalled)
    print('.text identisk: %s  (%d vs %d byte)' % (a == b, len(a), len(b)))
    if a != b:
        diff = sum(1 for x, y in zip(a, b) if x != y)
        print('  skiljer i %d byte - offset kan vara opalitliga' % diff)

    syms = load_map(mappath)
    keys = [s[0] for s in syms]
    print('symboler ur .map: %d' % len(syms))

    d, streams = mdmp.parse(dumppath)
    mods = mdmp.modules(d, streams)
    tid, code, flags, addr, nparams, params = mdmp.exception(d, streams)

    tbase = tsize = None
    for mb, ms, name in mods:
        if 'Translate-x64.dll' in name:
            tbase, tsize = mb, ms
    print('\nTranslate-x64.dll @ %016x +%x' % (tbase, tsize))

    for t_tid, s_start, s_size, s_rva in mdmp.threads(d, streams):
        if t_tid != tid:
            continue
        stack = d[s_rva:s_rva + s_size]
        print('\n=== Translate-ramar pa stacken, i stackordning ===')
        seen = []
        for i in range(0, len(stack) - 8, 8):
            v = struct.unpack_from('<Q', stack, i)[0]
            if tbase <= v < tbase + tsize:
                r = resolve(syms, keys, v - tbase)
                seen.append((s_start + i, v - tbase, r))
        for sp, off, r in seen:
            if r:
                print('  %016x  +0x%06x  %s +0x%x' % (sp, off, r[0], r[1]))
            else:
                print('  %016x  +0x%06x  (okand)' % (sp, off))

if __name__ == '__main__':
    main(*sys.argv[1:])
