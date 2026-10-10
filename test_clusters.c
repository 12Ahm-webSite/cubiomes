#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <string.h>

#include "generator.h"
#include "finders.h"
#include "biomes.h"

#define SEARCH_RADIUS 5000
#define MAX_STRUCTURES 4096

#define CLUSTER_RADIUS 500.0
#define MAX_CLUSTER_DIAMETER 800.0

#define MIN_CLUSTER_STRUCTURES 3
#define MIN_CLUSTER_TYPES 2

#define MAX_CLUSTERS 20

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

typedef struct {
    int indices[MAX_STRUCTURES];

    int count;
    int type_count;

    double center_x;
    double center_z;

    double nearest_spawn;

    double max_distance;

    double closest_distance;
    int closest_a;
    int closest_b;

    double density;
} Cluster;


/* =========================================================
   Distance Helpers
   ========================================================= */

double distance_xy(
    double x1,
    double z1,
    double x2,
    double z2
)
{
    double dx = x1 - x2;
    double dz = z1 - z2;

    return sqrt(dx * dx + dz * dz);
}


double distance_records(
    const StructureRecord *a,
    const StructureRecord *b
)
{
    return distance_xy(
        a->x,
        a->z,
        b->x,
        b->z
    );
}


/* =========================================================
   Collect Viable Structures
   ========================================================= */

int collect_records(
    uint64_t seed,
    int mc,
    StructureTarget *targets,
    int target_count,
    StructureRecord *records
)
{
    Generator g;

    setupGenerator(
        &g,
        mc,
        0
    );

    applySeed(
        &g,
        DIM_OVERWORLD,
        seed
    );

    int record_count = 0;

    for (int t = 0; t < target_count; t++)
    {
        StructureConfig config;

        if (!getStructureConfig(
                targets[t].type,
                mc,
                &config))
        {
            continue;
        }

        /*
         * Calculate enough structure regions
         * to cover the requested search bounds.
         */
        int region_range =
            (SEARCH_RADIUS /
             (config.regionSize * 16)) + 2;

        for (int rz = -region_range;
             rz <= region_range;
             rz++)
        {
            for (int rx = -region_range;
                 rx <= region_range;
                 rx++)
            {
                Pos pos;

                /*
                 * Find candidate structure position.
                 */
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
                 * Keep only positions inside
                 * the requested square bounds.
                 */
                if (pos.x < -SEARCH_RADIUS ||
                    pos.x > SEARCH_RADIUS ||
                    pos.z < -SEARCH_RADIUS ||
                    pos.z > SEARCH_RADIUS)
                {
                    continue;
                }

                /*
                 * Check biome/structure viability.
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
                 * Protect the records array.
                 */
                if (record_count >= MAX_STRUCTURES)
                {
                    return record_count;
                }

                records[record_count].type =
                    targets[t].type;

                records[record_count].name =
                    targets[t].name;

                records[record_count].x =
                    pos.x;

                records[record_count].z =
                    pos.z;

                records[record_count].distance =
                    sqrt(
                        (double)pos.x * pos.x +
                        (double)pos.z * pos.z
                    );

                record_count++;
            }
        }
    }

    return record_count;
}


/* =========================================================
   Build Cluster
   ========================================================= */

/* =========================================================
   Build Cluster
   ========================================================= */

