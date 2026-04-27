"""Run Drain on one dataset and emit harness-format parsed-output CSVs."""
import pathlib
import sys
import time

import pandas as pd

from .datasets import BENCH_DIR, DATASETS

# Add loghub-2.0's Drain to the import path
_DRAIN_DIR = BENCH_DIR / "third_party" / "loghub-2.0" / "benchmark" / "logparser" / "Drain"
if str(_DRAIN_DIR) not in sys.path:
    sys.path.insert(0, str(_DRAIN_DIR))

from Drain import LogParser  # noqa: E402


def run(name, log_path, gt_structured_csv, log_suffix):
    """Run Drain and return dict with runtime_s, n_templates, structured_csv, templates_csv."""
    out_dir = BENCH_DIR / "results" / "drain" / name
    out_dir.mkdir(parents=True, exist_ok=True)

    ds = DATASETS[name]

    parser = LogParser(
        log_format=ds["log_format"],
        indir=str(log_path.parent),
        outdir=str(out_dir),
        depth=ds["depth"],
        st=ds["st"],
        rex=ds["regex"],
        keep_para=False,
    )

    t0 = time.time()
    parser.parse(log_path.name)
    runtime = time.time() - t0

    structured_csv = out_dir / f"{log_suffix}_structured.csv"
    templates_csv = out_dir / f"{log_suffix}_templates.csv"

    df = pd.read_csv(structured_csv)
    n_templates = df["EventId"].nunique()

    return {
        "runtime_s": runtime,
        "n_templates": n_templates,
        "structured_csv": structured_csv,
        "templates_csv": templates_csv,
    }
