"""Dataset registry: name -> paths, log_format, Drain params."""
import pathlib

BENCH_DIR = pathlib.Path(__file__).resolve().parent.parent
REPO_ROOT = BENCH_DIR.parent
LOGHUB_2K = BENCH_DIR / "third_party" / "loghub-2.0" / "2k_dataset"
LOGHUB_FULL = BENCH_DIR / "data"
LOGA_BIN = REPO_ROOT / "build" / "loga"

DATASETS = {
    "Apache": {
        "log_format": r"\[<Time>\] \[<Level>\] <Content>",
        "regex": [r"(\d+\.){3}\d+"],
        "st": 0.5,
        "depth": 4,
    },
    "BGL": {
        "log_format": "<Label> <Timestamp> <Date> <Node> <Time> <NodeRepeat> <Type> <Component> <Level> <Content>",
        "regex": [r"core\.\d+"],
        "st": 0.5,
        "depth": 4,
    },
    "Hadoop": {
        "log_format": r"<Date> <Time> <Level> \[<Process>\] <Component>: <Content>",
        "regex": [r"(\d+\.){3}\d+"],
        "st": 0.5,
        "depth": 4,
    },
    "HDFS": {
        "log_format": "<Date> <Time> <Pid> <Level> <Component>: <Content>",
        "regex": [r"blk_-?\d+", r"(\d+\.){3}\d+(:\d+)?"],
        "st": 0.5,
        "depth": 4,
    },
    "HealthApp": {
        "log_format": r"<Time>\|<Component>\|<Pid>\|<Content>",
        "regex": [],
        "st": 0.2,
        "depth": 4,
    },
    "HPC": {
        "log_format": "<LogId> <Node> <Component> <State> <Time> <Flag> <Content>",
        "regex": [r"=\d+"],
        "st": 0.5,
        "depth": 4,
    },
    "Linux": {
        "log_format": r"<Month> <Date> <Time> <Level> <Component>(\[<PID>\])?: <Content>",
        "regex": [r"(\d+\.){3}\d+", r"\d{2}:\d{2}:\d{2}"],
        "st": 0.39,
        "depth": 6,
    },
    "Mac": {
        "log_format": r"<Month>  <Date> <Time> <User> <Component>\[<PID>\]( \(<Address>\))?: <Content>",
        "regex": [r"([\w-]+\.){2,}[\w-]+"],
        "st": 0.7,
        "depth": 6,
    },
    "OpenSSH": {
        "log_format": r"<Date> <Day> <Time> <Component> sshd\[<Pid>\]: <Content>",
        "regex": [r"(\d+\.){3}\d+", r"([\w-]+\.){2,}[\w-]+"],
        "st": 0.6,
        "depth": 5,
    },
    "OpenStack": {
        "log_format": r"<Logrecord> <Date> <Time> <Pid> <Level> <Component> \[<ADDR>\] <Content>",
        "regex": [r"((\d+\.){3}\d+,?)+", r"/.+?\s", r"\d+"],
        "st": 0.5,
        "depth": 5,
    },
    "Proxifier": {
        "log_format": r"\[<Time>\] <Program> - <Content>",
        "regex": [r"<\d+\ssec", r"([\w-]+\.)+[\w-]+(:\d+)?", r"\d{2}:\d{2}(:\d{2})*", r"[KGTM]B"],
        "st": 0.6,
        "depth": 3,
    },
    "Spark": {
        "log_format": "<Date> <Time> <Level> <Component>: <Content>",
        "regex": [],
        "st": 0.5,
        "depth": 4,
    },
    "Thunderbird": {
        "log_format": r"<Label> <Timestamp> <Date> <User> <Month> <Day> <Time> <Location> <Component>(\[<PID>\])?: <Content>",
        "regex": [r"(\d+\.){3}\d+"],
        "st": 0.5,
        "depth": 4,
    },
    "Zookeeper": {
        "log_format": r"<Date> <Time> - <Level>  \[<Node>:<Component>@<Id>\] - <Content>",
        "regex": [r"(/|)(\d+\.){3}\d+(:\d+)?"],
        "st": 0.5,
        "depth": 4,
    },
}

ALL_DATASETS = sorted(DATASETS.keys())


def resolve_paths(name):
    """Return (log_path, gt_structured_csv, gt_templates_csv, suffix) for a dataset.

    Prefers full Zenodo data in bench/data/; falls back to 2k from the submodule.
    """
    full_dir = LOGHUB_FULL / name
    full_log = full_dir / f"{name}_full.log"
    if full_log.exists():
        suffix = f"{name}_full.log"
        return (
            full_log,
            full_dir / f"{suffix}_structured.csv",
            full_dir / f"{suffix}_templates.csv",
            suffix,
        )

    twok_dir = LOGHUB_2K / name
    twok_log = twok_dir / f"{name}_2k.log"
    if twok_log.exists():
        suffix = f"{name}_2k.log"
        return (
            twok_log,
            twok_dir / f"{suffix}_structured.csv",
            twok_dir / f"{suffix}_templates.csv",
            suffix,
        )

    raise FileNotFoundError(
        f"No log file found for {name}. Expected {full_log} or {twok_log}"
    )
