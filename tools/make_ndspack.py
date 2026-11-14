#!/usr/bin/env python3
"""Builds the Nintendo DS game data (pnj_ds.pack) from a pnj.pack, which make_pack.py builds from your own APK.

    python3 tools/make_ndspack.py pnj.pack -o pnj_ds.pack          needs numpy, Pillow, gcc and ffmpeg
    python3 tools/make_ndspack.py pnj.pack -o pnj_ds.pack --preview out_dir     also writes a PNG of every texture page
    python3 tools/make_ndspack.py pnj.pack -o pnj_ds.pack --extra misses.txt    adds source rectangles the DS build asked for and did not find

What the DS can hold decides the format. Two of its four video memory banks carry the picture from one screen to the other, which leaves 256 KB for
textures: one 1024x512 page at 4 bits a pixel. So there is one page per stage, holding everything that stage and the menus draw, at 0.8 of the size
it has in the 320x480 field (both screens stacked are 256x384, exactly 0.8), one texel per screen pixel.

A page is made of cells. A cell is one source rectangle the game draws from a sprite sheet (a sprite's animation frame, a font glyph). Cells are cut
into tiles of at most 128x128, each tile trimmed to its pixels and given its own 16-colour palette (15 colours and transparent).

pnj_ds.pack is an ordinary PNJPACK1 file (see make_pack.py) with:
  meta/atlas.txt, maps/*, lang/*      copied from the source pack
  nds/stage<1-8>.ndp                  texture pages
  nds/<name>.nda                      sounds and music as DS ADPCM (see make_ndaudio.py)

.ndp (little endian): "NDP2", u16 width, u16 height, u32 cells, u32 tiles, u32 palettes, then
  cells     x { u8 sheet, u8 0, s16 x, y, w, h (the source rectangle, in sheet units), u16 dw, dh (its size in texels), u16 first tile, u16 tiles }, sorted
  tiles     x { u16 px, py (place in the page), u16 w, h, u16 ox, oy (place inside the cell, texels), u16 palette, u16 0 }
  palettes  x 16 u16 (BGR555, entry 0 is the transparent one)
  width * height / 2 bytes of pixels, low nibble first
"""
import argparse, io, json, os, struct, subprocess, sys, tempfile
import numpy as np
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_ndaudio

TEX = ['bnglogo', 'bndr_logo', 'sysparts', 'stages', 'charaparts'] + ['stage_%d_%s' % (i, k) for i in range(1, 9) for k in ('map', 'cha')]
SCALE = 0.8
PAGE_W, PAGE_H = 1024, 512
TILE = 128
HELP_SPRITES = [18, 19, 105, 106, 107, 115, 116, 117, 151, 152, 153, 154]      # the Help pages show these whatever stage is selected


def read_pack(path):
    d = open(path, 'rb').read()
    if d[:8] != b'PNJPACK1': sys.exit('not a pnj.pack: ' + path)
    n = struct.unpack('<I', d[8:12])[0]; p = 12; ent = {}
    for _ in range(n):
        l = struct.unpack('<H', d[p:p + 2])[0]; p += 2; nm = d[p:p + l].decode(); p += l
        off, sz = struct.unpack('<II', d[p:p + 8]); p += 8; ent[nm] = d[off:off + sz]
    return ent


def write_pack(path, files):
    names = sorted(files); head = b'PNJPACK1' + struct.pack('<I', len(names))
    size = len(head) + sum(2 + len(n.encode()) + 8 for n in names); off = size; idx = b''
    for n in names:
        b = n.encode(); idx += struct.pack('<H', len(b)) + b + struct.pack('<II', off, len(files[n])); off += len(files[n])
    with open(path, 'wb') as f:
        f.write(head + idx)
        for n in names: f.write(files[n])


class Sheet:
    def __init__(self, png, tw):
        im = Image.open(io.BytesIO(png)).convert('RGBA')
        self.k = im.size[0] / float(tw)                       # sheet pixels per atlas unit (2 for the hdpi sheets)
        self.w, self.h = im.size; self.px = np.array(im)


