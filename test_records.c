#include <stdio.h>
#include <stdint.h>
#include <math.h>

#include "finders.h"
#include "generator.h"

#define SEARCH_RADIUS 5000
#define MAX_STRUCTURES 4096

typedef struct {
    const char *name;
    int type;
} StructureTarget;

typedef struct {
    int type;
    const char *name;
    int x;
    int z;
    double distance;
} StructureRecord;

double distance_from_spawn(Pos p)
{
    return sqrt(
        (double)p.x * p.x +
        (double)p.z * p.z
    );
}

double distance_between(
    StructureRecord a,
    StructureRecord b
)
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

    int target_count =
        sizeof(targets) / sizeof(targets[0]);

    StructureRecord records[MAX_STRUCTURES];
    int record_count = 0;

    Generator g;

    setupGenerator(&g, mc, 0);
    applySeed(&g, DIM_OVERWORLD, seed);

    printf("========================================\n");
    printf("       SeedLens Structure Records\n");
    printf("========================================\n\n");

    printf("Seed: %llu\n",
           (unsigned long long)seed);

    printf("Minecraft: 1.20.6\n");
    printf("Search radius: +/- %d blocks\n\n",
           SEARCH_RADIUS);

    /*
     * ========================================
     * STEP 1
     * Scan every structure type.
     * ========================================
     */

    for (int t = 0; t < target_count; t++)
    {
        StructureConfig sc;

        if (!getStructureConfig(
                targets[t].type,
                mc,
                &sc))
        {
            continue;
        }

        int region_blocks =
            sc.regionSize * 16;

        int region_radius =
            (SEARCH_RADIUS + region_blocks - 1)
            / region_blocks;

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
                 * Square search area.
                 */
                if (pos.x < -SEARCH_RADIUS ||
                    pos.x > SEARCH_RADIUS ||
                    pos.z < -SEARCH_RADIUS ||
                    pos.z > SEARCH_RADIUS)
                {
                    continue;
                }

                /*
                 * Candidate -> Viable
                 */
                if (!isViableStructurePos(
                        targets[t].type,
                        &g,
                        pos.x,
                        pos.z,
                        0))
                {
                    continue;
                }

                /*
                 * Safety limit.
                 */
                if (record_count >= MAX_STRUCTURES)
                {
                    fprintf(stderr,
                            "WARNING: MAX_STRUCTURES reached\n");
                    goto scanning_done;
                }

                StructureRecord *r =
                    &records[record_count++];

                r->type =
                    targets[t].type;

                r->name =
                    targets[t].name;

                r->x =
                    pos.x;

                r->z =
                    pos.z;

                r->distance =
                    distance_from_spawn(pos);
            }
        }
    }

scanning_done:

    /*
     * ========================================
     * STEP 2
     * Summary.
     * ========================================
     */

    printf("========================================\n");
    printf("         RECORD SUMMARY\n");
    printf("========================================\n\n");

    printf("Total viable structures: %d\n\n",
           record_count);

    for (int t = 0; t < target_count; t++)
    {
        int count = 0;

        for (int i = 0; i < record_count; i++)
        {
            if (records[i].type ==
                targets[t].type)
            {
                count++;
            }
        }

        printf("%-20s : %d\n",
               targets[t].name,
               count);
    }

    /*
     * ========================================
     * STEP 3
     * Show first 30 records.
     * ========================================
     */

    printf("\n========================================\n");
    printf("       FIRST STRUCTURE RECORDS\n");
    printf("========================================\n\n");

    int preview =
        record_count < 30
        ? record_count
        : 30;

    for (int i = 0; i < preview; i++)
    {
        printf(
            "#%03d %-20s X=%6d Z=%6d | spawn=%7.1f\n",
            i + 1,
            records[i].name,
            records[i].x,
            records[i].z,
            records[i].distance
        );
    }

    /*
     * ========================================
     * STEP 4
     * Find closest pair of DIFFERENT types.
     * ========================================
     */

    double closest_distance = 1e30;

    int closest_a = -1;
    int closest_b = -1;

    for (int i = 0; i < record_count; i++)
    {
        for (int j = i + 1;
             j < record_count;
             j++)
        {
            /*
             * We want relationships between
             * different structure types.
             */
            if (records[i].type ==
                records[j].type)
            {
                continue;
            }

            double d =
                distance_between(
                    records[i],
                    records[j]
                );

            if (d < closest_distance)
            {
                closest_distance = d;
                closest_a = i;
                closest_b = j;
            }
        }
    }

    printf("\n========================================\n");
    printf("          CLOSEST DIFFERENT TYPES\n");
    printf("========================================\n\n");

    if (closest_a >= 0)
    {
        printf(
            "%s <-> %s\n",
            records[closest_a].name,
            records[closest_b].name
        );

        printf(
            "A: X=%d Z=%d\n",
            records[closest_a].x,
            records[closest_a].z
        );

        printf(
            "B: X=%d Z=%d\n",
            records[closest_b].x,
            records[closest_b].z
        );

        printf(
            "Distance: %.1f blocks\n",
            closest_distance
        );
    }
    else
    {
        printf("No pair found.\n");
    }

    /*
     * ========================================
     * STEP 5
     * Find all cross-type relationships <= 500.
     * ========================================
     */

    printf("\n========================================\n");
    printf("       RELATIONSHIPS <= 500 BLOCKS\n");
    printf("========================================\n\n");

    int relationships = 0;

    for (int i = 0; i < record_count; i++)
    {
        for (int j = i + 1;
             j < record_count;
             j++)
        {
            if (records[i].type ==
                records[j].type)
            {
                continue;
            }

            double d =
                distance_between(
                    records[i],
                    records[j]
                );

            if (d <= 500.0)
            {
                printf(
                    "%-20s <-> %-20s : %7.1f\n",
                    records[i].name,
                    records[j].name,
                    d
                );

                relationships++;
            }
        }
    }

    printf("\nTotal relationships <= 500: %d\n",
           relationships);

    return 0;
}
