#!/usr/bin/env python3
"""Benchmark LOGA vs Drain on LogHub-2.0 datasets.

Usage:
    python bench/main.py --dataset Apache
    python bench/main.py --dataset Apache,Proxifier,Linux
    python bench/main.py --dataset all
    python bench/main.py --dataset Apache --force
    python bench/main.py --list
"""
import argparse
import pathlib
import sys
import traceback

import pandas as pd

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from wrapper.datasets import ALL_DATASETS, DATASETS, BENCH_DIR, resolve_paths
from wrapper import run_loga, run_drain
from wrapper.score import score
from wrapper.summary import rebuild, CSV_PATH, RESULTS_DIR


def parse_args():
    ap = argparse.ArgumentParser(description="Benchmark LOGA vs Drain")
    ap.add_argument("--dataset", type=str, help="Dataset name, comma-separated list, or 'all'")
    ap.add_argument("--force", action="store_true", help="Re-run even if cached results exist")
    ap.add_argument("--list", action="store_true", dest="list_datasets", help="Print available datasets and exit")
    return ap.parse_args()


def dataset_already_done(name):
    """Check if results exist for this dataset."""
    loga_dir = RESULTS_DIR / "loga" / name
    drain_dir = RESULTS_DIR / "drain" / name
    if not CSV_PATH.exists():
        return False
    existing = pd.read_csv(CSV_PATH)
    has_row = name in existing["dataset"].values
    has_loga = any(loga_dir.glob("*_structured.csv"))
    has_drain = any(drain_dir.glob("*_structured.csv"))
    return has_row and has_loga and has_drain


def upsert_csv_row(row_dict):
    """Append or overwrite one dataset's row in the results CSV."""
    RESULTS_DIR.mkdir(parents=True, exist_ok=True)
    if CSV_PATH.exists():
        df = pd.read_csv(CSV_PATH)
        df = df[df["dataset"] != row_dict["dataset"]]
    else:
        df = pd.DataFrame()
    df = pd.concat([df, pd.DataFrame([row_dict])], ignore_index=True)
    df = df.sort_values("dataset").reset_index(drop=True)
    df.to_csv(CSV_PATH, index=False)


def run_one(name, force):
    """Run LOGA + Drain + score for one dataset. Returns True on success."""
    if not force and dataset_already_done(name):
        print(f"  {name}: already done (use --force to re-run)")
        return True

    log_path, gt_structured, gt_templates, log_suffix = resolve_paths(name)
    n_lines = sum(1 for _ in open(log_path))

    gt_df = pd.read_csv(gt_structured)
    n_templates_gt = gt_df["EventTemplate"].nunique()

    # LOGA
    print(f"  {name}: running LOGA...")
    loga_result = run_loga.run(name, log_path, gt_structured, log_suffix)

    # Drain
    print(f"  {name}: running Drain...")
    drain_result = run_drain.run(name, log_path, gt_structured, log_suffix)

    # Score LOGA
    print(f"  {name}: scoring LOGA...")
    loga_scores = score(name, loga_result["structured_csv"], gt_structured)

    # Score Drain
    print(f"  {name}: scoring Drain...")
    drain_scores = score(name, drain_result["structured_csv"], gt_structured)

    row = {
        "dataset": name,
        "lines": n_lines,
        "n_templates_gt": n_templates_gt,
        "n_templates_loga": loga_result["n_templates"],
        "n_templates_drain": drain_result["n_templates"],
        "GA_loga": loga_scores["GA"],
        "FGA_loga": loga_scores["FGA"],
        "PA_loga": loga_scores["PA"],
        "FTA_loga": loga_scores["FTA"],
        "GA_drain": drain_scores["GA"],
        "FGA_drain": drain_scores["FGA"],
        "PA_drain": drain_scores["PA"],
        "FTA_drain": drain_scores["FTA"],
        "runtime_loga_s": round(loga_result["runtime_s"], 2),
        "runtime_drain_s": round(drain_result["runtime_s"], 2),
    }
    upsert_csv_row(row)

    print(
        f"  {name}: GA loga={loga_scores['GA']:.3f} drain={drain_scores['GA']:.3f} | "
        f"runtime loga={loga_result['runtime_s']:.0f}s drain={drain_result['runtime_s']:.0f}s"
    )
    return True


def main():
    args = parse_args()

    if args.list_datasets:
        print("Available datasets:")
        for name in ALL_DATASETS:
            try:
                log_path, _, _, suffix = resolve_paths(name)
                size = "full" if "_full." in suffix else "2k"
                print(f"  {name:15s} ({size}, {log_path})")
            except FileNotFoundError:
                print(f"  {name:15s} (not downloaded)")
        return

    if not args.dataset:
        print("Error: --dataset required (or use --list)")
        sys.exit(1)

    if args.dataset == "all":
        targets = ALL_DATASETS
    else:
        targets = [t.strip() for t in args.dataset.split(",")]

    unknown = [t for t in targets if t not in DATASETS]
    if unknown:
        print(f"Error: unknown dataset(s): {', '.join(unknown)}")
        print(f"Available: {', '.join(ALL_DATASETS)}")
        sys.exit(1)

    failures = {}
    for name in targets:
        print(f"\n{'='*60}")
        print(f"Dataset: {name}")
        print(f"{'='*60}")
        try:
            run_one(name, args.force)
        except Exception:
            tb = traceback.format_exc()
            failures[name] = tb
            error_log = RESULTS_DIR / f"{name}.error.log"
            error_log.write_text(tb)
            print(f"  {name}: FAILED (see {error_log})")
            print(tb)

    # Rebuild summary from whatever we have
    print(f"\n{'='*60}")
    print("Rebuilding SUMMARY.md...")
    rebuild()

    if failures:
        print(f"\n{len(failures)} dataset(s) failed: {', '.join(failures.keys())}")
        sys.exit(1)

    print("\nDone.")


if __name__ == "__main__":
    main()
