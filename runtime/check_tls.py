#!/usr/bin/env python3
"""Fails the build unless pnl_tls_app_slot is at offset 0 of peanuts-run's TLS segment and the segment's alignment is at most 16 (see src/pnl_tls.c)."""
import struct, sys
def main(path):
    b = open(path, 'rb').read()
    assert b[:4] == b'\x7fELF' and b[4] == 2 and b[5] == 1, 'not a 64-bit little-endian ELF'
    phoff, shoff = struct.unpack_from('<QQ', b, 0x20); phentsize, phnum, shentsize, shnum, shstrndx = struct.unpack_from('<HHHHH', b, 0x36)
    tls = [struct.unpack_from('<IIQQQQQQ', b, phoff + i * phentsize) for i in range(phnum)]
    tls = [p for p in tls if p[0] == 7]   # PT_TLS
    if len(tls) != 1: sys.exit('[RED] peanuts-run has %d TLS segments' % len(tls))
    align = tls[0][7]
    if align > 16: sys.exit('[RED] the TLS segment is %d-aligned: the slot word would not be at tpidr_el0 + 16' % align)
    secs = [struct.unpack_from('<IIQQQQIIQQ', b, shoff + i * shentsize) for i in range(shnum)]
    found = None
    for s in secs:
        if s[1] != 2: continue   # SHT_SYMTAB
        strtab = secs[s[6]]; data = b[s[4]:s[4] + s[5]]; strs = b[strtab[4]:strtab[4] + strtab[5]]
        for i in range(0, len(data), 24):
            name, info, other, shndx, value, size = struct.unpack_from('<IBBHQQ', data, i)
            if info & 0xf == 6 and strs[name:strs.index(b'\0', name)] == b'pnl_tls_app_slot': found = value
    if found is None: sys.exit('[RED] no pnl_tls_app_slot in peanuts-run')
    if found != 0: sys.exit('[RED] pnl_tls_app_slot is at offset %#x of the TLS segment, not 0: pnl_tls.c must be linked first' % found)
    print('TLS check: the slot word leads the TLS segment (alignment %d)' % align)
main(sys.argv[1])
