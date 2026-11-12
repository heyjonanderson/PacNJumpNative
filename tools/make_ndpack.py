#!/usr/bin/env python3
"""Builds the Nintendo DS texture data from a pnj.pack (which is built from your own APK by make_pack.py).

    python3 tools/make_ndpack.py pnj.pack -o nds_out            writes nds_out/page_*.bin, page_*.cel, page_*.png (a preview) and report.txt
    python3 tools/make_ndpack.py pnj.pack --scale 0.5 --size 512

The DS has 512 KB of texture memory, so every sprite is shrunk to SCALE of its on-screen size in the 320x480 field (default 0.5), packed into one page
per stage (plus one for the menus), and the page is quantised to 255 colours plus a transparent index 0. A sprite in the atlas is a grid of animation
cells (col, row, vrow pick one, see spr() in platform/sdl/main.c), so each cell is shrunk and packed on its own and listed in the page's .cel table.

Files (little endian):
  page_<name>.bin   "NDP1", u16 width, u16 height, then 256 palette entries (u16, BGR555, entry 0 = transparent), then width * height index bytes.
  page_<name>.cel   "NDC1", u32 count, then count x { u16 sprite, u8 col, u8 row, u8 vrow, u8 pad, u16 x, u16 y, u16 w, u16 h } sorted by (sprite, col, row, vrow).
Needs numpy and Pillow.
"""
import argparse, io, os, struct, sys
import numpy as np
from PIL import Image

TEX = ['bnglogo', 'bndr_logo', 'sysparts', 'stages', 'charaparts'] + ['stage_%d_%s' % (i, k) for i in range(1, 9) for k in ('map', 'cha')]


def read_pack(path):
    d = open(path, 'rb').read()
    if d[:8] != b'PNJPACK1': sys.exit('not a pnj.pack')
    n = struct.unpack('<I', d[8:12])[0]; p = 12; ent = {}
    for _ in range(n):
        l = struct.unpack('<H', d[p:p + 2])[0]; p += 2; nm = d[p:p + l].decode(); p += l
        off, sz = struct.unpack('<II', d[p:p + 8]); p += 8; ent[nm] = d[off:off + sz]
    return ent


def parse_atlas(txt):
    return [tuple(int(v) for v in l.split()) for l in txt.decode().splitlines() if len(l.split()) == 6]


class Sheet:
    def __init__(self, png):
        im = Image.open(io.BytesIO(png)).convert('RGBA')
        self.w, self.h = im.size
        self.px = np.array(im)
        self.opaque = self.px[:, :, 3] > 8


def cell_src(sp, sheet, col, row, vrow):
    """Source rectangle (sheet pixels) of one animation cell, or None when it falls outside the sheet."""
    t, x, y, w, h, tw = sp
    k = sheet.w / tw
    sx = x + col * (w + 2); sy = y + row * (h + 2) + vrow * h
    x0, y0, x1, y1 = int(sx * k + 0.5), int(sy * k + 0.5), int((sx + w) * k + 0.5), int((sy + h) * k + 0.5)
    if x0 < 0 or y0 < 0 or x1 > sheet.w or y1 > sheet.h or x1 <= x0 or y1 <= y0: return None
    return x0, y0, x1, y1


def has_pixels(sheet, r):
    return r is not None and bool(sheet.opaque[r[1]:r[3], r[0]:r[2]].any())


def cells_of(sid, sp, sheet, used):
    """The (col, row, vrow) cells of this sprite to pack: the ones the core draws (tools/cells_c.c) plus cell 0, and only those that hold pixels."""
    keys = set(used.get(sid, ())) | {(0, 0, 0)}
    return sorted(k for k in keys if has_pixels(sheet, cell_src(sp, sheet, *k)))


def read_cells(path):
    used = {}
    for l in open(path):
        v = l.split()
        if len(v) == 4: used.setdefault(int(v[0]), set()).add(tuple(int(x) for x in v[1:]))
    for c in range(4): used.setdefault(92, set()).add((c, 0, 0))     # the title screen's Pac-Man frames are picked by the menu code, not the core
    return used


