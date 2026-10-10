#define SEEDLENS_NO_CLUSTER_DEMO
#include "test_clusters.c"

/* Provisional analysis threshold; tune against intended gameplay criteria. */
#define LUCKY_SPAWN_RADIUS 1000.0

/* Baseline quantiles measured across the 120 clusters from the six test seeds. */
#define USEFUL_V2_MEDIAN_SPAWN 3078.8
#define USEFUL_V2_MEDIAN_DIAMETER 723.5
#define USEFUL_V2_MEDIAN_CLOSEST_PAIR 182.4
#define USEFUL_V2_MEDIAN_DENSITY 0.00001254
#define USEFUL_V2_Q75_DENSITY 0.00001520

#define TARGET_COUNT (sizeof(targets) / sizeof(targets[0]))
#define BASELINE_SEED_COUNT 6
#define HOLDOUT_SEED_COUNT 8
#define SEED_COUNT (BASELINE_SEED_COUNT + HOLDOUT_SEED_COUNT)

static StructureTarget targets[] =
{
    {"Village", Village},
    {"Desert Pyramid", Desert_Pyramid},
    {"Jungle Temple", Jungle_Pyramid},
    {"Shipwreck", Shipwreck},
    {"Ancient City", Ancient_City}
};

typedef struct {
    const char *interesting_reasons[2];
    int interesting_reason_count;
    const char *useful_v1_reasons[3];
    int useful_v1_reason_count;
    const char *useful_v2_reasons[3];
    char useful_v2_rule_ids[3];
    int useful_v2_reason_count;
    int is_spawn_outpost;
    int is_mega_hub;
    int is_trading_metropolis;
    int is_abyssal_hub;
    int is_dense_multi;
    int useful_v3_category_count;
    int is_useful_v3;
    const char *lucky_reason;
    double nearest_useful_spawn;
} ClusterAnalysis;

typedef struct {
    int64_t seed;
    int viable_count;
    int candidate_count;
    int cluster_count;
    int interesting_count;
    int useful_count;
    int useful_v2_count;
    int useful_v3_count;
    int lucky_count;
    int interesting_village_ancient_count;
    int interesting_pyramid_temple_count;
    int useful_village_shipwreck_count;
    int useful_multiple_villages_count;
    int useful_strategic_count;
    int v3_spawn_outpost_count;
    int v3_mega_hub_count;
    int v3_trading_metropolis_count;
    int v3_abyssal_hub_count;
    int v3_dense_multi_count;
    StructureRecord records[MAX_STRUCTURES];
    Cluster clusters[MAX_CLUSTERS];
} SeedRun;

typedef struct {
    int type;
    const char *name;
} SignatureType;

static const SignatureType signature_types[] = {
    {Village, "Village"},
    {Desert_Pyramid, "DesertPyramid"},
    {Jungle_Pyramid, "JungleTemple"},
    {Shipwreck, "Shipwreck"},
    {Ancient_City, "AncientCity"}
};

static int count_type(
    const Cluster *cluster,
    const StructureRecord *records,
    int type
)
{
    int count = 0;

    for (int i = 0; i < cluster->count; i++)
    {
        if (records[cluster->indices[i]].type == type)
        {
            count++;
        }
    }

    return count;
}

static double type_pair_distance(
    const Cluster *cluster,
    const StructureRecord *records,
    int type_a,
    int type_b,
    int maximum
)
{
    double result = maximum ? 0.0 : 1e30;
    int found = 0;

    for (int i = 0; i < cluster->count; i++)
    {
        const StructureRecord *a = &records[cluster->indices[i]];

        for (int j = i + 1; j < cluster->count; j++)
        {
            const StructureRecord *b = &records[cluster->indices[j]];
            int pair_matches =
                (a->type == type_a && b->type == type_b) ||
                (a->type == type_b && b->type == type_a);

            if (!pair_matches)
            {
                continue;
            }

            double distance = distance_records(a, b);
            if (!found || (maximum ? distance > result : distance < result))
            {
                result = distance;
                found = 1;
            }
        }
    }

    return found ? result : -1.0;
}

