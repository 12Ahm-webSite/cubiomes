#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include "finders.h"

#define SEARCH_RADIUS 5000
#define MAX_RESULTS 100

typedef struct {
    const char *name;
    int type;
} StructureTarget;

typedef struct {
    const char *name;
    Pos pos;
    double spawn_distance;
} Discovery;

double distance_from_spawn(Pos p)
{
    return sqrt((double)p.x * p.x + (double)p.z * p.z);
}

double distance_between(Pos a, Pos b)
{
    double dx = (double)a.x - b.x;
    double dz = (double)a.z - b.z;

    return sqrt(dx * dx + dz * dz);
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

    int target_count = sizeof(targets) / sizeof(targets[0]);

    Discovery results[MAX_RESULTS];
    int result_count = 0;

    printf("========================================\n");
    printf("        SeedLens Discovery Engine\n");
    printf("========================================\n\n");

    printf("Seed: %llu\n", (unsigned long long)seed);
    printf("Minecraft: 1.20.6\n");
    printf("Search radius: +/- %d blocks\n\n", SEARCH_RADIUS);

    /*
     * Search structure regions covering the requested area.
     *
     * Cubiomes structure regions are based on region coordinates,
     * so we scan a wider region grid around spawn.
     */
    for (int t = 0; t < target_count; t++)
    {
        int found_for_type = 0;

        for (int rx = -10; rx <= 10; rx++)
        {
            for (int rz = -10; rz <= 10; rz++)
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

                if (result_count >= MAX_RESULTS)
                    break;

                results[result_count].name = targets[t].name;
                results[result_count].pos = pos;
                results[result_count].spawn_distance =
                    distance_from_spawn(pos);

                result_count++;
                found_for_type++;
            }

            if (result_count >= MAX_RESULTS)
                break;
        }

        printf("%-20s : %d found\n",
               targets[t].name,
               found_for_type);
    }

    printf("\n========================================\n");
    printf("Discovered structures: %d\n", result_count);
    printf("========================================\n\n");

    for (int i = 0; i < result_count; i++)
    {
        printf(
            "%-20s X=%6d Z=%6d | Spawn: %7.1f\n",
            results[i].name,
            results[i].pos.x,
            results[i].pos.z,
            results[i].spawn_distance
        );
    }

    printf("\n========================================\n");
    printf("Nearby relationships <= 500 blocks\n");
    printf("========================================\n\n");

    int relationships = 0;

    for (int i = 0; i < result_count; i++)
    {
        for (int j = i + 1; j < result_count; j++)
        {
            double d = distance_between(
                results[i].pos,
                results[j].pos
            );

            if (d <= 500.0)
            {
                printf(
                    "%-20s <-> %-20s : %7.1f blocks\n",
                    results[i].name,
                    results[j].name,
                    d
                );

                relationships++;
            }
        }
    }

    printf("\nRelationships: %d\n", relationships);

    return 0;
}
