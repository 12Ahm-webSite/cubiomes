#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include "finders.h"

typedef struct {
    const char *name;
    int type;
} Target;

int main(void)
{
    uint64_t seed = 123456789ULL;
    int mc = MC_1_20_6;
    const int radius = 5000;

    Target targets[] = {
        {"Village", Village},
        {"Desert Pyramid", Desert_Pyramid},
        {"Jungle Temple", Jungle_Temple},
        {"Shipwreck", Shipwreck},
        {"Ancient City", Ancient_City}
    };

    int count = sizeof(targets) / sizeof(targets[0]);

    printf("=== SeedLens Discovery Test ===\n");
    printf("Minecraft: 1.20.6\n");
    printf("Seed: %llu\n", (unsigned long long)seed);
    printf("Search radius: +/- %d blocks\n\n", radius);

    for (int i = 0; i < count; i++)
    {
        Pos best;
        int found = 0;

        for (int rx = -1; rx <= 1 && !found; rx++)
        {
            for (int rz = -1; rz <= 1 && !found; rz++)
            {
                Pos pos;

                if (getStructurePos(
                        targets[i].type,
                        mc,
                        seed,
                        rx,
                        rz,
                        &pos))
                {
                    if (pos.x >= -radius && pos.x <= radius &&
                        pos.z >= -radius && pos.z <= radius)
                    {
                        best = pos;
                        found = 1;
                    }
                }
            }
        }

        if (found)
        {
            double distance = sqrt(
                (double)best.x * best.x +
                (double)best.z * best.z
            );

            printf("%-20s X=%6d Z=%6d | Spawn distance: %7.1f blocks\n",
                   targets[i].name,
                   best.x,
                   best.z,
                   distance);
        }
        else
        {
            printf("%-20s Not found\n", targets[i].name);
        }
    }

    return 0;
}
