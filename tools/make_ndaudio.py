#!/usr/bin/env python3
"""Converts the pack's sound effects and music to the DS's own IMA ADPCM, mono, at a low sample rate.

    python3 tools/make_ndaudio.py pnj.pack -o nds_out/audio            (needs ffmpeg on the PATH)
    python3 tools/make_ndaudio.py pnj.pack --music-rate 16000 --sfx-rate 22050

The ARM9 cannot decode Vorbis fast enough while it also runs the game, so the data builder does it once on the PC. ADPCM is 4 bits a sample and the
DS sound hardware plays it directly (the ARM7 does the work), so music streams from the card with almost no CPU.
File (little endian): "NDA1", u32 rate, u32 sample count, u32 data bytes, then the DS ADPCM data: a 32-bit header (bits 0-15 first sample, bits 16-22 step index),
then 4-bit codes, low nibble first, padded to a whole 32-bit word.
"""
import argparse, os, struct, subprocess, sys

STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337,
        371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
        5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767]
IDX = [-1, -1, -1, -1, 2, 4, 6, 8]


def read_pack(path):
    d = open(path, 'rb').read()
    n = struct.unpack('<I', d[8:12])[0]; p = 12; ent = {}
    for _ in range(n):
        l = struct.unpack('<H', d[p:p + 2])[0]; p += 2; nm = d[p:p + l].decode(); p += l
        off, sz = struct.unpack('<II', d[p:p + 8]); p += 8; ent[nm] = d[off:off + sz]
    return ent


def to_pcm(data, ext, rate, tmp):
    src = tmp + '.' + ext; open(src, 'wb').write(data)
    out = subprocess.run(['ffmpeg', '-v', 'error', '-i', src, '-f', 's16le', '-ac', '1', '-ar', str(rate), '-'], capture_output=True, check=True).stdout
    os.remove(src)
    return struct.unpack('<%dh' % (len(out) // 2), out)


def encode(pcm):
    """IMA ADPCM as the DS plays it. Returns (bytes, decoded samples) so the caller can check the error."""
    pred = pcm[0]; idx = 0; nib = []; dec = [pred]
    for s in pcm[1:]:
        step = STEP[idx]; diff = s - pred; code = 0
        if diff < 0: code = 8; diff = -diff
        if diff >= step: code |= 4; diff -= step
        step2 = step >> 1
        if diff >= step2: code |= 2; diff -= step2
        step4 = step >> 2
        if diff >= step4: code |= 1
        d = step >> 3
        if code & 4: d += step
        if code & 2: d += step >> 1
        if code & 1: d += step >> 2
        pred += -d if code & 8 else d
        if pred > 32767: pred = 32767
        elif pred < -32768: pred = -32768
        idx += IDX[code & 7]
        if idx < 0: idx = 0
        elif idx > 88: idx = 88
        nib.append(code); dec.append(pred)
    head = struct.pack('<I', (pcm[0] & 0xffff) | (0 << 16))
    if len(nib) & 7: nib += [0] * (8 - (len(nib) & 7))
    body = bytearray(len(nib) // 2)
    for i in range(0, len(nib), 2): body[i // 2] = nib[i] | (nib[i + 1] << 4)
    return head + bytes(body), dec


def snr(a, b):
    import math
    num = sum(x * x for x in a); den = sum((x - y) ** 2 for x, y in zip(a, b)) or 1
    return 10 * math.log10(num / den) if num else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('pack'); ap.add_argument('-o', '--out', default='nds_out/audio')
    ap.add_argument('--music-rate', type=int, default=16000); ap.add_argument('--sfx-rate', type=int, default=22050)
    a = ap.parse_args()
    ent = read_pack(a.pack); os.makedirs(a.out, exist_ok=True); tot = 0; rep = []
    for nm in sorted(k for k in ent if k.startswith('audio/')):
        base, ext = os.path.splitext(os.path.basename(nm)); rate = a.music_rate if base.startswith('bgm') else a.sfx_rate
        pcm = to_pcm(ent[nm], ext[1:], rate, os.path.join(a.out, '_tmp'))
        if len(pcm) < 2: continue
        data, dec = encode(pcm)
        with open(os.path.join(a.out, base + '.nda'), 'wb') as f: f.write(b'NDA1' + struct.pack('<III', rate, len(pcm), len(data)) + data)
        tot += len(data) + 16
        rep.append('%-14s %6d Hz %8d samples %7.1f s %8d bytes  SNR %.1f dB' % (base, rate, len(pcm), len(pcm) / rate, len(data) + 16, snr(pcm, dec)))
        print(rep[-1])
    rep.append('total %d bytes (%.2f MB)' % (tot, tot / 1048576.0)); print(rep[-1])
    open(os.path.join(a.out, 'report.txt'), 'w').write('\n'.join(rep) + '\n')


if __name__ == '__main__':
    main()