def cut(sheet, rect):
    """The source rectangle (atlas units) shrunk to SCALE of its on-screen size: RGBA, alpha made 0 or 255."""
    x, y, w, h = rect; k = sheet.k
    x0, y0, x1, y1 = int(round(x * k)), int(round(y * k)), int(round((x + w) * k)), int(round((y + h) * k))
    if x0 < 0 or y0 < 0 or x1 > sheet.w or y1 > sheet.h or x1 <= x0 or y1 <= y0: return None
    a = sheet.px[y0:y1, x0:x1].astype(np.float32)
    if not (a[:, :, 3] > 8).any(): return None
    dw, dh = max(1, int(round(w * SCALE))), max(1, int(round(h * SCALE)))
    pm = a.copy(); pm[:, :, :3] *= a[:, :, 3:4] / 255.0
    im = Image.fromarray(np.clip(pm + 0.5, 0, 255).astype(np.uint8), 'RGBA').resize((dw, dh), Image.LANCZOS)
    o = np.array(im).astype(np.float32); al = o[:, :, 3:4]
    o[:, :, :3] = np.where(al > 0, o[:, :, :3] * 255.0 / np.maximum(al, 1), 0)
    o = np.clip(o, 0, 255).astype(np.uint8)
    o[:, :, 3] = np.where(o[:, :, 3] >= 112, 255, 0)
    o[o[:, :, 3] == 0] = 0
    return o


def to555(rgb): return (rgb.astype(np.uint16) >> 3)


