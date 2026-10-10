#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include "finders.h"

#define MAX_TARGETS 5
#define CLUSTER_RADIUS 500.0

typedef struct {
    const char *name;
    int type;
    Pos pos;
    int found;
} Target;

double distance(Pos a, Pos b)
{
    double dx = (double)a.x - b.x;
    double dz = (double)a.z - b.z;

    return sqrt(dx * dx + dz * dz);
}

int main(void)
{
    uint64_t seed = 123456789ULL;
    int mc = MC_1_20_6;

    Target targets[MAX_TARGETS] = {
        {"Village", Village, {0, 0}, 0},
        {"Desert Pyramid", Desert_Pyramid, {0, 0}, 0},
        {"Jungle Temple", Jungle_Temple, {0, 0}, 0},
        {"Shipwreck", Shipwreck, {0, 0}, 0},
        {"Ancient City", Ancient_City, {0, 0}, 0}
    };

    printf("=== SeedLens Cluster Test ===\n");
    printf("Seed: %llu\n", (unsigned long long)seed);
    printf("Cluster radius: %.0f blocks\n\n", CLUSTER_RADIUS);

    for (int i = 0; i < MAX_TARGETS; i++)
    {
        for (int rx = -1; rx <= 1 && !targets[i].found; rx++)
        {
            for (int rz = -1; rz <= 1 && !targets[i].found; rz++)
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
                    if (pos.x >= -5000 && pos.x <= 5000 &&
                        pos.z >= -5000 && pos.z <= 5000)
                    {
                        targets[i].pos = pos;
                        targets[i].found = 1;
                    }
                }
            }
        }
    }

    printf("Nearby structure relationships:\n\n");

    for (int i = 0; i < MAX_TARGETS; i++)
    {
        if (!targets[i].found)
            continue;

        for (int j = i + 1; j < MAX_TARGETS; j++)
        {
            if (!targets[j].found)
                continue;

            double d = distance(targets[i].pos, targets[j].pos);

            if (d <= CLUSTER_RADIUS)
            {
                printf("%-20s <-> %-20s : %7.1f blocks\n",
                       targets[i].name,
                       targets[j].name,
                       d);
            }
        }
    }

    return 0;
}
