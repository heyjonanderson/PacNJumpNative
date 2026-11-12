/* Runs the core for N steps (optionally building the draw list each step) so an instruction counter can price it.
 * Used with an ARMv5 soft-float build under qemu-user to estimate the DS ARM9 cost: see tools/nds_cost.sh.   bench stage level steps draw */
#include <stdio.h>
#include <stdlib.h>
#include "../core/pnj_core.h"
static pnj_game G;
int main(int argc, char **argv) {
    int stage = argc > 1 ? atoi(argv[1]) : 0, level = argc > 2 ? atoi(argv[2]) : 0, steps = argc > 3 ? atoi(argv[3]) : 100, draw = argc > 4 ? atoi(argv[4]) : 0;
    pnj_new(&G, 4242u, stage); G.start_level = level;
    unsigned r = 99; double t = 0, acc = 0;
    for (int i = 0; i < steps; i++) {
        r = r * 1664525u + 1013904223u; t += ((r >> 16) / 65535.0 - 0.5) * 0.12 - t * 0.04; if (t > 0.5) t = 0.5; if (t < -0.5) t = -0.5;
        pnj_step(&G, (fx)t);
        if (draw) { pnj_build_draw(&G); acc += G.ndraw; }
        if (G.over) { pnj_new(&G, r, stage); G.start_level = level; }
    }
    printf("%.0f\n", acc);
    return 0;
}
