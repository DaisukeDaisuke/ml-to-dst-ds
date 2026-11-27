"""Build a small original DS test ROM with a RAM counter on ARM9."""
from pathlib import Path
import struct
import sys

rom = bytearray(128 * 1024)
rom[:12] = b"ML2DST TEST "
rom[12:16] = b"####"
struct.pack_into("<8I", rom, 0x20,
                 0x200, 0x02000000, 0x02000000, 24,
                 0x400, 0x03800000, 0x03800000, 4)
struct.pack_into("<2I", rom, 0x80, len(rom), 0x200)
# LDR r0, [pc, #12]; LDR r1, [r0]; ADD r1, r1, #1;
# STR r1, [r0]; B to second instruction; address of counter.
struct.pack_into("<6I", rom, 0x200, 0xe59f000c, 0xe5901000, 0xe2811001,
                 0xe5801000, 0xeafffffb, 0x02001000)
struct.pack_into("<I", rom, 0x400, 0xeafffffe)
Path(sys.argv[1]).write_bytes(rom)
