# LOGA vs Drain — Benchmark Results

## Results Table

| dataset   |   lines |   n_templates_gt |   n_templates_loga |   n_templates_drain |   GA_loga |   FGA_loga |   PA_loga |   FTA_loga |   GA_drain |   FGA_drain |   PA_drain |   FTA_drain |   runtime_loga_s |   runtime_drain_s |
|:----------|--------:|-----------------:|-------------------:|--------------------:|----------:|-----------:|----------:|-----------:|-----------:|------------:|-----------:|------------:|-----------------:|------------------:|
| Apache    |    2000 |                6 |                  6 |                   6 |    1.0000 |     1.0000 |    0.6935 |     0.5000 |     1.0000 |      1.0000 |     0.6935 |      0.5000 |          13.9700 |            0.0900 |
| Linux     |    2000 |              118 |                 45 |                 112 |    0.0300 |     0.1104 |    0.0150 |     0.0491 |     0.6900 |      0.9304 |     0.1835 |      0.4348 |          39.4700 |            0.1200 |
| Mac       |    2000 |              341 |                177 |                 394 |    0.3940 |     0.3745 |    0.1180 |     0.0502 |     0.7865 |      0.7973 |     0.2175 |      0.1986 |          76.4700 |            0.1500 |
| Proxifier |    2000 |                8 |                 52 |                  18 |    0.0025 |     0.0667 |    0.0000 |     0.0000 |     0.5265 |      0.5385 |     0.0000 |      0.0000 |          67.4900 |            0.1400 |
| Zookeeper |    2000 |               50 |                 46 |                  46 |    0.6715 |     0.3125 |    0.5765 |     0.1458 |     0.9665 |      0.8542 |     0.4970 |      0.3542 |          14.5500 |            0.1100 |

## Per-Metric Comparison

- **GA**: Drain leads (LOGA 0.4196 vs Drain 0.7939, delta 0.3743)
- **FGA**: Drain leads (LOGA 0.3728 vs Drain 0.8241, delta 0.4512)
- **PA**: Drain leads (LOGA 0.2806 vs Drain 0.3183, delta 0.0377)
- **FTA**: Drain leads (LOGA 0.1490 vs Drain 0.2975, delta 0.1485)

## Runtime

Total across 5 datasets: LOGA 211.9s, Drain 0.6s.

## Parameter Stability

LOGA was run with default parameters across all datasets (K=1, Leiden community detection, threshold=0.9, outlier=1.5). Drain was run with per-dataset tuned parameters from LogHub-2.0 (dataset-specific depth, similarity threshold, and regex preprocessing).

## Datasets Not Yet Run

BGL, HDFS, HPC, Hadoop, HealthApp, OpenSSH, OpenStack, Spark, Thunderbird
