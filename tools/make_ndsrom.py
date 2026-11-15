#!/usr/bin/env python3
"""Make a one-file Pac'N-Jump DS ROM: append pnj_ds.pack to the built .nds.

    python3 tools/make_ndsrom.py pacnjump.nds pnj_ds.pack pacnjump_full.nds

The game opens its own .nds on the card (the launcher passes the path; failing that it looks through
/, /pacnjump, /nds, /roms, /games and /homebrew) and reads the pack from the tail. Layout after the ROM:
padding to 512, the pack, padding to 4, then 16 bytes: "PNJDSEMB", u32 pack offset, u32 pack size.
The header and the program are untouched, so DLDI patching and launchers work as with the plain ROM."""
import struct, sys

def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    rom = open(sys.argv[1], 'rb').read()
    pack = open(sys.argv[2], 'rb').read()
    if rom[-16:-8] == b'PNJDSEMB':
        sys.exit('%s already carries a pack' % sys.argv[1])
    if pack[:8] != b'PNJPACK1':
        sys.exit('%s is not a pack' % sys.argv[2])
    out = bytearray(rom)
    out += b'\0' * (-len(out) % 512)
    off = len(out)
    out += pack
    out += b'\0' * (-len(out) % 4)
    out += b'PNJDSEMB' + struct.pack('<II', off, len(pack))
    open(sys.argv[3], 'wb').write(out)
    print('%s: %d bytes (rom %d, pack %d at %d)' % (sys.argv[3], len(out), len(rom), len(pack), off))

main()
