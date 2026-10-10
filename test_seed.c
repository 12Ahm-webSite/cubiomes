#include <stdio.h>
#include <stdint.h>
#include "generator.h"

int main(void)
{
    Generator g;

    int mc = MC_1_21;
    uint64_t seed = 123456789ULL;

    setupGenerator(&g, mc, 0);
    applySeed(&g, DIM_OVERWORLD, seed);

    int biome = getBiomeAt(&g, 4, 0, 0, 0);

    printf("Cubiomes test\n");
    printf("Minecraft: 1.21\n");
    printf("Seed: %llu\n", (unsigned long long)seed);
    printf("Biome ID at spawn: %d\n", biome);

    return 0;
}
