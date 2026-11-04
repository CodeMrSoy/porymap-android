#!/usr/bin/env python3
"""Verify native APK contents and 16 KB PT_LOAD alignment for modern Pixels."""
import struct
import sys
import zipfile

with zipfile.ZipFile(sys.argv[1]) as apk:
    libraries = [name for name in apk.namelist() if name.startswith('lib/') and name.endswith('.so')]
    assert libraries, 'APK contains no native libraries'
    assert any('libporymap_' in name for name in libraries), 'Porymap native library missing'
    for name in libraries:
        data = apk.read(name)
        assert data[:6] == b'\x7fELF\x02\x01', (name, 'expected little-endian ELF64')
        offset = struct.unpack_from('<Q', data, 32)[0]
        size, count = struct.unpack_from('<HH', data, 54)
        loads = 0
        for i in range(count):
            header = offset + i * size
            if struct.unpack_from('<I', data, header)[0] != 1:
                continue
            file_offset, virtual = struct.unpack_from('<QQ', data, header + 8)
            alignment = struct.unpack_from('<Q', data, header + 48)[0]
            assert alignment >= 16384, (name, 'PT_LOAD is not 16 KB aligned', alignment)
            assert file_offset % 16384 == virtual % 16384, (name, 'segment alignment mismatch')
            loads += 1
        assert loads, (name, 'no loadable segments')
    print(f'PASS: {len(libraries)} native libraries; all loadable segments support 16 KB pages')