Cluster build_cluster(
    StructureRecord *records,
    int count,
    int center_index
)
{
    Cluster cluster;

    memset(
        &cluster,
        0,
        sizeof(cluster)
    );

    cluster.closest_distance = 1e30;
    cluster.nearest_spawn = 1e30;
    cluster.density = 0.0;

    int type_seen[FEATURE_NUM];

    memset(
        type_seen,
        0,
        sizeof(type_seen)
    );

    StructureRecord *center =
        &records[center_index];


    /*
     * -----------------------------------------------------
     * PASS 1
     *
     * Collect all viable structures within the local
     * discovery radius.
     * -----------------------------------------------------
     */

    for (int i = 0; i < count; i++)
    {
        double d =
            distance_records(
                center,
                &records[i]
            );

        if (d > CLUSTER_RADIUS)
        {
            continue;
        }

        if (cluster.count >= MAX_STRUCTURES)
        {
            break;
        }

        cluster.indices[
            cluster.count
        ] = i;

        cluster.count++;


        /*
         * Count distinct structure types.
         */

        if (records[i].type >= 0 &&
            records[i].type < FEATURE_NUM)
        {
            if (!type_seen[
                    records[i].type])
            {
                type_seen[
                    records[i].type
                ] = 1;

                cluster.type_count++;
            }
        }


        /*
         * Nearest structure to spawn.
         */

        if (records[i].distance <
            cluster.nearest_spawn)
        {
            cluster.nearest_spawn =
                records[i].distance;
        }
    }


    /*
     * -----------------------------------------------------
     * PASS 2
     *
     * Calculate geometric center.
     * -----------------------------------------------------
     */

    if (cluster.count > 0)
    {
        double sx = 0.0;
        double sz = 0.0;

        for (int i = 0;
             i < cluster.count;
             i++)
        {
            StructureRecord *r =
                &records[
                    cluster.indices[i]
                ];

            sx += r->x;
            sz += r->z;
        }

        cluster.center_x =
            sx / cluster.count;

        cluster.center_z =
            sz / cluster.count;
    }


    /*
     * -----------------------------------------------------
     * PASS 3
     *
     * Calculate closest pair and maximum diameter.
     * -----------------------------------------------------
     */

    for (int i = 0;
         i < cluster.count;
         i++)
    {
        for (int j = i + 1;
             j < cluster.count;
             j++)
        {
            StructureRecord *a =
                &records[
                    cluster.indices[i]
                ];

            StructureRecord *b =
                &records[
                    cluster.indices[j]
                ];

            double d =
                distance_records(
                    a,
                    b
                );


            /*
             * Closest pair.
             */

            if (d <
                cluster.closest_distance)
            {
                cluster.closest_distance =
                    d;

                cluster.closest_a =
                    cluster.indices[i];

                cluster.closest_b =
                    cluster.indices[j];
            }


            /*
             * Maximum pairwise distance.
             *
             * This is the actual cluster diameter.
             */

            if (d >
                cluster.max_distance)
            {
                cluster.max_distance =
                    d;
            }
        }
    }


    /*
     * -----------------------------------------------------
     * PASS 4
     *
     * Reject clusters whose real diameter exceeds
     * the allowed maximum.
     *
     * IMPORTANT:
     * Do this AFTER calculating all metrics,
     * but BEFORE returning the cluster.
     * -----------------------------------------------------
     */

    if (cluster.count >= 2 &&
        cluster.max_distance >
            MAX_CLUSTER_DIAMETER)
    {
        /*
         * Mark cluster as invalid.
         */

        cluster.count = 0;
        cluster.type_count = 0;

        return cluster;
    }


    /*
     * -----------------------------------------------------
     * PASS 5
     *
     * Calculate density.
     *
     * We use a circular area whose diameter is the
     * cluster's maximum span.
     * -----------------------------------------------------
     */

    if (cluster.count >= 2 &&
        cluster.max_distance > 0.0)
    {
        double radius =
            cluster.max_distance / 2.0;

        double area =
            M_PI *
            radius *
            radius;

        cluster.density =
            (double)cluster.count /
            area;
    }
    else
    {
        cluster.density = 0.0;
    }


    return cluster;
}
/* =========================================================
   Cluster Ranking Helper
   ========================================================= */

int cluster_better(
    const Cluster *a,
    const Cluster *b
)
{
    /*
     * More distinct types first.
     */
    if (a->type_count !=
        b->type_count)
    {
        return
            a->type_count >
            b->type_count;
    }


    /*
     * Then more structures.
     */
    if (a->count !=
        b->count)
    {
        return
            a->count >
            b->count;
    }


    /*
     * Then closer to spawn.
     */
    return
        a->nearest_spawn <
        b->nearest_spawn;
}


/* =========================================================
   Cluster Similarity
   ========================================================= */

