"""Build a small original DS test ROM with a RAM counter on ARM9."""
from pathlib import Path
import struct
import sys

rom = bytearray(128 * 1024)
rom[:12] = b"ML2DST TEST "
retail = "--retail" in sys.argv[2:]
rom[12:16] = b"ZZZE" if retail else b"####"
arm9_origin, arm7_origin = (0x4000, 0x5000) if retail else (0x200, 0x400)
entry_offset = 0x800 if retail else 0
struct.pack_into("<8I", rom, 0x20,
                 arm9_origin, 0x02000000 + entry_offset, 0x02000000, entry_offset + 24,
                 arm7_origin, 0x03800000, 0x03800000, 4)
struct.pack_into("<2I", rom, 0x80, len(rom), 0x200)
if retail:
    # Match the marker melonDS uses to detect a decrypted DS secure area.
    struct.pack_into("<2I", rom, arm9_origin, 0xe7ffdeff, 0xe7ffdeff)
# LDR r0, [pc, #12]; LDR r1, [r0]; ADD r1, r1, #1;
# STR r1, [r0]; B to second instruction; address of counter.
struct.pack_into("<6I", rom, arm9_origin + entry_offset, 0xe59f000c, 0xe5901000, 0xe2811001,
                 0xe5801000, 0xeafffffb, 0x02001000)
struct.pack_into("<I", rom, arm7_origin, 0xeafffffe)
Path(sys.argv[1]).write_bytes(rom)