static int useful_v3_spawn_outpost(
    const Cluster *cluster,
    const StructureRecord *records
)
{
    if (cluster->nearest_spawn > 1000.0)
    {
        return 0;
    }
    if (cluster->count < 3)
    {
        return 0;
    }
    if (cluster->max_distance > 720.0)
    {
        return 0;
    }
    if (cluster->density < 0.00001000)
    {
        return 0;
    }

    int v = count_type(cluster, records, Village);
    double d_v_sw = type_pair_distance(cluster, records, Village, Shipwreck, 0);
    double d_v_ac = type_pair_distance(cluster, records, Village, Ancient_City, 0);

    return ((d_v_sw > 0.0 && d_v_sw <= 450.0) ||
            (d_v_ac > 0.0 && d_v_ac <= 450.0) ||
            (v >= 2));
}

static int useful_v3_mega_hub(
    const Cluster *cluster,
    const StructureRecord *records
)
{
    (void)records;

    if (cluster->count < 5 ||
        cluster->type_count < 3 ||
        cluster->closest_distance > 180.0)
    {
        return 0;
    }

    if (cluster->max_distance <= 780.0)
    {
        return 1;
    }

    /* v3.1: high-diversity compact exception */
    return (cluster->max_distance <= 800.0 &&
            cluster->type_count >= 3 &&
            cluster->count >= 5 &&
            cluster->closest_distance <= 150.0);
}

static int useful_v3_trading_metropolis(
    const Cluster *cluster,
    const StructureRecord *records
)
{
    if (cluster->count < 4)
    {
        return 0;
    }
    if (cluster->max_distance > 760.0)
    {
        return 0;
    }
    if (cluster->density < 0.00001200)
    {
        return 0;
    }

    int v = count_type(cluster, records, Village);
    if (v < 2)
    {
        return 0;
    }

    double d_v_v_max = type_pair_distance(cluster, records, Village, Village, 1);
    return (d_v_v_max > 0.0 && d_v_v_max <= 600.0);
}

static int useful_v3_abyssal_hub(
    const Cluster *cluster,
    const StructureRecord *records
)
{
    if (cluster->count < 4)
    {
        return 0;
    }
    if (cluster->max_distance > 780.0)
    {
        return 0;
    }

    int ac = count_type(cluster, records, Ancient_City);
    if (ac < 1)
    {
        return 0;
    }

    int v = count_type(cluster, records, Village);
    int sw = count_type(cluster, records, Shipwreck);

    double d_v_ac = type_pair_distance(cluster, records, Village, Ancient_City, 0);
    double d_sw_ac = type_pair_distance(cluster, records, Shipwreck, Ancient_City, 0);

    /*
     * v3.1 Variant B:
     * - Village + Ancient City: distance <= 450
     * - Without Village: Shipwreck + Ancient City: distance <= 250
     */
    if (v >= 1)
    {
        return (d_v_ac > 0.0 && d_v_ac <= 450.0);
    }

    return (sw >= 1 &&
            d_sw_ac > 0.0 &&
            d_sw_ac <= 250.0);
}

static int useful_v3_dense_multi(
    const Cluster *cluster,
    const StructureRecord *records
)
{
    (void)records;
    return (cluster->closest_distance <= 100.0 &&
            cluster->count >= 4 &&
            cluster->max_distance <= 700.0 &&
            cluster->density >= 0.00001400);
}

static int useful_v3(
    const Cluster *cluster,
    const StructureRecord *records
)
{
    return (useful_v3_spawn_outpost(cluster, records) ||
            useful_v3_mega_hub(cluster, records) ||
            useful_v3_trading_metropolis(cluster, records) ||
            useful_v3_abyssal_hub(cluster, records) ||
            useful_v3_dense_multi(cluster, records));
}

