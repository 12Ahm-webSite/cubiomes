#include <stdio.h>
#include <stdint.h>
#include <math.h>

#include "finders.h"
#include "generator.h"

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

    /*
     * Initialize Cubiomes generator.
     * setupGenerator() returns void in this version.
     */
    Generator g;

    setupGenerator(&g, mc, 0);
    applySeed(&g, DIM_OVERWORLD, seed);

    printf("========================================\n");
    printf("       SeedLens Viability Test\n");
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

        int region_blocks =
            sc.regionSize * 16;

        int region_radius =
            (SEARCH_RADIUS + region_blocks - 1)
            / region_blocks;

        int candidates = 0;
        int viable = 0;

        double nearest_distance = 1e30;
        Pos nearest = {0, 0};

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

                /*
                 * Keep candidates inside
                 * the +/- 5000 block square.
                 */
                if (pos.x < -SEARCH_RADIUS ||
                    pos.x > SEARCH_RADIUS ||
                    pos.z < -SEARCH_RADIUS ||
                    pos.z > SEARCH_RADIUS)
                {
                    continue;
                }

                candidates++;

                /*
                 * Check whether the structure
                 * can actually generate here.
                 */
                if (isViableStructurePos(
                        targets[t].type,
                        &g,
                        pos.x,
                        pos.z,
                        0))
                {
                    viable++;

                    double d =
                        distance_from_spawn(pos);

                    if (d < nearest_distance)
                    {
                        nearest_distance = d;
                        nearest = pos;
                    }
                }
            }
        }

        printf("----------------------------------------\n");
        printf("%s\n", targets[t].name);
        printf("Candidates : %d\n", candidates);
        printf("Viable     : %d\n", viable);

        if (viable > 0)
        {
            printf(
                "Nearest    : X=%d Z=%d | distance=%.1f\n",
                nearest.x,
                nearest.z,
                nearest_distance
            );
        }
        else
        {
            printf("Nearest    : none\n");
        }

        printf("\n");
    }

    return 0;
}