int same_cluster(
    const Cluster *a,
    const Cluster *b
)
{
    /*
     * -----------------------------------------------------
     * Rule 1:
     * If their centers are very close, treat them as
     * the same geographic cluster.
     * -----------------------------------------------------
     */

    double center_distance =
        distance_xy(
            a->center_x,
            a->center_z,
            b->center_x,
            b->center_z
        );

    if (center_distance < 250.0)
    {
        return 1;
    }


    /*
     * -----------------------------------------------------
     * Rule 2:
     * Compare their actual members.
     *
     * This handles cases where two candidate clusters
     * have slightly different centers but represent
     * almost the same structures.
     * -----------------------------------------------------
     */

    int intersection = 0;

    for (int i = 0;
         i < a->count;
         i++)
    {
        int index_a =
            a->indices[i];

        for (int j = 0;
             j < b->count;
             j++)
        {
            if (index_a ==
                b->indices[j])
            {
                intersection++;
                break;
            }
        }
    }


    /*
     * If 60% or more of the smaller cluster's members
     * are shared, treat them as the same cluster.
     */

    int smaller_count =
        a->count < b->count
        ? a->count
        : b->count;

    if (smaller_count > 0)
    {
        double overlap =
            (double)intersection /
            (double)smaller_count;

        if (overlap >= 0.60)
        {
            return 1;
        }
    }


    return 0;
}


int detect_clusters(
    StructureRecord *records,
    int record_count,
    Cluster *final_clusters,
    int *candidate_count_out
)
{
    static Cluster candidates[MAX_STRUCTURES];
    int candidate_count = 0;

    for (int i = 0; i < record_count; i++)
    {
        Cluster cluster =
            build_cluster(
                records,
                record_count,
                i
            );

        if (cluster.count < MIN_CLUSTER_STRUCTURES ||
            cluster.type_count < MIN_CLUSTER_TYPES)
        {
            continue;
        }

        if (candidate_count >= MAX_STRUCTURES)
        {
            break;
        }

        candidates[candidate_count++] = cluster;
    }

    for (int i = 0; i < candidate_count; i++)
    {
        for (int j = i + 1; j < candidate_count; j++)
        {
            if (cluster_better(&candidates[j], &candidates[i]))
            {
                Cluster temp = candidates[i];
                candidates[i] = candidates[j];
                candidates[j] = temp;
            }
        }
    }

    int final_count = 0;

    for (int i = 0;
         i < candidate_count && final_count < MAX_CLUSTERS;
         i++)
    {
        int duplicate = 0;

        for (int j = 0; j < final_count; j++)
        {
            if (same_cluster(&candidates[i], &final_clusters[j]))
            {
                duplicate = 1;
                break;
            }
        }

        if (!duplicate)
        {
            final_clusters[final_count++] = candidates[i];
        }
    }

    *candidate_count_out = candidate_count;
    return final_count;
}


/* =========================================================
   Print Cluster
   ========================================================= */

void print_cluster(
    Cluster *cluster,
    StructureRecord *records,
    int id
)
{
    printf("\n");

    printf("========================================\n");

    printf(
        "CLUSTER #%d\n",
        id
    );

    printf("========================================\n");


    printf(
        "Structures      : %d\n",
        cluster->count
    );

    printf(
        "Different types : %d\n",
        cluster->type_count
    );

    const int signature_order[] = {
        Village,
        Desert_Pyramid,
        Jungle_Pyramid,
        Shipwreck,
        Ancient_City
    };
    int signature_has_type = 0;

    printf("Signature       : ");

    for (int type_index = 0;
         type_index < sizeof(signature_order) / sizeof(signature_order[0]);
         type_index++)
    {
        const char *type_name = NULL;

        for (int i = 0; i < cluster->count; i++)
        {
            StructureRecord *record =
                &records[cluster->indices[i]];

            if (record->type == signature_order[type_index])
            {
                type_name = record->name;
                break;
            }
        }

        if (type_name == NULL)
        {
            continue;
        }

        if (signature_has_type)
        {
            printf("+");
        }

        for (const char *character = type_name;
             *character != '\0';
             character++)
        {
            if (*character != ' ')
            {
                putchar(*character);
            }
        }

        signature_has_type = 1;
    }

    if (!signature_has_type)
    {
        printf("None");
    }

    printf("\n");


    printf(
        "Center          : X=%.1f Z=%.1f\n",
        cluster->center_x,
        cluster->center_z
    );


    printf(
        "Nearest spawn   : %.1f blocks\n",
        cluster->nearest_spawn
    );


    printf(
    "Max span        : %.1f blocks\n",
        cluster->max_distance
    );

    printf(
    "Density         : %.8f structures/block²\n",
    cluster->density
    );

    if (cluster->count >= 2)
    {
        StructureRecord *a =
            &records[
                cluster->closest_a
            ];

        StructureRecord *b =
            &records[
                cluster->closest_b
            ];


        printf(
            "Closest pair    : %.1f blocks\n",
            cluster->closest_distance
        );


        printf(
            "                  %s <-> %s\n",
            a->name,
            b->name
        );
    }
    /*
 * -----------------------------------------------------
 * Structure composition
 * -----------------------------------------------------
 */

int composition[FEATURE_NUM];

memset(
    composition,
    0,
    sizeof(composition)
);

for (int i = 0;
     i < cluster->count;
     i++)
{
    StructureRecord *r =
        &records[
            cluster->indices[i]
        ];

    if (r->type >= 0 &&
        r->type < FEATURE_NUM)
    {
        composition[r->type]++;
    }
}


printf("\n");
printf("Composition:\n");

for (int i = 0;
     i < cluster->count;
     i++)
{
    StructureRecord *r =
        &records[
            cluster->indices[i]
        ];

    int already_printed = 0;

    /*
     * Check whether this type was already printed.
     */
    for (int j = 0; j < i; j++)
    {
        StructureRecord *previous =
            &records[
                cluster->indices[j]
            ];

        if (previous->type == r->type)
        {
            already_printed = 1;
            break;
        }
    }

    if (already_printed)
    {
        continue;
    }

    printf(
        "  %-20s : %d\n",
        r->name,
        composition[r->type]
    );
}

    printf("\n");

    printf("Members:\n");


    for (int i = 0;
         i < cluster->count;
         i++)
    {
        StructureRecord *r =
            &records[
                cluster->indices[i]
            ];


        printf(
            "  %-20s X=%6d Z=%6d | spawn=%7.1f\n",
            r->name,
            r->x,
            r->z,
            r->distance
        );
    }
}


