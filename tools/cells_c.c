/* Prints every sprite cell the core draws: "sprite col row vrow" per line. Runs each stage at several start levels with a random-walk tilt and
 * collects the draw list after every step. tools/make_ndpack.py reads this to know which animation cells to pack for the DS.
 *   gcc -std=c99 -O2 core/pnj_core.c tools/cells_c.c -lm -o cells_c && ./cells_c > nds_cells.txt */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/pnj_core.h"
static pnj_game G;
static unsigned char seen[512][32][32][8];
int main(int argc, char **argv) {
    int steps = argc > 1 ? atoi(argv[1]) : 6000, seeds = argc > 2 ? atoi(argv[2]) : 6;
    for (int stage = 0; stage < 8; stage++) for (int level = 0; level < 12; level += (stage == 0 ? 1 : 3)) for (int s = 1; s <= seeds; s++) {
        pnj_new(&G, 1000u * s + (unsigned)stage * 7u + (unsigned)level, stage); G.start_level = level;
        unsigned r = 12345u + (unsigned)s; double t = 0;
        for (int i = 0; i < steps; i++) {
            r = r * 1664525u + 1013904223u; t += ((r >> 16) / 65535.0 - 0.5) * 0.12 - t * 0.04; if (t > 0.5) t = 0.5; if (t < -0.5) t = -0.5;
            pnj_step(&G, (fx)t);
            if (G.overflow) break;
            pnj_build_draw(&G);
            for (int k = 0; k < G.ndraw; k++) { const pnj_draw *d = &G.draw[k]; if (d->sprite >= 0 && d->sprite < 512 && d->col >= 0 && d->col < 32 && d->row >= 0 && d->row < 32 && d->vrow >= 0 && d->vrow < 8) seen[d->sprite][d->col][d->row][d->vrow] = 1; }
            if (G.over) { pnj_new(&G, 77u * (unsigned)i + (unsigned)s, stage); G.start_level = level; }
        }
    }
    for (int a = 0; a < 512; a++) for (int c = 0; c < 32; c++) for (int r = 0; r < 32; r++) for (int v = 0; v < 8; v++) if (seen[a][c][r][v]) printf("%d %d %d %d\n", a, c, r, v);
    return 0;
}