static ClusterAnalysis analyze_cluster(
    const Cluster *cluster,
    const StructureRecord *records
)
{
    ClusterAnalysis analysis = {0};
    analysis.nearest_useful_spawn = 1e30;

    int villages = count_type(cluster, records, Village);
    int desert_pyramids = count_type(cluster, records, Desert_Pyramid);
    int jungle_temples = count_type(cluster, records, Jungle_Pyramid);
    int shipwrecks = count_type(cluster, records, Shipwreck);
    int ancient_cities = count_type(cluster, records, Ancient_City);

    if (villages > 0 && ancient_cities > 0)
    {
        analysis.interesting_reasons[analysis.interesting_reason_count++] =
            "Village + Ancient City";
    }

    if (desert_pyramids > 0 && jungle_temples > 0)
    {
        analysis.interesting_reasons[analysis.interesting_reason_count++] =
            "Desert Pyramid + Jungle Temple";
    }

    int strategic_count = villages + shipwrecks + ancient_cities;
    int strategic_type_count =
        (villages > 0) + (shipwrecks > 0) + (ancient_cities > 0);

    for (int i = 0; i < cluster->count; i++)
    {
        const StructureRecord *record =
            &records[cluster->indices[i]];

        if ((record->type == Village ||
             record->type == Shipwreck ||
             record->type == Ancient_City) &&
            record->distance < analysis.nearest_useful_spawn)
        {
            analysis.nearest_useful_spawn = record->distance;
        }
    }

    if (villages > 0 && shipwrecks > 0)
    {
        analysis.useful_v1_reasons[analysis.useful_v1_reason_count++] =
            "Village + Shipwreck";
    }

    if (villages >= 2)
    {
        analysis.useful_v1_reasons[analysis.useful_v1_reason_count++] =
            "Multiple Villages";
    }

    if (strategic_count >= 3 && strategic_type_count >= 2)
    {
        analysis.useful_v1_reasons[analysis.useful_v1_reason_count++] =
            "3+ Villages/Shipwrecks/Ancient Cities across 2+ types";
    }

    double village_shipwreck_distance = type_pair_distance(
        cluster,
        records,
        Village,
        Shipwreck,
        0
    );
    double village_ancient_city_distance = type_pair_distance(
        cluster,
        records,
        Village,
        Ancient_City,
        0
    );
    double village_spread = type_pair_distance(
        cluster,
        records,
        Village,
        Village,
        1
    );

    if (((villages > 0 && shipwrecks > 0 &&
          village_shipwreck_distance <= CLUSTER_RADIUS) ||
         (villages > 0 && ancient_cities > 0 &&
          village_ancient_city_distance <= CLUSTER_RADIUS)) &&
        cluster->nearest_spawn <= LUCKY_SPAWN_RADIUS)
    {
        analysis.useful_v2_rule_ids[analysis.useful_v2_reason_count] = 'A';
        analysis.useful_v2_reasons[analysis.useful_v2_reason_count++] =
            "Village + Shipwreck or Village + Ancient City within 500 blocks, with the cluster within 1000 blocks of spawn";
    }

    if (villages >= 2 &&
        village_spread >= 0.0 &&
        village_spread <= CLUSTER_RADIUS &&
        cluster->max_distance <= USEFUL_V2_MEDIAN_DIAMETER &&
        cluster->density >= USEFUL_V2_MEDIAN_DENSITY &&
        cluster->nearest_spawn <= USEFUL_V2_MEDIAN_SPAWN)
    {
        analysis.useful_v2_rule_ids[analysis.useful_v2_reason_count] = 'B';
        analysis.useful_v2_reasons[analysis.useful_v2_reason_count++] =
            "Multiple Villages within 500 blocks in a median-compact, median-dense cluster within the baseline median spawn distance";
    }

    if (strategic_count >= 3 &&
        strategic_type_count >= 2 &&
        cluster->closest_distance <= USEFUL_V2_MEDIAN_CLOSEST_PAIR &&
        cluster->max_distance <= USEFUL_V2_MEDIAN_DIAMETER &&
        cluster->density >= USEFUL_V2_Q75_DENSITY &&
        cluster->nearest_spawn <= USEFUL_V2_MEDIAN_SPAWN)
    {
        analysis.useful_v2_rule_ids[analysis.useful_v2_reason_count] = 'C';
        analysis.useful_v2_reasons[analysis.useful_v2_reason_count++] =
            "3+ strategic structures across 2+ types, with a median-close pair, median-or-smaller diameter, upper-quartile density, and median-or-closer spawn distance";
    }

    if (analysis.useful_v1_reason_count > 0 &&
        analysis.nearest_useful_spawn <= LUCKY_SPAWN_RADIUS)
    {
        analysis.lucky_reason =
            "Useful and a Village/Shipwreck/Ancient City within 1000 blocks of spawn";
    }

    analysis.is_spawn_outpost = useful_v3_spawn_outpost(cluster, records);
    analysis.is_mega_hub = useful_v3_mega_hub(cluster, records);
    analysis.is_trading_metropolis = useful_v3_trading_metropolis(cluster, records);
    analysis.is_abyssal_hub = useful_v3_abyssal_hub(cluster, records);
    analysis.is_dense_multi = useful_v3_dense_multi(cluster, records);

    analysis.useful_v3_category_count =
        analysis.is_spawn_outpost +
        analysis.is_mega_hub +
        analysis.is_trading_metropolis +
        analysis.is_abyssal_hub +
        analysis.is_dense_multi;

    analysis.is_useful_v3 = useful_v3(cluster, records);

    return analysis;
}

