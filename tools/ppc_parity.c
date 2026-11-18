/* Endian and FPU parity check for the portable core: runs every stage with a fixed pseudo-random tilt and prints one 64-bit FNV-1a hash per run.
 * The hash mixes the draw list (values, not bytes, so the byte order does not matter). Build it for the host and for a big-endian target and diff the output.
 *   gcc -std=gnu99 -O2 -ffp-contract=off core/pnj_core.c tools/ppc_parity.c -lm -o parity_x86 && ./parity_x86 > a.txt
 *   powerpc-linux-gnu-gcc -static -std=gnu99 -O2 -ffp-contract=off core/pnj_core.c tools/ppc_parity.c -lm -o parity_ppc && qemu-ppc ./parity_ppc > b.txt
 * Add -DPNJ_FLOAT to both for the float build the consoles use. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../core/pnj_core.h"
static pnj_game G;
static uint64_t h;
static void mix(uint64_t v) { for (int i = 0; i < 8; i++) { h ^= (v >> (8 * i)) & 255; h *= 1099511628211ull; } }
static void mixf(fx v) { double d = (double)v; uint64_t u; memcpy(&u, &d, 8); mix(u); }
int main(int argc, char **argv) {
    int steps = argc > 1 ? atoi(argv[1]) : 3000, seeds = argc > 2 ? atoi(argv[2]) : 3;
    for (int stage = 0; stage < 8; stage++) for (int level = 0; level < 12; level += 4) for (int s = 1; s <= seeds; s++) {
        pnj_new(&G, 1000u * s + (unsigned)stage * 7u + (unsigned)level, stage); G.start_level = level;
        unsigned r = 12345u + (unsigned)s; double t = 0; h = 1469598103934665603ull; int games = 1;
        for (int i = 0; i < steps; i++) {
            r = r * 1664525u + 1013904223u; t += ((r >> 16) / 65535.0 - 0.5) * 0.12 - t * 0.04; if (t > 0.5) t = 0.5; if (t < -0.5) t = -0.5;
            pnj_step(&G, (fx)t);
            pnj_build_draw(&G);
            mix((uint64_t)G.ndraw); mix((uint64_t)(int64_t)pnj_height(&G)); mixf(G.p.x); mixf(G.p.y);
            for (int k = 0; k < G.ndraw; k++) { const pnj_draw *d = &G.draw[k]; mix((uint64_t)(uint16_t)d->sprite ^ ((uint64_t)(uint16_t)d->col << 16) ^ ((uint64_t)(uint16_t)d->row << 32) ^ ((uint64_t)(uint16_t)d->vrow << 48)); mixf(d->x); mixf(d->y); mixf(d->rot); mixf(d->scale); mix(d->r | d->g << 8 | d->b << 16 | (uint64_t)d->a << 24 | (uint64_t)d->blend << 32 | (uint64_t)d->layer << 40); }
            if (G.over) { pnj_new(&G, 77u * (unsigned)i + (unsigned)s, stage); G.start_level = level; games++; }
        }
        printf("stage %d level %d seed %d games %d hash %016llx\n", stage, level, s, games, (unsigned long long)h);
    }
    return 0;
}
