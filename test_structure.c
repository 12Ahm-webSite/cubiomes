#include <stdio.h>
#include <stdint.h>
#include "finders.h"

int main(void)
{
    uint64_t seed = 123456789ULL;
    int mc = MC_1_20_6;

    Pos pos;

    int found = getStructurePos(Village, mc, seed, 0, 0, &pos);

    printf("SeedLens Structure test\n");
    printf("Minecraft: 1.20.6\n");
    printf("Seed: %llu\n", (unsigned long long)seed);

    if (found)
        printf("Village found at: X=%d Z=%d\n", pos.x, pos.z);
    else
        printf("No village found in region 0,0\n");

    return 0;
}