def quantize(t):
    """One tile to 15 colours plus transparent. Returns (indices h x w uint8, palette as 16 BGR555 ints)."""
    op = t[:, :, 3] > 0
    px = to555(t[:, :, :3][op])                                # n x 3, 5 bits a channel
    uniq, inv = np.unique(px, axis=0, return_inverse=True)
    if len(uniq) <= 15:
        pal = uniq; idx_op = inv.reshape(-1)
    else:
        strip = Image.fromarray((px.astype(np.uint8) << 3).reshape(1, -1, 3), 'RGB')
        q = strip.quantize(colors=15, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
        pal = (np.array(q.getpalette()[:45], np.uint8).reshape(-1, 3) >> 3).astype(np.uint16)
        for _ in range(4):                                     # a few k-means rounds on top of the median cut
            d = ((px[:, None, :].astype(np.int32) - pal[None, :, :].astype(np.int32)) ** 2).sum(2); idx_op = d.argmin(1)
            for c in range(len(pal)):
                m = idx_op == c
                if m.any(): pal[c] = np.round(px[m].mean(0)).astype(np.uint16)
        d = ((px[:, None, :].astype(np.int32) - pal[None, :, :].astype(np.int32)) ** 2).sum(2); idx_op = d.argmin(1)
    idx = np.zeros(t.shape[:2], np.uint8); idx[op] = idx_op.astype(np.uint8) + 1
    out = [0] * 16
    for i, c in enumerate(pal[:15]): out[i + 1] = int(c[0]) | (int(c[1]) << 5) | (int(c[2]) << 10)
    return idx, tuple(out)


class MaxRects:
    def __init__(self, w, h): self.free = [(0, 0, w, h)]

    def place(self, rw, rh):
        best = None
        for (fx, fy, fw, fh) in self.free:
            if rw <= fw and rh <= fh:
                sc = (min(fw - rw, fh - rh), fy, fx)
                if best is None or sc < best[0]: best = (sc, fx, fy)
        if best is None: return None
        x, y = best[1], best[2]; nf = []
        for (fx, fy, fw, fh) in self.free:
            if x >= fx + fw or x + rw <= fx or y >= fy + fh or y + rh <= fy: nf.append((fx, fy, fw, fh)); continue
            if x > fx: nf.append((fx, fy, x - fx, fh))
            if x + rw < fx + fw: nf.append((x + rw, fy, fx + fw - (x + rw), fh))
            if y > fy: nf.append((fx, fy, fw, y - fy))
            if y + rh < fy + fh: nf.append((fx, y + rh, fw, fy + fh - (y + rh)))
        nf = list(set(nf))
        self.free = [f for f in nf if not any(g is not f and g[0] <= f[0] and g[1] <= f[1] and g[0] + g[2] >= f[0] + f[2] and g[1] + g[3] >= f[1] + f[3] for g in nf)]
        return x, y


def sprite_rect(sp, col=0, row=0, vrow=0):
    t, x, y, w, h, tw = sp
    return (t, x + col * (w + 2), y + row * (h + 2) + vrow * h, w, h)


def page_rects(stage, atlas, used, extra):
    """Every source rectangle (sheet, x, y, w, h) the DS draws while stage (0-based) is the selected one."""
    R = set()
    sheets = {2, 3, 4, 5 + 2 * stage, 6 + 2 * stage}
    for sid, sp in enumerate(atlas):
        t = sp[0]
        if t == 2: R.add(sprite_rect(sp))                                      # menus, HUD, buttons
        if t == 3 and sid == 83 + stage: R.add(sprite_rect(sp))                # this stage's picture on the stage select screen
        if sid in HELP_SPRITES: R.add(sprite_rect(sp))
        if t in sheets and t != 3:
            for (c, r, v) in used.get(sid, ()): R.add(sprite_rect(sp, c, r, v))
    for i in range(1, 96): R.add((2, (i % 16) * 32, (i // 16) * 32, 32, 32))   # the dotted font: 16 x 6 cells of 32 from ' ' (ftext in menus.h)
    for sid in (126 + 3 * stage, 127 + 3 * stage, 128 + 3 * stage):
        if sid < len(atlas): R.add(sprite_rect(atlas[sid]))
    for c in range(3): R.add(sprite_rect(atlas[92], c))                        # the title screen's hopping Pac-Man
    for e in extra:
        if e[0] in sheets or e[0] in (atlas[s][0] for s in HELP_SPRITES): R.add(e)
    return R


def build_page(stage, atlas, sheets, used, extra, preview):
    rects = sorted(page_rects(stage, atlas, used, extra))
    cells = []; tiles = []; pals = {}; pal_list = []; tile_px = {}; skipped = 0
    for rc in rects:
        sh = sheets[rc[0]]
        img = cut(sh, rc[1:]) if sh else None
        if img is None: skipped += 1; continue
        dh, dw = img.shape[:2]; first = len(tiles)
        nx, ny = (dw + TILE - 1) // TILE, (dh + TILE - 1) // TILE
        for ty in range(ny):
            for tx in range(nx):
                x0, x1 = dw * tx // nx, dw * (tx + 1) // nx; y0, y1 = dh * ty // ny, dh * (ty + 1) // ny
                t = img[y0:y1, x0:x1]; op = t[:, :, 3] > 0
                if not op.any(): continue
                ys, xs = np.where(op); ax0, ax1, ay0, ay1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
                t = t[ay0:ay1, ax0:ax1]
                idx, pal = quantize(t)
                if pal not in pals: pals[pal] = len(pal_list); pal_list.append(pal)
                key = (idx.shape, idx.tobytes(), pals[pal])
                if key not in tile_px: tile_px[key] = [idx, None]
                tiles.append([key, x0 + ax0, y0 + ay0, pals[pal]])
        cells.append((rc, dw, dh, first, len(tiles) - first))
    order = sorted(tile_px.items(), key=lambda kv: (-kv[1][0].shape[0], -kv[1][0].shape[1]))
    mr = MaxRects(PAGE_W, PAGE_H); page = np.zeros((PAGE_H, PAGE_W), np.uint8); lost = 0; used_px = 0
    for key, v in order:
        h, w = v[0].shape
        pos = mr.place(min(w + 1, PAGE_W), min(h + 1, PAGE_H))
        if pos is None: lost += 1; continue
        v[1] = pos; page[pos[1]:pos[1] + h, pos[0]:pos[0] + w] = v[0]; used_px += (w + 1) * (h + 1)
    out = b'NDP2' + struct.pack('<HHIII', PAGE_W, PAGE_H, len(cells), len(tiles), len(pal_list))
    for (rc, dw, dh, first, n) in cells: out += struct.pack('<BBhhhhHHHH', rc[0], 0, rc[1], rc[2], rc[3], rc[4], dw, dh, first, n)
    for (key, ox, oy, pi) in tiles:
        idx, pos = tile_px[key]; h, w = idx.shape
        if pos is None: out += struct.pack('<HHHHHHHH', 0, 0, 0, 0, ox, oy, pi, 0)
        else: out += struct.pack('<HHHHHHHH', pos[0], pos[1], w, h, ox, oy, pi, 0)
    for p in pal_list: out += struct.pack('<16H', *p)
    out += (page[:, 0::2] | (page[:, 1::2] << 4)).astype(np.uint8).tobytes()
    msg = 'stage %d: %d cells, %d tiles (%d distinct), %d palettes, page %.0f%% full, %d tiles did not fit, %d empty rectangles' % (
        stage + 1, len(cells), len(tiles), len(order), len(pal_list), 100.0 * used_px / (PAGE_W * PAGE_H), lost, skipped)
    print(msg)
    if preview:
        rgba = np.zeros((PAGE_H, PAGE_W, 4), np.uint8); rgba[:, :] = (60, 60, 80, 255)
        for (key, ox, oy, pi) in tiles:
            idx, pos = tile_px[key]
            if pos is None: continue
            h, w = idx.shape; p = np.array([[(c & 31) << 3, ((c >> 5) & 31) << 3, ((c >> 10) & 31) << 3, 255] for c in pal_list[pi]], np.uint8)
            blk = p[idx]; m = idx > 0; reg = rgba[pos[1]:pos[1] + h, pos[0]:pos[0] + w]; reg[m] = blk[m]
        os.makedirs(preview, exist_ok=True); Image.fromarray(rgba, 'RGBA').save(os.path.join(preview, 'stage%d.png' % (stage + 1)))
    return out, lost, msg


def record_cells():
    here = os.path.dirname(os.path.abspath(__file__)); td = tempfile.mkdtemp(); exe = os.path.join(td, 'cells_c')
    subprocess.check_call(['gcc', '-std=c99', '-O2', os.path.join(here, '..', 'core', 'pnj_core.c'), os.path.join(here, 'cells_c.c'), '-lm', '-o', exe])
    used = {}
    for l in subprocess.check_output([exe, '60000', '30']).decode().splitlines():
        v = l.split()
        if len(v) == 4: used.setdefault(int(v[0]), set()).add(tuple(int(x) for x in v[1:]))
    return used


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('pack'); ap.add_argument('-o', '--out', default='pnj_ds.pack')
    ap.add_argument('--preview', help='folder for a PNG of each page'); ap.add_argument('--extra', help='file of "sheet x y w h" lines to add')
    ap.add_argument('--no-audio', action='store_true', help='skip the sounds (quick texture tests)')
    ap.add_argument('--music-rate', type=int, default=16000); ap.add_argument('--sfx-rate', type=int, default=22050)
    a = ap.parse_args()
    ent = read_pack(a.pack)
    atlas = [tuple(int(v) for v in l.split()) for l in ent['meta/atlas.txt'].decode().splitlines() if len(l.split()) == 6]
    tw = {}
    for sp in atlas: tw[sp[0]] = sp[5]
    sheets = [Sheet(ent['tex/%s.png' % n], tw.get(i, 512)) if 'tex/%s.png' % n in ent else None for i, n in enumerate(TEX)]
    extra = set()
    if a.extra and os.path.exists(a.extra):
        for l in open(a.extra):
            v = l.split()
            if len(v) == 5: extra.add(tuple(int(x) for x in v))
    used = record_cells()
    files = {k: v for k, v in ent.items() if k.startswith('maps/') or k.startswith('lang/') or k == 'meta/atlas.txt'}
    report = []; bad = 0
    for st in range(8):
        data, lost, msg = build_page(st, atlas, sheets, used, extra, a.preview)
        files['nds/stage%d.ndp' % (st + 1)] = data; bad += lost; report.append(msg)
    if not a.no_audio:
        tmp = tempfile.mkdtemp(); tot = 0
        for nm in sorted(k for k in ent if k.startswith('audio/')):
            base, ext = os.path.splitext(os.path.basename(nm)); rate = a.music_rate if base.startswith('bgm') else a.sfx_rate
            pcm = make_ndaudio.to_pcm(ent[nm], ext[1:], rate, os.path.join(tmp, '_t'))
            if len(pcm) < 2: continue
            data, _ = make_ndaudio.encode(pcm)
            files['nds/%s.nda' % base] = b'NDA1' + struct.pack('<III', rate, len(pcm), len(data)) + data; tot += len(data)
        report.append('audio: %.2f MB' % (tot / 1048576.0)); print(report[-1])
    files['meta.json'] = json.dumps({'kind': 'nds', 'scale': SCALE, 'report': report}).encode()
    write_pack(a.out, files)
    print('%s: %d files, %.2f MB' % (a.out, len(files), os.path.getsize(a.out) / 1048576.0))
    if bad: print('%d tiles did not fit the page' % bad); sys.exit(1)


if __name__ == '__main__':
    main()
