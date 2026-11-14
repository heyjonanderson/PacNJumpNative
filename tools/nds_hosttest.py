#!/usr/bin/env python3
"""Runs the DS build's PC test harness through the menus and all 8 stages, and writes a picture of each screen plus a list of every
source rectangle the game asked for that the DS pack did not hold.

    python3 tools/nds_hosttest.py pnj_ds.pack out_dir            needs gcc and Pillow

The harness is the real shell (platform/sdl/main.c) and the real DS drawing code (platform/nds/nds_gfx.h) built with -DPNJ_NDS_HOST, which swaps
the DS's 3D hardware for a software rasteriser and the buttons for a script. Feed misses.txt back to make_ndspack.py --extra to add what was missed.
"""
import os, random, shutil, subprocess, sys
from PIL import Image
A, B, SELECT, START, RIGHT, LEFT, UP, DOWN = 1, 2, 4, 8, 16, 32, 64, 128
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.join(HERE, '..')


class Script:
    def __init__(self): self.t = 100; self.keys = []; self.shots = []
    def press(self, k, hold=5, gap=14): self.keys.append((self.t, k)); self.keys.append((self.t + hold, 0)); self.t += hold + gap
    def wait(self, n): self.t += n
    def shot(self, name): self.shots.append((self.t, name)); self.t += 2
    def hold(self, k, n): self.keys.append((self.t, k)); self.t += n; self.keys.append((self.t, 0)); self.t += 2
    def touch(self, x, y, hold=5, gap=14): self.keys.append((self.t, '4096:%d:%d' % (x, y))); self.keys.append((self.t + hold, 0)); self.t += hold + gap


def run(exe, out, name, sc, pack):
    env = dict(os.environ)
    env['PNJ_NDS_KEYS'] = ','.join('%d:%s' % (t, k) for t, k in sc.keys)
    env['PNJ_NDS_SHOT'] = ','.join('%d:%s' % (t, os.path.join(out, '%s_%s.ppm' % (name, n))) for t, n in sc.shots)
    env['PNJ_NDS_MISSLOG'] = os.path.join(out, 'misses_raw.txt')
    wd = os.path.join(out, 'wd'); os.makedirs(wd, exist_ok=True)
    if not os.path.exists(os.path.join(wd, 'pnj_ds.pack')): shutil.copy(pack, os.path.join(wd, 'pnj_ds.pack'))
    if os.path.exists(os.path.join(wd, 'pnj.sav')): os.remove(os.path.join(wd, 'pnj.sav'))
    r = subprocess.run([exe], cwd=wd, env=env, capture_output=True, text=True, timeout=600)
    for l in r.stderr.splitlines():
        if l.startswith('shot'): print('  ' + l.replace(out + os.sep, ''))
    if r.returncode: print('  exit code', r.returncode, r.stderr[-300:])


def main():
    pack, out = sys.argv[1], sys.argv[2]; os.makedirs(out, exist_ok=True)
    exe = os.path.join(out, 'ndshost')
    subprocess.check_call(['gcc', '-std=gnu99', '-O1', '-g', '-w', '-DPNJ_NDS', '-DPNJ_NDS_HOST', '-DPNJ_FLOAT', os.path.join(ROOT, 'platform', 'sdl', 'main.c'),
                           os.path.join(ROOT, 'core', 'pnj_core.c'), '-lm', '-o', exe])
    raw = os.path.join(out, 'misses_raw.txt')
    if os.path.exists(raw): os.remove(raw)
    # menus: title, main menu, every Help page, achievements, options
    s = Script(); s.shot('title'); s.press(A); s.shot('menu')
    s.press(DOWN); s.press(DOWN); s.press(DOWN); s.press(A); s.shot('help1')            # focus starts on PLAY GAME; RECORD, OPTION, HELP
    for i in range(8): s.press(RIGHT); s.shot('help%d' % (i + 2))
    s.press(B); s.press(DOWN); s.press(DOWN); s.press(DOWN); s.press(DOWN); s.press(A); s.shot('ach1')
    for i in range(8): s.press(RIGHT); s.shot('ach%d' % (i + 2))
    s.press(B); s.press(DOWN); s.press(DOWN); s.press(A); s.shot('option'); s.press(A); s.shot('opt_sound'); s.press(B); s.press(DOWN); s.press(A); s.shot('opt_ctrl')
    s.press(B); s.press(B); s.press(DOWN); s.press(A); s.shot('record')
    print('menus'); run(exe, out, 'menus', s, pack)
    # each stage: pick it, play with the d-pad for a while (several lives), pause, game over
    random.seed(7)
    for st in range(8):
        s = Script(); s.press(A); s.press(A)
        for i in range(st): s.press(RIGHT, gap=20)
        s.wait(20); s.shot('select'); s.press(A); s.wait(40); s.shot('start')
        for i in range(60):
            s.hold(random.choice([LEFT, RIGHT, LEFT, RIGHT, 0]), random.randint(10, 70))
            if i in (8, 25, 45): s.shot('play%d' % i)
        s.press(START); s.wait(10); s.shot('pause'); s.press(START)
        for i in range(40): s.hold(random.choice([LEFT, RIGHT]), random.randint(20, 90))
        s.shot('late'); s.wait(600); s.shot('idle')
        print('stage', st + 1); run(exe, out, 'st%d' % (st + 1), s, pack)
    miss = {}
    if os.path.exists(raw):
        for l in open(raw): miss[l.strip()] = miss.get(l.strip(), 0) + 1
    with open(os.path.join(out, 'misses.txt'), 'w') as f:
        for k in sorted(miss): f.write(k + '\n')
    print('%d distinct rectangles missing' % len(miss))
    for k in sorted(miss, key=lambda k: -miss[k])[:20]: print('  %s  x%d' % (k, miss[k]))
    for f in sorted(os.listdir(out)):
        if f.endswith('.ppm'): Image.open(os.path.join(out, f)).save(os.path.join(out, f[:-4] + '.png')); os.remove(os.path.join(out, f))


if __name__ == '__main__':
    main()
