"""Rebuild SUMMARY.md from loga_vs_drain.csv."""
import pathlib

import pandas as pd

from .datasets import ALL_DATASETS, BENCH_DIR

RESULTS_DIR = BENCH_DIR / "results"
CSV_PATH = RESULTS_DIR / "loga_vs_drain.csv"
SUMMARY_PATH = RESULTS_DIR / "SUMMARY.md"

NUMERIC_COLS = [
    "GA_loga", "FGA_loga", "PA_loga", "FTA_loga",
    "GA_drain", "FGA_drain", "PA_drain", "FTA_drain",
    "runtime_loga_s", "runtime_drain_s",
]


def rebuild():
    """Regenerate SUMMARY.md from whatever rows exist in the CSV."""
    if not CSV_PATH.exists():
        SUMMARY_PATH.write_text("# LOGA vs Drain — No results yet\n")
        return

    df = pd.read_csv(CSV_PATH)
    if df.empty:
        SUMMARY_PATH.write_text("# LOGA vs Drain — No results yet\n")
        return

    # Drop any stale Average row
    df = df[df["dataset"] != "Average"]

    present = set(df["dataset"].tolist())
    missing = sorted(set(ALL_DATASETS) - present)

    # Append Average row only when all datasets are present
    if not missing:
        avg = df[NUMERIC_COLS].mean()
        avg_row = {"dataset": "Average", "lines": "", "n_templates_gt": "", "n_templates_loga": "", "n_templates_drain": ""}
        avg_row.update(avg.to_dict())
        df = pd.concat([df, pd.DataFrame([avg_row])], ignore_index=True)

    lines = ["# LOGA vs Drain — Benchmark Results\n"]

    # Table
    lines.append("## Results Table\n")
    lines.append(df.to_markdown(index=False, floatfmt=".4f"))
    lines.append("")

    # Per-metric comparison
    lines.append("## Per-Metric Comparison\n")
    data_rows = df[df["dataset"] != "Average"]
    for metric in ["GA", "FGA", "PA", "FTA"]:
        loga_col = f"{metric}_loga"
        drain_col = f"{metric}_drain"
        loga_mean = data_rows[loga_col].mean()
        drain_mean = data_rows[drain_col].mean()
        winner = "LOGA" if loga_mean > drain_mean else "Drain" if drain_mean > loga_mean else "Tie"
        diff = abs(loga_mean - drain_mean)
        lines.append(
            f"- **{metric}**: {winner} leads (LOGA {loga_mean:.4f} vs Drain {drain_mean:.4f}, delta {diff:.4f})"
        )
    lines.append("")

    # Runtime
    lines.append("## Runtime\n")
    loga_total = data_rows["runtime_loga_s"].sum()
    drain_total = data_rows["runtime_drain_s"].sum()
    lines.append(
        f"Total across {len(data_rows)} datasets: LOGA {loga_total:.1f}s, Drain {drain_total:.1f}s."
    )
    lines.append("")

    # Parameter-stability note
    lines.append("## Parameter Stability\n")
    lines.append(
        "LOGA was run with default parameters across all datasets "
        "(K=1, Leiden community detection, threshold=0.9, outlier=1.5). "
        "Drain was run with per-dataset tuned parameters from LogHub-2.0 "
        "(dataset-specific depth, similarity threshold, and regex preprocessing)."
    )
    lines.append("")

    # Missing datasets
    if missing:
        lines.append("## Datasets Not Yet Run\n")
        lines.append(", ".join(missing))
        lines.append("")

    SUMMARY_PATH.write_text("\n".join(lines))
    print(f"  Wrote {SUMMARY_PATH}")
