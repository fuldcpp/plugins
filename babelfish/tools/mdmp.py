import struct, sys, collections

MODULE_LIST, EXCEPTION_STREAM, THREAD_LIST, MEMORY_LIST, SYSTEM_INFO = 4, 6, 3, 5, 7

def read(path):
    with open(path, 'rb') as f:
        return f.read()

def parse(path):
    d = read(path)
    if d[:4] != b'MDMP':
        return None
    nstreams, dir_rva = struct.unpack_from('<II', d, 8)
    streams = {}
    for i in range(nstreams):
        t, size, rva = struct.unpack_from('<III', d, dir_rva + i * 12)
        streams[t] = (size, rva)
    return d, streams

def mdstring(d, rva):
    n = struct.unpack_from('<I', d, rva)[0]
    return d[rva + 4: rva + 4 + n].decode('utf-16-le', 'replace')

def modules(d, streams):
    if MODULE_LIST not in streams:
        return []
    _, rva = streams[MODULE_LIST]
    n = struct.unpack_from('<I', d, rva)[0]
    out = []
    base = rva + 4
    for i in range(n):
        o = base + i * 108
        b, size, _chk, _ts, name_rva = struct.unpack_from('<QIIII', d, o)
        out.append((b, size, mdstring(d, name_rva)))
    return sorted(out)

def exception(d, streams):
    if EXCEPTION_STREAM not in streams:
        return None
    _, rva = streams[EXCEPTION_STREAM]
    tid = struct.unpack_from('<I', d, rva)[0]
    o = rva + 8
    code, flags, _rec, addr, nparams = struct.unpack_from('<IIQQI', d, o)
    params = struct.unpack_from('<15Q', d, o + 32)
    return tid, code, flags, addr, nparams, params[:nparams]

def threads(d, streams):
    if THREAD_LIST not in streams:
        return []
    _, rva = streams[THREAD_LIST]
    n = struct.unpack_from('<I', d, rva)[0]
    out = []
    for i in range(n):
        o = rva + 4 + i * 48
        tid, _susp, _pri0, _pri, _teb, stack_start, stack_size, stack_rva = \
            struct.unpack_from('<IIIIQQII', d, o)
        out.append((tid, stack_start, stack_size, stack_rva))
    return out

def owner(mods, addr):
    for b, size, name in mods:
        if b <= addr < b + size:
            return name.split('\\')[-1], addr - b
    return None, 0

def main(path):
    parsed = parse(path)
    if not parsed:
        print('not a minidump'); return
    d, streams = parsed
    mods = modules(d, streams)

    print('=== moduler av intresse ===')
    for b, size, name in mods:
        short = name.split('\\')[-1]
        if any(k in short.lower() for k in ('translate', 'squiggle', 'fuldc', 'spellplugin',
                                            'protocolanalyzer', 'winhttp', 'comctl')):
            print('  %016x +%08x  %s' % (b, size, name))

    exc = exception(d, streams)
    if exc:
        tid, code, flags, addr, nparams, params = exc
        mod, off = owner(mods, addr)
        print('\n=== undantag ===')
        print('  trad          : %d (0x%x)' % (tid, tid))
        print('  kod           : 0x%08X' % code)
        print('  adress        : 0x%016x  -> %s+0x%x' % (addr, mod, off))
        print('  parametrar    : %s' % (['0x%x' % p for p in params],))

        # Poor man's stack walk: scan the crashing thread's stack for values that
        # land inside a loaded module's code range.
        for t_tid, s_start, s_size, s_rva in threads(d, streams):
            if t_tid != tid:
                continue
            print('\n=== returadresser pa stacken (trad %d) ===' % tid)
            stack = d[s_rva: s_rva + s_size]
            seen = []
            for i in range(0, len(stack) - 8, 8):
                v = struct.unpack_from('<Q', stack, i)[0]
                m, o = owner(mods, v)
                if m:
                    seen.append((s_start + i, m, o))
            counts = collections.Counter(m for _, m, _ in seen)
            print('  moduler pa stacken:', dict(counts.most_common(12)))
            print('  forsta 40 traffarna:')
            for sp, m, o in seen[:40]:
                print('    %016x  %s+0x%x' % (sp, m, o))

if __name__ == '__main__':
    main(sys.argv[1])
