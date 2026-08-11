import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mdmp

MEMORY_LIST, MEMORY64_LIST = 5, 9

def ranges(d, streams):
    out = []
    if MEMORY_LIST in streams:
        _, rva = streams[MEMORY_LIST]
        n = struct.unpack_from('<I', d, rva)[0]
        for i in range(n):
            o = rva + 4 + i * 16
            start, size, mrva = struct.unpack_from('<QII', d, o)
            out.append((start, size, mrva))
    if MEMORY64_LIST in streams:
        _, rva = streams[MEMORY64_LIST]
        n, base = struct.unpack_from('<QQ', d, rva)
        off = base
        for i in range(n):
            start, size = struct.unpack_from('<QQ', d, rva + 16 + i * 16)
            out.append((start, size, off))
            off += size
    return out

def read(d, rs, addr, count):
    for start, size, rva in rs:
        if start <= addr < start + size:
            avail = min(count, start + size - addr)
            o = rva + (addr - start)
            return d[o:o + avail]
    return None

def main(dumppath, addr, count=64):
    d, streams = mdmp.parse(dumppath)
    rs = ranges(d, streams)
    total = sum(s for _, s, _ in rs)
    print('minnesregioner: %d, totalt %d byte' % (len(rs), total))

    addr = int(addr, 16)
    blob = read(d, rs, addr, int(count))
    if not blob:
        print('adress %016x finns inte i dumpen' % addr)
        return

    print('\n%016x:' % addr)
    for i in range(0, len(blob) - 7, 8):
        q = struct.unpack_from('<Q', blob, i)[0]
        note = ''
        s = read(d, rs, q, 64)
        if s:
            text = s.split(b'\0')[0]
            if text and all(32 <= c < 127 for c in text):
                note = '  -> "%s"' % text.decode('ascii')
        lo, hi = struct.unpack_from('<II', blob, i)
        print('  +%02d  %016x   (u32: %-10d %-10d)%s' % (i, q, lo, hi, note))

if __name__ == '__main__':
    main(*sys.argv[1:])