def shrink(sheet, r, ow, oh):
    """Alpha-weighted (premultiplied) resize of a source rectangle to ow x oh."""
    a = sheet.px[r[1]:r[3], r[0]:r[2]].astype(np.float32)
    pm = a.copy(); pm[:, :, :3] *= a[:, :, 3:4] / 255.0
    im = Image.fromarray(pm.astype(np.uint8), 'RGBA').resize((ow, oh), Image.LANCZOS if (r[2] - r[0]) > ow else Image.NEAREST)
    o = np.array(im).astype(np.float32); al = o[:, :, 3:4]
    o[:, :, :3] = np.where(al > 0, o[:, :, :3] * 255.0 / np.maximum(al, 1), 0)
    return np.clip(o, 0, 255).astype(np.uint8)


class MaxRects:
    def __init__(self, w, h): self.w, self.h = w, h; self.free = [(0, 0, w, h)]

    def place(self, rw, rh):
        best = None
        for (fx, fy, fw, fh) in self.free:
            if rw <= fw and rh <= fh:
                sc = (min(fw - rw, fh - rh), max(fw - rw, fh - rh))
                if best is None or sc < best[0]: best = (sc, fx, fy)
        if best is None: return None
        x, y = best[1], best[2]; nf = []
        for (fx, fy, fw, fh) in self.free:
            if x >= fx + fw or x + rw <= fx or y >= fy + fh or y + rh <= fy: nf.append((fx, fy, fw, fh)); continue
            if x > fx: nf.append((fx, fy, x - fx, fh))
            if x + rw < fx + fw: nf.append((x + rw, fy, fx + fw - (x + rw), fh))
            if y > fy: nf.append((fx, fy, fw, y - fy))
            if y + rh < fy + fh: nf.append((fx, y + rh, fw, fy + fh - (y + rh)))
        self.free = [f for i, f in enumerate(nf) if not any(j != i and g[0] <= f[0] and g[1] <= f[1] and g[0] + g[2] >= f[0] + f[2] and g[1] + g[3] >= f[1] + f[3] for j, g in enumerate(nf))]
        return x, y


