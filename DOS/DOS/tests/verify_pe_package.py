#!/usr/bin/env python3
from pathlib import Path
import struct
import sys
import zlib


def pe_info(data: bytes):
    if len(data) < 0x40 or data[:2] != b'MZ':
        raise AssertionError('missing MZ')
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    if pe + 24 > len(data) or data[pe:pe + 4] != b'PE\0\0':
        raise AssertionError('missing PE signature')
    machine, sections, _, _, _, optional_size = struct.unpack_from('<HHIIIH', data, pe + 4)
    optional = pe + 24
    if optional + optional_size > len(data):
        raise AssertionError('truncated optional header')
    magic = struct.unpack_from('<H', data, optional)[0]
    if machine != 0x8664 or magic != 0x20B:
        raise AssertionError(f'not x64 PE32+: machine=0x{machine:04x} magic=0x{magic:04x}')
    section_table = optional + optional_size
    if section_table + sections * 40 > len(data):
        raise AssertionError('truncated section table')
    return pe, sections, optional, optional_size


def package_info(data: bytes):
    magic = b'DOSPKG4'
    footer = len(magic) + 8 + 4
    pos = data.rfind(magic)
    if pos < 0 or pos + footer != len(data):
        raise AssertionError('DOSPKG4 footer missing')
    source_len, crc = struct.unpack_from('<QI', data, pos + len(magic))
    if source_len > pos:
        raise AssertionError('invalid source length')
    source = data[pos - source_len:pos]
    if len(source) != source_len:
        raise AssertionError('truncated source')
    if (zlib.crc32(source) & 0xffffffff) != crc:
        raise AssertionError('CRC mismatch')
    return pos, source


def main():
    if len(sys.argv) != 3:
        raise SystemExit('usage: verify_pe_package.py base.exe packaged.exe')
    base = Path(sys.argv[1]).read_bytes()
    packaged = Path(sys.argv[2]).read_bytes()
    pe_info(base)
    pe_info(packaged)
    pos, source = package_info(packaged)
    if packaged[:len(base)] != base:
        raise AssertionError('PE carrier bytes changed before overlay')
    if pos != len(base) + len(source):
        raise AssertionError('overlay/source boundary mismatch')
    print('BASE_PE=PASS')
    print('PACKAGED_PE=PASS')
    print('OVERLAY_PRESERVATION=PASS')
    print('DOSPKG4=PASS')
    print('CRC32=PASS')
    print(f'SOURCE_BYTES={len(source)}')


if __name__ == '__main__':
    main()
