"""Score parsed output against ground truth using the LogHub-2.0 evaluator."""
import sys
import pathlib

import pandas as pd

_BENCH_DIR = pathlib.Path(__file__).resolve().parent.parent
_HARNESS_LOGPARSER = _BENCH_DIR / "third_party" / "loghub-2.0" / "benchmark" / "logparser" / "utils"
_HARNESS_EVAL = _BENCH_DIR / "third_party" / "loghub-2.0" / "benchmark"

# The evaluator modules use absolute imports like "from evaluation.utils.common import ..."
# so we need the benchmark/ dir on sys.path.
for p in [str(_HARNESS_LOGPARSER), str(_HARNESS_EVAL)]:
    if p not in sys.path:
        sys.path.insert(0, p)

from evaluator import evaluate  # GA, FGA  # noqa: E402
from evaluation.utils.PA_calculator import calculate_parsing_accuracy  # PA  # noqa: E402
from evaluation.utils.template_level_analysis import evaluate_template_level  # FTA  # noqa: E402


def score(dataset_name, parsed_structured_csv, gt_structured_csv):
    """Score parsed output and return dict with GA, FGA, PA, FTA."""
    df_parsed = pd.read_csv(parsed_structured_csv)
    df_gt = pd.read_csv(gt_structured_csv)

    # Align indices — the evaluator expects shared index between gt and parsed
    df_gt = df_gt.set_index("LineId")
    df_parsed = df_parsed.set_index("LineId")

    GA, FGA = evaluate(df_gt, df_parsed)
    PA = calculate_parsing_accuracy(df_gt, df_parsed)
    _n_identified, _n_oracle, FTA, _PTA, _RTA = evaluate_template_level(
        dataset_name, df_gt, df_parsed
    )

    return {"GA": GA, "FGA": FGA, "PA": PA, "FTA": FTA}