def quantize(rgba):
    """255 colours plus index 0 = transparent. Returns indices, palette (256 x RGB), mean abs error over opaque pixels."""
    op = rgba[:, :, 3] >= 128
    rgb = np.where(op[:, :, None], rgba[:, :, :3], 0).astype(np.uint8)
    q = Image.fromarray(rgb, 'RGB').quantize(colors=255, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    idx = np.array(q).astype(np.uint8) + 1
    pal = np.zeros((256, 3), np.uint8); p = np.array(q.getpalette()[:255 * 3], np.uint8).reshape(-1, 3); pal[1:1 + len(p)] = p
    idx[~op] = 0
    err = float(np.abs(pal[idx[op]].astype(int) - rgba[:, :, :3][op].astype(int)).mean()) if op.any() else 0.0
    return idx, pal, err


def build_page(name, tex_ids, atlas, sheets, used, scale, size, out, report):
    cells = []   # (sprite id, col, row, vrow, w, h, pixels)
    seen = {}
    for sid, sp in enumerate(atlas):
        if sp[0] not in tex_ids: continue
        sh = sheets[sp[0]]
        for (c, r, v) in cells_of(sid, sp, sh, used):
            src = cell_src(sp, sh, c, r, v)
            kk = (sp[0], src)
            ow, oh = max(1, int(round(sp[3] * scale))), max(1, int(round(sp[4] * scale)))
            if kk in seen and seen[kk][0] == (ow, oh): cells.append((sid, c, r, v, ow, oh, seen[kk][1])); continue
            seen[kk] = ((ow, oh), shrink(sh, src, ow, oh))
            cells.append((sid, c, r, v, ow, oh, seen[kk][1]))
    uniq = {}
    for cl in cells: uniq.setdefault(id(cl[6]), cl[6])
    order = sorted(uniq.values(), key=lambda p: -(p.shape[0] * p.shape[1]) - p.shape[0] * 0.001)
    W = H = size
    mr = MaxRects(W, H); where = {}; over = 0
    for pxl in order:
        h, w = pxl.shape[:2]
        pos = mr.place(w + 1, h + 1)
        if pos is None: over += 1; continue
        where[id(pxl)] = pos
    page = np.zeros((H, W, 4), np.uint8)
    for pxl in order:
        if id(pxl) in where:
            x, y = where[id(pxl)]; page[y:y + pxl.shape[0], x:x + pxl.shape[1]] = pxl
    used = sum(p.shape[0] * p.shape[1] for p in order)
    idx, pal, err = quantize(page)
    # files
    with open(os.path.join(out, 'page_%s.bin' % name), 'wb') as f:
        f.write(b'NDP1' + struct.pack('<HH', W, H))
        for i in range(256):
            r, g, b = (int(v) for v in pal[i]); f.write(struct.pack('<H', (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) | (0x8000 if i else 0)))
        f.write(idx.tobytes())
    rows = sorted((cl[0], cl[1], cl[2], cl[3], where[id(cl[6])]) for cl in cells if id(cl[6]) in where)
    with open(os.path.join(out, 'page_%s.cel' % name), 'wb') as f:
        f.write(b'NDC1' + struct.pack('<I', len(rows)))
        cl_by = {(c[0], c[1], c[2], c[3]): c for c in cells}
        for (sid, c, r, v, pos) in rows:
            cl = cl_by[(sid, c, r, v)]
            f.write(struct.pack('<HBBBBHHHH', sid, c, r, v, 0, pos[0], pos[1], cl[4], cl[5]))
    prev = Image.fromarray(idx, 'P'); prev.putpalette(pal.flatten().tolist()); prev.info['transparency'] = 0
    prev.save(os.path.join(out, 'page_%s.png' % name), transparency=0)
    msg = '%-8s %4d cells (%4d distinct), %6d px used of %d (%.0f%%), %d did not fit, colour error %.2f/255' % (name, len(cells), len(order), used, W * H, 100.0 * used / (W * H), over, err)
    print(msg); report.append(msg)
    return over


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('pack'); ap.add_argument('-o', '--out', default='nds_out')
    ap.add_argument('--cells', default=None, help='cell list from tools/cells_c.c (default: built and run here when gcc is available)')
    ap.add_argument('--scale', type=float, default=0.5, help='fraction of the on-screen size in the 320x480 field (default 0.5)')
    ap.add_argument('--size', type=int, default=512, help='page width and height (default 512, 256 KB as 8-bit)')
    a = ap.parse_args()
    ent = read_pack(a.pack); os.makedirs(a.out, exist_ok=True)
    cf = a.cells
    if not cf:
        import subprocess, tempfile
        here = os.path.dirname(os.path.abspath(__file__)); td = tempfile.mkdtemp(); exe = os.path.join(td, 'cells_c'); cf = os.path.join(td, 'cells.txt')
        subprocess.check_call(['gcc', '-std=c99', '-O2', os.path.join(here, '..', 'core', 'pnj_core.c'), os.path.join(here, 'cells_c.c'), '-lm', '-o', exe])
        open(cf, 'w').write(subprocess.check_output([exe, '60000', '30']).decode())
    used = read_cells(cf)
    atlas = parse_atlas(ent['meta/atlas.txt'])
    sheets = [Sheet(ent['tex/%s.png' % n]) if 'tex/%s.png' % n in ent else None for n in TEX]
    report = []; bad = 0
    bad += build_page('menu', {0, 1, 2, 3}, atlas, sheets, used, a.scale, a.size, a.out, report)
    for st in range(1, 9):
        bad += build_page('stage%d' % st, {2, 4, 5 + 2 * (st - 1), 6 + 2 * (st - 1)}, atlas, sheets, used, a.scale, a.size, a.out, report)
    open(os.path.join(a.out, 'report.txt'), 'w').write('\n'.join(report) + '\n')
    if bad: print('%d cells did not fit: try a bigger --size or a smaller --scale' % bad); sys.exit(1)


if __name__ == '__main__':
    main()
