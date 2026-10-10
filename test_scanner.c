#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include "finders.h"

#define SEARCH_RADIUS 5000

typedef struct {
    const char *name;
    int type;
} StructureTarget;

double distance_from_spawn(Pos p)
{
    return sqrt(
        (double)p.x * p.x +
        (double)p.z * p.z
    );
}

int main(void)
{
    uint64_t seed = 123456789ULL;
    int mc = MC_1_20_6;

    StructureTarget targets[] = {
        {"Village", Village},
        {"Desert Pyramid", Desert_Pyramid},
        {"Jungle Temple", Jungle_Temple},
        {"Shipwreck", Shipwreck},
        {"Ancient City", Ancient_City}
    };

    int target_count =
        sizeof(targets) / sizeof(targets[0]);

    printf("========================================\n");
    printf("       SeedLens Structure Scanner\n");
    printf("========================================\n\n");

    printf("Seed: %llu\n",
           (unsigned long long)seed);

    printf("Minecraft: 1.20.6\n");
    printf("Search radius: +/- %d blocks\n\n",
           SEARCH_RADIUS);

    for (int t = 0; t < target_count; t++)
    {
        StructureConfig sc;

        if (!getStructureConfig(
                targets[t].type,
                mc,
                &sc))
        {
            printf("%s: unsupported\n\n",
                   targets[t].name);
            continue;
        }

        /*
         * One region covers:
         *
         * regionSize * 16 blocks
         */
        int region_blocks =
            sc.regionSize * 16;

        int region_radius =
            (SEARCH_RADIUS + region_blocks - 1)
            / region_blocks;

        printf("----------------------------------------\n");
        printf("%s\n", targets[t].name);
        printf("regionSize : %d chunks\n",
               sc.regionSize);
        printf("chunkRange : %d chunks\n",
               sc.chunkRange);
        printf("region span: %d blocks\n",
               region_blocks);
        printf("region scan: -%d .. +%d\n\n",
               region_radius,
               region_radius);

        int found = 0;

        for (int rz = -region_radius;
             rz <= region_radius;
             rz++)
        {
            for (int rx = -region_radius;
                 rx <= region_radius;
                 rx++)
            {
                Pos pos;

                if (!getStructurePos(
                        targets[t].type,
                        mc,
                        seed,
                        rx,
                        rz,
                        &pos))
                {
                    continue;
                }

                if (pos.x < -SEARCH_RADIUS ||
                    pos.x > SEARCH_RADIUS ||
                    pos.z < -SEARCH_RADIUS ||
                    pos.z > SEARCH_RADIUS)
                {
                    continue;
                }

                double d =
                    distance_from_spawn(pos);

                printf(
                    "  X=%6d Z=%6d | distance=%7.1f\n",
                    pos.x,
                    pos.z,
                    d
                );

                found++;
            }
        }

        printf("\nFound: %d\n\n", found);
    }

    return 0;
}