/* =========================================================
   Main
   ========================================================= */

#ifndef SEEDLENS_NO_CLUSTER_DEMO
int main(void)
{
    uint64_t seed =
        123456789ULL;

    int mc =
        MC_1_20_6;


    StructureTarget targets[] =
    {
        {
            "Village",
            Village
        },

        {
            "Desert Pyramid",
            Desert_Pyramid
        },

        {
            "Jungle Temple",
            Jungle_Pyramid
        },

        {
            "Shipwreck",
            Shipwreck
        },

        {
            "Ancient City",
            Ancient_City
        }
    };


    int target_count =
        sizeof(targets) /
        sizeof(targets[0]);


    /*
     * Static allocation avoids large stack usage.
     */
    static StructureRecord
        records[MAX_STRUCTURES];

    static Cluster
        final_clusters[MAX_CLUSTERS];


    printf(
        "========================================\n"
    );

    printf(
        "       SeedLens Cluster Engine\n"
    );

    printf(
        "========================================\n\n"
    );


    printf(
        "Seed           : %llu\n",
        (unsigned long long)seed
    );


    printf(
        "Minecraft      : 1.20.6\n"
    );


    printf(
        "Search bounds  : +/- %d blocks\n",
        SEARCH_RADIUS
    );


    printf(
        "Cluster radius : %.0f blocks\n",
        CLUSTER_RADIUS
    );


    printf(
        "Max diameter   : %.0f blocks\n\n",
        MAX_CLUSTER_DIAMETER
    );


    printf(
        "Collecting viable structures...\n"
    );


    int record_count =
        collect_records(
            seed,
            mc,
            targets,
            target_count,
            records
        );


    printf(
        "Collected      : %d viable structures\n",
        record_count
    );


    int candidate_count = 0;
    int final_count = detect_clusters(
        records,
        record_count,
        final_clusters,
        &candidate_count
    );

    printf(
        "Cluster candidates: %d\n",
        candidate_count
    );


    /*
     * -----------------------------------------------------
     * Print final results.
     * -----------------------------------------------------
     */

    printf("\n");

    printf(
        "========================================\n"
    );

    printf(
        "          TOP CLUSTERS\n"
    );

    printf(
        "========================================\n"
    );


    printf(
        "Unique clusters: %d\n",
        final_count
    );


    int limit =
        final_count < 10
        ? final_count
        : 10;


    for (int i = 0;
         i < limit;
         i++)
    {
        print_cluster(
            &final_clusters[i],
            records,
            i + 1
        );
    }


    printf("\n");

    printf(
        "========================================\n"
    );

    printf(
        "SeedLens Cluster Engine finished.\n"
    );

    printf(
        "========================================\n"
    );


    return 0;
}
#endif
