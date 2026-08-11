import struct, sys

# Decodes an MSVC ThrowInfo at a given RVA straight out of the PE on disk, which
# is enough to name the exact C++ type that was thrown.

def sections(d):
    pe = struct.unpack_from('<I', d, 0x3C)[0]
    nsec = struct.unpack_from('<H', d, pe + 6)[0]
    opt_size = struct.unpack_from('<H', d, pe + 20)[0]
    base = pe + 24 + opt_size
    out = []
    for i in range(nsec):
        o = base + i * 40
        name = d[o:o + 8].rstrip(b'\0').decode('ascii', 'replace')
        vsize, vaddr, rawsize, rawptr = struct.unpack_from('<IIII', d, o + 8)
        out.append((vaddr, vsize, rawptr, rawsize, name))
    return out

def rva2off(secs, rva):
    for vaddr, vsize, rawptr, rawsize, _ in secs:
        if vaddr <= rva < vaddr + max(vsize, rawsize):
            return rawptr + (rva - vaddr)
    return None

def cstr(d, off):
    end = d.index(b'\0', off)
    return d[off:end].decode('ascii', 'replace')

def main(dll, rva):
    d = open(dll, 'rb').read()
    secs = sections(d)
    o = rva2off(secs, rva)
    attrs, unwind, fwd, cta_rva = struct.unpack_from('<IiiI', d, o)
    print('ThrowInfo @ rva 0x%x  attributes=0x%x' % (rva, attrs))

    cta = rva2off(secs, cta_rva)
    n = struct.unpack_from('<I', d, cta)[0]
    print('katalogiserade typer: %d' % n)
    for i in range(n):
        ct_rva = struct.unpack_from('<I', d, cta + 4 + i * 4)[0]
        ct = rva2off(secs, ct_rva)
        props, ptype_rva = struct.unpack_from('<Ii', d, ct)
        td = rva2off(secs, ptype_rva)
        print('  %d: %s' % (i, cstr(d, td + 16)))

if __name__ == '__main__':
    main(sys.argv[1], int(sys.argv[2], 16))