static const struct {
    int type;
    const char *singular;
    const char *plural;
} inventory_types[] = {
    {Village, "Village", "Villages"},
    {Desert_Pyramid, "Desert Pyramid", "Desert Pyramids"},
    {Jungle_Pyramid, "Jungle Temple", "Jungle Temples"},
    {Shipwreck, "Shipwreck", "Shipwrecks"},
    {Ancient_City, "Ancient City", "Ancient Cities"}
};

static void print_structure_inventory(
    const Cluster *cluster,
    const StructureRecord *records
)
{
    printf("Structures:\n");
    for (size_t i = 0; i < sizeof(inventory_types) / sizeof(inventory_types[0]); i++)
    {
        int c = count_type(cluster, records, inventory_types[i].type);
        if (c > 0)
        {
            printf("- %d %s\n", c, c == 1 ? inventory_types[i].singular : inventory_types[i].plural);
        }
    }
}

static const char *primary_category(
    const ClusterAnalysis *analysis
)
{
    if (analysis->is_mega_hub)
    {
        return "Strategic Mega-Hub";
    }
    if (analysis->is_trading_metropolis)
    {
        return "Trading Metropolis";
    }
    if (analysis->is_abyssal_hub)
    {
        return "Abyssal Hub";
    }
    if (analysis->is_spawn_outpost)
    {
        return "Spawn Outpost";
    }
    if (analysis->is_dense_multi)
    {
        return "Dense Multi";
    }
    return "None";
}

