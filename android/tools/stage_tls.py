#!/usr/bin/env python3
"""Stage OpenSSL 3 with Android-compatible SONAMEs, preserving ELF layout."""
from pathlib import Path
import struct
import sys


def stage(source, destination):
    data = bytearray(source.read_bytes())
    if data[:6] != b'\x7fELF\x02\x01':
        raise ValueError('Expected a little-endian ELF64 library')
    header = struct.unpack_from('<16sHHIQQQIHHHHHH', data)
    if header[2] != 183:
        raise ValueError('Expected an ARM64 library')
    sections = [struct.unpack_from('<IIQQQQIIQQ', data, header[6] + index * header[11])
                for index in range(header[12])]
    names = sections[header[13]]
    table = data[names[4]:names[4] + names[5]]
    for section in sections:
        name = table[section[0]:].split(b'\0', 1)[0]
        if name != b'.dynstr':
            continue
        start, size = section[4], section[5]
        strings = data[start:start + size]
        count = 0
        for library in (b'crypto', b'ssl'):
            old = b'\0lib' + library + b'.so.3\0'
            new = b'\0lib' + library + b'_3.so\0'
            assert len(old) == len(new)
            count += strings.count(old)
            strings = strings.replace(old, new)
        if count == 0:
            raise ValueError('Expected OpenSSL 3 SONAME or dependency missing')
        data[start:start + size] = strings
        destination.write_bytes(data)
        return
    raise ValueError('ELF dynamic string table missing')


if __name__ == '__main__':
    source, output = map(Path, sys.argv[1:])
    output.mkdir(parents=True, exist_ok=True)
    for name in ('crypto', 'ssl'):
        stage(source / f'lib{name}.so.3', output / f'lib{name}_3.so')
