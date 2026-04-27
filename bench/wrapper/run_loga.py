"""Run LOGA on one dataset and emit harness-format parsed-output CSVs."""
import pathlib
import subprocess
import time

import pandas as pd

from .parse_loga_output import parse, SINGLETON_OFFSET
from .datasets import LOGA_BIN, BENCH_DIR


def run(name, log_path, gt_structured_csv, log_suffix):
    """Run LOGA and return dict with runtime_s, n_templates, structured_csv, templates_csv."""
    out_dir = BENCH_DIR / "results" / "loga" / name
    out_dir.mkdir(parents=True, exist_ok=True)
    work = out_dir / "loga_work"
    work.mkdir(exist_ok=True)

    # Extract Content column from ground-truth for LOGA to parse.
    # All benchmark parsers operate on Content, not the raw log line.
    gt = pd.read_csv(gt_structured_csv)
    content_file = work / f"{name}_content.log"
    content_file.write_text(
        "\n".join(gt["Content"].astype(str).tolist()) + "\n"
    )

    content_name = content_file.name
    cache_dir = work / f"{content_name}_d"

    # Clean previous LOGA cache so phases run fresh
    if cache_dir.exists():
        import shutil
        shutil.rmtree(cache_dir)

    loga_stdout = work / "loga.stdout"

    loga_bin = str(LOGA_BIN)
    content_path = str(content_file)

    t0 = time.time()
    # Single invocation on fresh cache runs both phases (clustering + merging)
    with open(loga_stdout, "w") as f:
        subprocess.check_call(
            [loga_bin, content_path], cwd=str(work), stdout=f, stderr=subprocess.STDOUT
        )
    runtime = time.time() - t0

    components_txt = cache_dir / f"{content_name}.components.txt"
    labels_txt = cache_dir / f"{content_name}.labels.txt"

    components = [int(x) for x in components_txt.read_text().split()]
    labels = [int(x) for x in labels_txt.read_text().split()]

    templates, singleton_clusters = parse(loga_stdout)

    rows = []
    n_unmatched = 0
    for i, comp in enumerate(components):
        if comp in templates:
            tmpl = templates[comp]
            eid = comp
        elif labels[i] in singleton_clusters:
            tmpl = templates[SINGLETON_OFFSET + labels[i]]
            eid = SINGLETON_OFFSET + labels[i]
        else:
            tmpl = "<UNMATCHED>"
            eid = -1
            n_unmatched += 1
        rows.append((i + 1, eid, tmpl))

    df = pd.DataFrame(rows, columns=["LineId", "EventId", "EventTemplate"])
    df = df.merge(gt[["LineId", "Content"]], on="LineId", how="left")
    df = df[["LineId", "Content", "EventId", "EventTemplate"]]

    structured_csv = out_dir / f"{log_suffix}_structured.csv"
    df.to_csv(structured_csv, index=False)

    counts = (
        df.groupby(["EventId", "EventTemplate"]).size().reset_index(name="Occurrences")
    )
    templates_csv = out_dir / f"{log_suffix}_templates.csv"
    counts.to_csv(templates_csv, index=False)

    unmatched_frac = n_unmatched / len(df) if len(df) > 0 else 0
    if unmatched_frac > 0.05:
        raise RuntimeError(
            f"LOGA: {n_unmatched}/{len(df)} lines ({unmatched_frac:.1%}) unmatched "
            f"in {name} — exceeds 5% threshold"
        )
    if n_unmatched > 0:
        print(f"  WARNING: {n_unmatched}/{len(df)} lines unmatched in {name}")

    return {
        "runtime_s": runtime,
        "n_templates": df["EventId"].nunique(),
        "structured_csv": structured_csv,
        "templates_csv": templates_csv,
    }
