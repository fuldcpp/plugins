import struct, sys, bisect

DCHUB_SLOTS = ['apiVersion(+pad)', 'add_hub', 'find_hub', 'remove_hub', 'emulate_protocol_cmd',
               'send_protocol_cmd', 'send_message', 'local_message', 'send_private_message',
               'find_user', 'copy_user', 'release_user', 'copy', 'release']

def pe(d):
    off = struct.unpack_from('<I', d, 0x3C)[0]
    nsec = struct.unpack_from('<H', d, off + 6)[0]
    optsz = struct.unpack_from('<H', d, off + 20)[0]
    opt = off + 24
    imagebase = struct.unpack_from('<Q', d, opt + 24)[0]
    secs = []
    for i in range(nsec):
        o = opt + optsz + i * 40
        name = d[o:o + 8].rstrip(b'\0').decode('ascii', 'replace')
        vsize, vaddr, rawsize, rawptr = struct.unpack_from('<IIII', d, o + 8)
        secs.append((vaddr, vsize, rawptr, rawsize, name))
    # Exception directory = data directory entry 3
    nd = struct.unpack_from('<I', d, opt + 108)[0]
    exc_rva, exc_size = struct.unpack_from('<II', d, opt + 112 + 3 * 8)
    return imagebase, secs, exc_rva, exc_size

def rva2off(secs, rva):
    for vaddr, vsize, rawptr, rawsize, _ in secs:
        if vaddr <= rva < vaddr + max(vsize, rawsize):
            return rawptr + (rva - vaddr)
    return None

def sec_of(secs, rva):
    for vaddr, vsize, rawptr, rawsize, name in secs:
        if vaddr <= rva < vaddr + max(vsize, rawsize):
            return name
    return '?'

def main(exepath, *addrs):
    d = open(exepath, 'rb').read()
    imagebase, secs, exc_rva, exc_size = pe(d)
    print('image base 0x%x, exception dir rva 0x%x size %d' % (imagebase, exc_rva, exc_size))

    base = rva2off(secs, exc_rva)
    n = exc_size // 12
    funcs = []
    for i in range(n):
        b, e, u = struct.unpack_from('<III', d, base + i * 12)
        funcs.append((b, e))
    funcs.sort()
    starts = [f[0] for f in funcs]
    print('RUNTIME_FUNCTION-poster: %d' % len(funcs))

    targets = []
    for a in addrs:
        rva = int(a, 16)
        i = bisect.bisect_right(starts, rva) - 1
        if i >= 0 and funcs[i][0] <= rva < funcs[i][1]:
            b, e = funcs[i]
            print('\nrva 0x%x  ->  funktion 0x%x..0x%x  (%d byte, sektion %s)'
                  % (rva, b, e, e - b, sec_of(secs, b)))
            targets.append(b)
        else:
            print('\nrva 0x%x  ->  ingen matchande funktion' % rva)

    uniq = sorted(set(targets))
    print('\nunika funktioner: %s' % ['0x%x' % t for t in uniq])

    # Look for the function address stored as a pointer anywhere in the image,
    # which is how an interface struct of function pointers would reference it.
    for t in uniq:
        needle = struct.pack('<Q', imagebase + t)
        print('\n=== soker pekare till 0x%x ===' % t)
        pos = 0
        hits = 0
        while True:
            pos = d.find(needle, pos)
            if pos < 0 or hits >= 6:
                break
            # Which rva is this file offset?
            here = None
            for vaddr, vsize, rawptr, rawsize, name in secs:
                if rawptr <= pos < rawptr + rawsize:
                    here = (vaddr + (pos - rawptr), name)
            if here:
                print('  hittad i %s vid rva 0x%x' % (here[1], here[0]))
                # Dump the qwords around it as rvas, to expose a pointer table.
                tbl = pos - 8 * 7
                for k in range(16):
                    o = tbl + k * 8
                    if o < 0 or o + 8 > len(d):
                        continue
                    v = struct.unpack_from('<Q', d, o)[0]
                    rel = v - imagebase
                    mark = '  <== HAR' if o == pos else ''
                    slot = ''
                    idx = (o - pos) // 8 + 2  # find_hub is slot index 2
                    if 0 <= idx < len(DCHUB_SLOTS):
                        slot = '  [om find_hub: %s]' % DCHUB_SLOTS[idx]
                    if 0 < rel < 0x4000000:
                        print('    +%3d  rva 0x%08x  %s%s%s'
                              % ((o - pos) // 8, rel, sec_of(secs, rel), mark, slot))
                    else:
                        print('    +%3d  0x%016x%s' % ((o - pos) // 8, v, mark))
                hits += 1
            pos += 1
        if hits == 0:
            print('  ingen statisk pekare hittad (interfacet fylls troligen i runtime)')

if __name__ == '__main__':
    main(*sys.argv[1:])
