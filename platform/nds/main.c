/* Nintendo DS port, step 1: a benchmark ROM. It runs the portable core on the ARM9 (no FPU, soft-float) and prints how long a step takes,
 * so we know the real cost before the renderer and audio exist. The core is the same pnj_core.c as every other platform, built with -DPNJ_FLOAT.
 * Budget: the game steps 40 times a second, so a step has 25 000 us, and the draw list, 3D engine and audio share that time. */
#include <nds.h>
#include <stdio.h>
#include "../../core/pnj_core.h"

static pnj_game G;
/* Results are also left in RAM so a debugger can read them without a screen: bench_us[case * 2] is the step time, [case * 2 + 1] step plus draw list, in microseconds. */
volatile unsigned bench_us[16];
__attribute__((noinline, target("arm"))) void bench_done(void) { __asm__ volatile("nop"); }

static u32 us(u32 ticks) { return (u32)(((unsigned long long)ticks * 1000000ull) / BUS_CLOCK); }

int main(void) {
    consoleDemoInit();
    printf("Pac'N-Jump DS: core benchmark\n");
    printf("bus clock %lu Hz, step budget 25000 us\n\n", (unsigned long)BUS_CLOCK);
    printf("stage level  step    +draw   (us)\n");
    static const int cases[][2] = { {0, 0}, {0, 6}, {1, 2}, {3, 2}, {5, 2}, {7, 2} };
    for (unsigned c = 0; c < sizeof cases / sizeof cases[0]; c++) {
        int stage = cases[c][0], level = cases[c][1], steps = 600;
        pnj_new(&G, 4242u, stage); G.start_level = level;
        unsigned r = 99; double t = 0; u32 tstep = 0, tdraw = 0;
        for (int i = 0; i < steps; i++) {
            r = r * 1664525u + 1013904223u; t += ((r >> 16) / 65535.0 - 0.5) * 0.12 - t * 0.04; if (t > 0.5) t = 0.5; if (t < -0.5) t = -0.5;
            cpuStartTiming(0); pnj_step(&G, (fx)t); tstep += cpuEndTiming();
            cpuStartTiming(0); pnj_build_draw(&G); tdraw += cpuEndTiming();
            if (G.over) { pnj_new(&G, r, stage); G.start_level = level; }
        }
        bench_us[c * 2] = us(tstep / steps); bench_us[c * 2 + 1] = us((tstep + tdraw) / steps);
        printf("%3d   %3d  %5lu  %6lu\n", stage + 1, level, (unsigned long)bench_us[c * 2], (unsigned long)bench_us[c * 2 + 1]);
    }
    printf("\nDone. Compare with 25000 us.\n");
    bench_done();
    while (1) swiWaitForVBlank();
    return 0;
}