static void print_explanation(
    const Cluster *cluster,
    const StructureRecord *records,
    const ClusterAnalysis *analysis
)
{
    const char *primary = primary_category(analysis);

    printf("\n------------------------------------------------------------\n");
    printf("Why is this cluster special?\n");
    printf("------------------------------------------------------------\n\n");

    printf("Primary: %s\n\n", primary);

    print_structure_inventory(cluster, records);

    printf("\nCluster:\n");
    printf("- %d structures\n", cluster->count);
    printf("- %d structure types\n", cluster->type_count);
    printf("- Diameter: %.1f blocks\n", cluster->max_distance);
    printf("- Closest pair: %.1f blocks\n", cluster->closest_distance);

    if (analysis->useful_v3_category_count > 1)
    {
        printf("\nAdditional advantages:\n");
        if (analysis->is_mega_hub && strcmp(primary, "Strategic Mega-Hub") != 0)
        {
            printf("- Strategic Mega-Hub\n");
        }
        if (analysis->is_trading_metropolis && strcmp(primary, "Trading Metropolis") != 0)
        {
            printf("- Trading Metropolis\n");
        }
        if (analysis->is_abyssal_hub && strcmp(primary, "Abyssal Hub") != 0)
        {
            printf("- Abyssal Hub\n");
        }
        if (analysis->is_spawn_outpost && strcmp(primary, "Spawn Outpost") != 0)
        {
            printf("- Spawn Outpost\n");
        }
        if (analysis->is_dense_multi && strcmp(primary, "Dense Multi") != 0)
        {
            printf("- Dense Multi\n");
        }
    }

    printf("\nWhy it matters:\n");
    if (strcmp(primary, "Strategic Mega-Hub") == 0)
    {
        printf("This cluster contains %d structures across %d structure types with a closest\n"
               "pair of %.1f blocks and a diameter of %.1f blocks. The combination of high structure\n"
               "diversity and compact grouping makes it useful for exploration and progression.\n",
               cluster->count, cluster->type_count, cluster->closest_distance, cluster->max_distance);
    }
    else if (strcmp(primary, "Trading Metropolis") == 0)
    {
        int villages = count_type(cluster, records, Village);
        double v_spread = type_pair_distance(cluster, records, Village, Village, 1);
        printf("This cluster contains %d Villages with a maximum spread of %.1f blocks within\n"
               "a %.1f block cluster. Multiple villages within a relatively compact area can support\n"
               "trading, villager management, and settlement expansion.\n",
               villages, v_spread, cluster->max_distance);
    }
    else if (strcmp(primary, "Abyssal Hub") == 0)
    {
        int villages = count_type(cluster, records, Village);
        double d_v_ac = type_pair_distance(cluster, records, Village, Ancient_City, 0);
        double d_sw_ac = type_pair_distance(cluster, records, Shipwreck, Ancient_City, 0);

        if (villages >= 1 && d_v_ac > 0.0 && d_v_ac <= 450.0)
        {
            printf("An Ancient City is located within %.1f blocks of a Village in a %.1f block cluster,\n"
                   "creating a compact surface-to-underground exploration opportunity.\n",
                   d_v_ac, cluster->max_distance);
        }
        else
        {
            printf("An Ancient City is located within %.1f blocks of a Shipwreck in a %.1f block cluster,\n"
                   "combining coastal exploration with access to a nearby deep underground location.\n",
                   d_sw_ac, cluster->max_distance);
        }
    }
    else if (strcmp(primary, "Spawn Outpost") == 0)
    {
        printf("Located %.1f blocks from world spawn with %d structures within a %.1f block cluster.\n"
               "Being close to world spawn makes this cluster useful for early-game exploration,\n"
               "establishing a base, and accessing nearby structures without extensive travel.\n",
               cluster->nearest_spawn, cluster->count, cluster->max_distance);
    }
    else if (strcmp(primary, "Dense Multi") == 0)
    {
        printf("This compact cluster contains %d structures with a closest pair of\n"
               "%.1f blocks, a diameter of %.1f blocks, and a density of %.8f.\n"
               "The close spatial arrangement provides several nearby points of interest.\n",
               cluster->count, cluster->closest_distance, cluster->max_distance, cluster->density);
    }
    else
    {
        printf("This cluster meets baseline useful criteria with %d structures across %.1f blocks.\n",
               cluster->count, cluster->max_distance);
    }
}


static void make_signature(
    char *signature,
    size_t signature_size,
    const Cluster *cluster,
    const StructureRecord *records
)
{
    size_t used = 0;
    signature[0] = '\0';

    for (size_t type_index = 0;
         type_index < sizeof(signature_types) / sizeof(signature_types[0]);
         type_index++)
    {
        if (count_type(cluster, records, signature_types[type_index].type) == 0)
        {
            continue;
        }

        int written = snprintf(
            signature + used,
            signature_size - used,
            "%s%s",
            used > 0 ? "+" : "",
            signature_types[type_index].name
        );

        if (written < 0 || (size_t)written >= signature_size - used)
        {
            signature[signature_size - 1] = '\0';
            return;
        }

        used += (size_t)written;
    }
}

static void print_reasons(
    const char *label,
    const char *const *reasons,
    int reason_count
)
{
    printf("%s: %s", label, reason_count > 0 ? "YES" : "NO");

    if (reason_count > 0)
    {
        printf(" (");
        for (int i = 0; i < reason_count; i++)
        {
            printf("%s%s", i > 0 ? "; " : "", reasons[i]);
        }
        printf(")");
    }

    printf("\n");
}

static int has_reason(
    const char *const *reasons,
    int reason_count,
    const char *expected
)
{
    for (int i = 0; i < reason_count; i++)
    {
        if (strcmp(reasons[i], expected) == 0)
        {
            return 1;
        }
    }

    return 0;
}

int main(void)
{
    const int mc = MC_1_20_6;
    const int64_t seeds[SEED_COUNT] = {
        /* Baseline (6) */
        123456789LL,
        1LL,
        42LL,
        2026LL,
        987654321LL,
        -123456789LL,
        /* Holdout (8) */
        -1467692750553100038LL,
        739208174635920481LL,
        -582913746201847350LL,
        1844674407370955161LL,
        -901274638520193847LL,
        314159265358979323LL,
        -777777777777777777LL,
        202620262026202626LL
    };
    static SeedRun runs[SEED_COUNT];

    printf("============================================================\n");
    printf("         SeedLens Useful v3 Analysis & Verification\n");
    printf("============================================================\n");
    printf("Minecraft: 1.20.6 (MC_1_20_6)\n");
    printf("Search bounds: +/- %d blocks | Max diameter: %.0f blocks\n\n",
        SEARCH_RADIUS, MAX_CLUSTER_DIAMETER);

    printf("Useful v3 Functional Categories (OR):\n");
    printf("  A: Spawn Outpost: spawn <= 1000, count >= 3, diameter <= 720, density >= 0.00001000,\n"
           "     and (d_v_sw <= 450 || d_v_ac <= 450 || v >= 2)\n");
    printf("  B: Strategic Mega-Hub: count >= 5, types >= 3, closest pair <= 180, diameter <= 780\n");
    printf("  C: Trading Metropolis: v >= 2, count >= 4, d_v_v_max <= 600, diameter <= 760, density >= 0.00001200\n");
    printf("  D: Abyssal Hub: ac >= 1, (v >= 1 || sw >= 1), count >= 4, diameter <= 780,\n"
           "     and (d_v_ac <= 450 || d_sw_ac <= 450)\n");
    printf("  E: Dense Multi: closest pair <= 100, count >= 4, diameter <= 700, density >= 0.00001400\n\n");

    for (int seed_index = 0; seed_index < SEED_COUNT; seed_index++)
    {
        SeedRun *run = &runs[seed_index];
        run->seed = seeds[seed_index];
        run->viable_count = collect_records(
            (uint64_t)run->seed,
            mc,
            targets,
            TARGET_COUNT,
            run->records
        );
        run->cluster_count = detect_clusters(
            run->records,
            run->viable_count,
            run->clusters,
            &run->candidate_count
        );

        for (int cluster_index = 0;
             cluster_index < run->cluster_count;
             cluster_index++)
        {
            ClusterAnalysis analysis = analyze_cluster(
                &run->clusters[cluster_index],
                run->records
            );

            run->interesting_count += analysis.interesting_reason_count > 0;
            run->useful_count += analysis.useful_v1_reason_count > 0;
            run->useful_v2_count += analysis.useful_v2_reason_count > 0;
            run->useful_v3_count += analysis.is_useful_v3;
            run->lucky_count += analysis.lucky_reason != NULL;

            run->v3_spawn_outpost_count += analysis.is_spawn_outpost;
            run->v3_mega_hub_count += analysis.is_mega_hub;
            run->v3_trading_metropolis_count += analysis.is_trading_metropolis;
            run->v3_abyssal_hub_count += analysis.is_abyssal_hub;
            run->v3_dense_multi_count += analysis.is_dense_multi;

            run->interesting_village_ancient_count += has_reason(
                analysis.interesting_reasons,
                analysis.interesting_reason_count,
                "Village + Ancient City");
            run->interesting_pyramid_temple_count += has_reason(
                analysis.interesting_reasons,
                analysis.interesting_reason_count,
                "Desert Pyramid + Jungle Temple");
            run->useful_village_shipwreck_count += has_reason(
                analysis.useful_v1_reasons,
                analysis.useful_v1_reason_count,
                "Village + Shipwreck");
            run->useful_multiple_villages_count += has_reason(
                analysis.useful_v1_reasons,
                analysis.useful_v1_reason_count,
                "Multiple Villages");
            run->useful_strategic_count += has_reason(
                analysis.useful_v1_reasons,
                analysis.useful_v1_reason_count,
                "3+ Villages/Shipwrecks/Ancient Cities across 2+ types");
        }
    }

    /* Print Per-Seed Summary Table */
    printf("Seed | Group | Clusters | Interesting | Useful v1 | Useful v2 | Useful v3 | Lucky\n");
    int total_clusters = 0;
    int total_interesting = 0;
    int total_useful_v1 = 0;
    int total_useful_v2 = 0;
    int total_useful_v3 = 0;
    int total_lucky = 0;

    int baseline_clusters = 0;
    int baseline_useful_v3 = 0;
    int holdout_clusters = 0;
    int holdout_useful_v3 = 0;

    int total_v3_spawn_outpost = 0;
    int total_v3_mega_hub = 0;
    int total_v3_trading_metropolis = 0;
    int total_v3_abyssal_hub = 0;
    int total_v3_dense_multi = 0;

    for (int seed_index = 0; seed_index < SEED_COUNT; seed_index++)
    {
        const SeedRun *run = &runs[seed_index];
        int is_baseline = (seed_index < BASELINE_SEED_COUNT);
        printf("%lld | %s | %d | %d | %d | %d | %d | %d\n",
            (long long)run->seed,
            is_baseline ? "Baseline" : "Holdout",
            run->cluster_count,
            run->interesting_count,
            run->useful_count,
            run->useful_v2_count,
            run->useful_v3_count,
            run->lucky_count);

        total_clusters += run->cluster_count;
        total_interesting += run->interesting_count;
        total_useful_v1 += run->useful_count;
        total_useful_v2 += run->useful_v2_count;
        total_useful_v3 += run->useful_v3_count;
        total_lucky += run->lucky_count;

        if (is_baseline)
        {
            baseline_clusters += run->cluster_count;
            baseline_useful_v3 += run->useful_v3_count;
        }
        else
        {
            holdout_clusters += run->cluster_count;
            holdout_useful_v3 += run->useful_v3_count;
        }

        total_v3_spawn_outpost += run->v3_spawn_outpost_count;
        total_v3_mega_hub += run->v3_mega_hub_count;
        total_v3_trading_metropolis += run->v3_trading_metropolis_count;
        total_v3_abyssal_hub += run->v3_abyssal_hub_count;
        total_v3_dense_multi += run->v3_dense_multi_count;
    }

    printf("\nUseful v3 category counts:\n\n");
    printf("Spawn Outpost: %d\n", total_v3_spawn_outpost);
    printf("Strategic Mega-Hub: %d\n", total_v3_mega_hub);
    printf("Trading Metropolis: %d\n", total_v3_trading_metropolis);
    printf("Abyssal Hub: %d\n", total_v3_abyssal_hub);
    printf("Dense Multi: %d\n", total_v3_dense_multi);

    printf("\nUseful v3 Totals:\n");
    printf("Baseline: %d / %d (%.1f%%)\n",
        baseline_useful_v3, baseline_clusters, (double)baseline_useful_v3 / baseline_clusters * 100.0);
    printf("Holdout: %d / %d (%.1f%%)\n",
        holdout_useful_v3, holdout_clusters, (double)holdout_useful_v3 / holdout_clusters * 100.0);
    printf("Total: %d / %d (%.1f%%)\n",
        total_useful_v3, total_clusters, (double)total_useful_v3 / total_clusters * 100.0);

    /* Print all Useful v3 Clusters Details */
    printf("\n============================================================\n");
    printf("                  USEFUL V3 CLUSTERS DETAILS\n");
    printf("============================================================\n");
    for (int seed_index = 0; seed_index < SEED_COUNT; seed_index++)
    {
        const SeedRun *run = &runs[seed_index];
        for (int cluster_index = 0;
             cluster_index < run->cluster_count;
             cluster_index++)
        {
            const Cluster *cluster = &run->clusters[cluster_index];
            ClusterAnalysis analysis = analyze_cluster(cluster, run->records);
            if (!analysis.is_useful_v3)
            {
                continue;
            }

            char signature[128];
            make_signature(signature, sizeof(signature), cluster, run->records);

            printf("\nSeed: %lld\n", (long long)run->seed);
            printf("Cluster #: %d\n", cluster_index + 1);
            printf("Signature: %s\n", signature);
            printf("Count: %d\n", cluster->count);
            printf("Type Count: %d\n", cluster->type_count);
            printf("Nearest Spawn: %.1f\n", cluster->nearest_spawn);
            printf("Diameter: %.1f\n", cluster->max_distance);
            printf("Closest Pair: %.1f\n", cluster->closest_distance);
            printf("Useful v3 Categories:\n");
            if (analysis.is_spawn_outpost) printf("- Spawn Outpost\n");
            if (analysis.is_mega_hub) printf("- Strategic Mega-Hub\n");
            if (analysis.is_trading_metropolis) printf("- Trading Metropolis\n");
            if (analysis.is_abyssal_hub) printf("- Abyssal Hub\n");
            if (analysis.is_dense_multi) printf("- Dense Multi\n");

            if (analysis.is_useful_v3)
            {
                print_explanation(cluster, run->records, &analysis);
            }
        }
    }

    /* Print Mandatory Case Checks */
    printf("\n============================================================\n");
    printf("               MANDATORY CASE VALIDATION\n");
    printf("============================================================\n");
    int64_t case_seeds[4] = {
        739208174635920481LL,
        202620262026202626LL,
        -1467692750553100038LL,
        1844674407370955161LL
    };
    int case_cids[4] = {8, 13, 1, 1};
    const char *case_names[4] = {"Case A", "Case B", "Case C", "Case D"};

    for (int k = 0; k < 4; k++)
    {
        for (int seed_index = 0; seed_index < SEED_COUNT; seed_index++)
        {
            const SeedRun *run = &runs[seed_index];
            if (run->seed != case_seeds[k])
            {
                continue;
            }

            int c_idx = case_cids[k] - 1;
            if (c_idx >= 0 && c_idx < run->cluster_count)
            {
                const Cluster *cluster = &run->clusters[c_idx];
                ClusterAnalysis analysis = analyze_cluster(cluster, run->records);
                char signature[128];
                make_signature(signature, sizeof(signature), cluster, run->records);

                printf("\n[%s] Seed: %lld, Cluster #%d\n", case_names[k], (long long)run->seed, case_cids[k]);
                printf("  Signature: %s | Count: %d | Types: %d\n",
                    signature, cluster->count, cluster->type_count);
                printf("  Spawn: %.1f | Diameter: %.1f | Closest: %.1f | Density: %.8f\n",
                    cluster->nearest_spawn, cluster->max_distance, cluster->closest_distance, cluster->density);
                printf("  Useful v1: %s | Useful v2: %s | Useful v3: %s\n",
                    analysis.useful_v1_reason_count > 0 ? "YES" : "NO",
                    analysis.useful_v2_reason_count > 0 ? "YES" : "NO",
                    analysis.is_useful_v3 ? "YES" : "NO");
                printf("  Useful v3 Categories:\n");
                if (analysis.is_spawn_outpost) printf("    - Spawn Outpost\n");
                if (analysis.is_mega_hub) printf("    - Strategic Mega-Hub\n");
                if (analysis.is_trading_metropolis) printf("    - Trading Metropolis\n");
                if (analysis.is_abyssal_hub) printf("    - Abyssal Hub\n");
                if (analysis.is_dense_multi) printf("    - Dense Multi\n");
                if (!analysis.is_useful_v3) printf("    (None)\n");
            }
        }
    }

    return 0;
}
