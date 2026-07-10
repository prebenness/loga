#!/usr/bin/env python3
"""Run the nine preliminary Loga experiments on prepared NMEA traces."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import platform
import shlex
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Sequence


LARGE_SHIP = "Arch1-Normal-UDP-Trial1-LargeShip.txt"
SMALL_SHIP = "Arch1-Normal-UDP-Trial1-SmallShip.txt"
MODIFY_DTM = "ModifyingDTM-1.txt"
MODIFY_ROT = "ModifyingROT-1.txt"
DROP_ROT_MODIFY_RPM = "Normal+DropROT+ModifyRPM.txt"
SPOOF_POSITION = "SpoofPosition.txt"

R08_SHUFFLE_SEED = 20260710
R08_SHUFFLE_ALGORITHM = "sha256-sort-v1"

LOGA_OPTIONS = (
    "--cluster-algo",
    "leiden",
    "--refine-algo",
    "components",
    "--threshold",
    "0.9",
    "--outlier",
    "1.5",
)


@dataclass(frozen=True)
class RunSpec:
    run_id: str
    source_group: str
    source_name: str
    purpose: str
    transformation: str = "copy"
    local_name: str | None = None


RUNS = (
    RunSpec("R01", "standard", LARGE_SHIP, "main normal trace"),
    RunSpec("R02", "standard", SMALL_SHIP, "second normal trace"),
    RunSpec("R03", "standard", MODIFY_DTM, "template catalogue"),
    RunSpec("R04", "standard", MODIFY_ROT, "template catalogue"),
    RunSpec("R05", "standard", DROP_ROT_MODIFY_RPM, "template catalogue"),
    RunSpec("R06", "standard", SPOOF_POSITION, "template catalogue"),
    RunSpec("R07", "standard", LARGE_SHIP, "exact repeat of R01"),
    RunSpec(
        "R08",
        "standard",
        LARGE_SHIP,
        "fixed reordering of R01",
        transformation="shuffle",
        local_name="Arch1-Normal-UDP-Trial1-LargeShip.shuffled.txt",
    ),
    RunSpec(
        "R09",
        "prefix-retained",
        SPOOF_POSITION,
        "prefix sensitivity",
    ),
)


class ExperimentError(RuntimeError):
    """A failure that should stop the experiment cleanly."""


def utc_now() -> str:
    return (
        datetime.now(timezone.utc)
        .isoformat(timespec="milliseconds")
        .replace("+00:00", "Z")
    )


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def relative_path(path: Path, root: Path) -> str:
    return path.resolve().relative_to(root.resolve()).as_posix()


def file_record(path: Path, *, root: Path | None = None) -> dict[str, Any]:
    resolved = path.resolve(strict=True)
    recorded_path = relative_path(resolved, root) if root is not None else str(resolved)
    return {
        "path": recorded_path,
        "size_bytes": resolved.stat().st_size,
        "sha256": sha256_file(resolved),
    }


def write_json(path: Path, value: Any) -> None:
    temporary = path.with_name(path.name + ".tmp")
    with temporary.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write(json.dumps(value, indent=2, sort_keys=True) + "\n")
    os.replace(temporary, path)


def display_command(command: Sequence[str]) -> str:
    if os.name == "nt":
        return subprocess.list2cmdline(list(command))
    return shlex.join(command)


def validated_records(content: bytes, *, standard: bool) -> list[bytes]:
    if not content:
        raise ExperimentError("prepared input is empty")
    if b"\r" in content:
        raise ExperimentError("prepared input contains a carriage return")
    if not content.endswith(b"\n"):
        raise ExperimentError("prepared input is not LF-terminated")

    records = content.splitlines(keepends=True)
    for line_number, record in enumerate(records, start=1):
        message = record[:-1]
        if not message:
            raise ExperimentError(
                f"prepared input contains an empty record at line {line_number}"
            )
        if message.count(b"$") != 1:
            raise ExperimentError(
                f"prepared input line {line_number} does not contain exactly one '$'"
            )
        if standard and not message.startswith(b"$"):
            raise ExperimentError(
                f"standard prepared input line {line_number} has a retained prefix"
            )
    return records


def permutation_indices(count: int, seed: int = R08_SHUFFLE_SEED) -> list[int]:
    """Return a stable pseudo-random permutation of zero-based record indices."""

    def key(index: int) -> tuple[bytes, int]:
        material = f"{R08_SHUFFLE_ALGORITHM}\0{seed}\0{index + 1}".encode("ascii")
        return hashlib.sha256(material).digest(), index

    return sorted(range(count), key=key)


def write_shuffle_input(
    records: Sequence[bytes], destination: Path, mapping_path: Path
) -> dict[str, Any]:
    permutation = permutation_indices(len(records))
    destination.write_bytes(b"".join(records[index] for index in permutation))

    with mapping_path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(
            (
                "input_row_0based",
                "source_row_0based",
                "shuffled_line_1based",
                "original_line_1based",
                "record_sha256",
            )
        )
        for input_row, source_row in enumerate(permutation):
            record = records[source_row][:-1]
            writer.writerow(
                (
                    input_row,
                    source_row,
                    input_row + 1,
                    source_row + 1,
                    hashlib.sha256(record).hexdigest(),
                )
            )

    return {
        "algorithm": R08_SHUFFLE_ALGORITHM,
        "seed": R08_SHUFFLE_SEED,
        "mapping": mapping_path,
        "permutation": permutation,
    }


def reserve_output_directory(path: Path) -> Path:
    candidate = path.expanduser().resolve()
    if os.path.lexists(str(candidate)):
        raise ExperimentError(f"output directory already exists: {candidate}")
    candidate.parent.mkdir(parents=True, exist_ok=True)
    try:
        candidate.mkdir()
    except FileExistsError as error:
        raise ExperimentError(f"output directory already exists: {candidate}") from error
    return candidate


def is_within(path: Path, parent: Path) -> bool:
    try:
        path.resolve().relative_to(parent.resolve())
    except ValueError:
        return False
    return True


def build_loga_command(
    loga_command_prefix: Sequence[str], input_path: Path
) -> list[str]:
    return [
        *loga_command_prefix,
        *LOGA_OPTIONS,
        "--input",
        str(input_path.resolve(strict=True)),
    ]


def artifact_inventory(directory: Path, experiment_root: Path) -> list[dict[str, Any]]:
    if not directory.is_dir():
        return []
    return [
        file_record(path, root=experiment_root)
        for path in sorted(directory.rglob("*"))
        if path.is_file() and not path.is_symlink()
    ]


def run_pass(
    *,
    number: int,
    command: Sequence[str],
    working_directory: Path,
    pass_directory: Path,
    loga_output_directory: Path,
    experiment_root: Path,
) -> dict[str, Any]:
    pass_directory.mkdir(parents=True, exist_ok=False)
    stdout_path = pass_directory / "stdout.log"
    stderr_path = pass_directory / "stderr.log"
    command_json_path = pass_directory / "command.json"
    command_text_path = pass_directory / "command.txt"
    metadata_path = pass_directory / "metadata.json"

    command_record = {
        "argv": list(command),
        "cwd": str(working_directory.resolve()),
        "display": display_command(command),
    }
    write_json(command_json_path, command_record)
    with command_text_path.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write(command_record["display"] + "\n")

    started_at = utc_now()
    started = time.perf_counter_ns()
    try:
        with stdout_path.open("wb") as stdout, stderr_path.open("wb") as stderr:
            completed = subprocess.run(
                list(command),
                cwd=working_directory,
                stdin=subprocess.DEVNULL,
                stdout=stdout,
                stderr=stderr,
                check=False,
            )
        return_code = completed.returncode
        launch_error: str | None = None
    except OSError as error:
        return_code = None
        launch_error = f"{type(error).__name__}: {error}"
        stderr_path.write_text(launch_error + "\n", encoding="utf-8")

    duration = (time.perf_counter_ns() - started) / 1_000_000_000
    result = {
        "pass": number,
        "status": "completed" if return_code == 0 else "failed",
        "started_at_utc": started_at,
        "completed_at_utc": utc_now(),
        "duration_seconds": round(duration, 6),
        "return_code": return_code,
        "launch_error": launch_error,
        "command": file_record(command_json_path, root=experiment_root),
        "command_text": file_record(command_text_path, root=experiment_root),
        "stdout": file_record(stdout_path, root=experiment_root),
        "stderr": file_record(stderr_path, root=experiment_root),
        "output_artifacts": artifact_inventory(
            loga_output_directory, experiment_root
        ),
    }
    write_json(metadata_path, result)
    result["metadata"] = file_record(metadata_path, root=experiment_root)
    return result


def execute_run(
    *,
    spec: RunSpec,
    source_path: Path,
    experiment_root: Path,
    loga_command_prefix: Sequence[str],
) -> dict[str, Any]:
    run_directory = experiment_root / spec.run_id
    input_directory = run_directory / "input"
    pass_root = run_directory / "passes"
    work_directory = run_directory / "work"
    run_directory.mkdir(exist_ok=False)
    input_directory.mkdir()
    pass_root.mkdir()
    work_directory.mkdir()

    started_at = utc_now()
    started = time.perf_counter_ns()
    source_content = source_path.read_bytes()
    records = validated_records(
        source_content, standard=spec.source_group == "standard"
    )
    local_name = spec.local_name or spec.source_name
    local_input = input_directory / local_name
    shuffle: dict[str, Any] | None = None

    if spec.transformation == "shuffle":
        mapping_path = run_directory / "shuffle-index.csv"
        shuffle_result = write_shuffle_input(records, local_input, mapping_path)
        shuffle = {
            "algorithm": shuffle_result["algorithm"],
            "seed": shuffle_result["seed"],
            "mapping": file_record(mapping_path, root=experiment_root),
        }
    elif spec.transformation == "copy":
        shutil.copyfile(source_path, local_input)
    else:
        raise ExperimentError(
            f"unsupported transformation for {spec.run_id}: {spec.transformation}"
        )

    local_records = validated_records(
        local_input.read_bytes(), standard=spec.source_group == "standard"
    )
    if len(local_records) != len(records):
        raise ExperimentError(f"{spec.run_id} changed the number of input records")

    loga_output_directory = work_directory / f"{local_input.name}_d"
    components_path = loga_output_directory / f"{local_input.name}.components"
    manifest_path = run_directory / "run.json"
    manifest: dict[str, Any] = {
        "schema_version": 1,
        "run_id": spec.run_id,
        "purpose": spec.purpose,
        "status": "running",
        "started_at_utc": started_at,
        "input": {
            "source_group": spec.source_group,
            "transformation": spec.transformation,
            "record_count": len(records),
            "source": file_record(source_path),
            "local": file_record(local_input, root=experiment_root),
        },
        "shuffle": shuffle,
        "loga_output_dir": relative_path(loga_output_directory, experiment_root),
        "components_file": relative_path(components_path, experiment_root),
        "passes": [],
    }
    write_json(manifest_path, manifest)

    command = build_loga_command(loga_command_prefix, local_input)
    try:
        for pass_number in (1, 2):
            result = run_pass(
                number=pass_number,
                command=command,
                working_directory=work_directory,
                pass_directory=pass_root / f"pass-{pass_number:02d}",
                loga_output_directory=loga_output_directory,
                experiment_root=experiment_root,
            )
            manifest["passes"].append(result)
            write_json(manifest_path, manifest)
            if result["return_code"] != 0:
                raise ExperimentError(
                    f"{spec.run_id} pass {pass_number} failed; see "
                    f"{result['stderr']['path']}"
                )
            if pass_number == 1 and not components_path.is_file():
                raise ExperimentError(
                    f"{spec.run_id} pass 1 did not create the components file"
                )
            if pass_number == 2:
                stdout_path = experiment_root / result["stdout"]["path"]
                if b"Loaded components" not in stdout_path.read_bytes():
                    raise ExperimentError(
                        f"{spec.run_id} pass 2 did not report loading components"
                    )

        manifest["status"] = "completed"
        manifest["completed_at_utc"] = utc_now()
        manifest["duration_seconds"] = round(
            (time.perf_counter_ns() - started) / 1_000_000_000, 6
        )
        manifest["component_artifact"] = file_record(
            components_path, root=experiment_root
        )
        manifest["final_artifacts"] = artifact_inventory(
            loga_output_directory, experiment_root
        )
        write_json(manifest_path, manifest)
        return manifest
    except BaseException as error:
        manifest["status"] = "interrupted" if isinstance(error, KeyboardInterrupt) else "failed"
        manifest["completed_at_utc"] = utc_now()
        manifest["duration_seconds"] = round(
            (time.perf_counter_ns() - started) / 1_000_000_000, 6
        )
        manifest["error"] = f"{type(error).__name__}: {error}"
        manifest["final_artifacts"] = artifact_inventory(
            loga_output_directory, experiment_root
        )
        write_json(manifest_path, manifest)
        raise


def git_provenance(repository: Path) -> dict[str, Any]:
    def git(*arguments: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            ["git", "-C", str(repository), *arguments],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )

    try:
        revision = git("rev-parse", "HEAD")
        status = git("status", "--porcelain=v1")
        branch = git("branch", "--show-current")
    except OSError as error:
        return {
            "available": False,
            "error": f"{type(error).__name__}: {error}",
        }
    if revision.returncode != 0 or status.returncode != 0:
        return {
            "available": False,
            "error": (revision.stderr or status.stderr).strip(),
        }
    status_lines = status.stdout.splitlines()
    return {
        "available": True,
        "repository": str(repository.resolve()),
        "commit": revision.stdout.strip(),
        "branch": branch.stdout.strip() if branch.returncode == 0 else None,
        "dirty": bool(status_lines),
        "status": status_lines,
    }


def validate_inputs(
    loga: Path, prepared_directory: Path, runs: Sequence[RunSpec] = RUNS
) -> tuple[Path, Path]:
    try:
        resolved_loga = loga.expanduser().resolve(strict=True)
    except FileNotFoundError as error:
        raise ExperimentError(f"Loga executable does not exist: {loga}") from error
    if not resolved_loga.is_file():
        raise ExperimentError(f"Loga executable is not a file: {resolved_loga}")
    if os.name != "nt" and not os.access(resolved_loga, os.X_OK):
        raise ExperimentError(f"Loga executable is not executable: {resolved_loga}")

    try:
        resolved_prepared = prepared_directory.expanduser().resolve(strict=True)
    except FileNotFoundError as error:
        raise ExperimentError(
            f"prepared input directory does not exist: {prepared_directory}"
        ) from error
    if not resolved_prepared.is_dir():
        raise ExperimentError(
            f"prepared input path is not a directory: {resolved_prepared}"
        )

    for spec in runs:
        source = resolved_prepared / spec.source_group / spec.source_name
        if not source.is_file():
            raise ExperimentError(f"required prepared input does not exist: {source}")
        validated_records(
            source.read_bytes(), standard=spec.source_group == "standard"
        )
    return resolved_loga, resolved_prepared


def select_runs(run_ids: Sequence[str] | None) -> tuple[RunSpec, ...]:
    if run_ids is None:
        return RUNS
    if len(set(run_ids)) != len(run_ids):
        raise ExperimentError("--runs contains a duplicate run ID")
    by_id = {spec.run_id: spec for spec in RUNS}
    try:
        return tuple(by_id[run_id] for run_id in run_ids)
    except KeyError as error:
        raise ExperimentError(f"unknown run ID: {error.args[0]}") from error


def run_experiment(
    loga: Path,
    prepared_directory: Path,
    output_directory: Path,
    runs: Sequence[RunSpec] = RUNS,
) -> int:
    if not runs:
        raise ExperimentError("at least one run must be selected")
    resolved_loga, resolved_prepared = validate_inputs(
        loga, prepared_directory, runs
    )
    proposed_output = output_directory.expanduser().resolve()
    if is_within(proposed_output, resolved_prepared):
        raise ExperimentError("output directory must be outside the prepared input tree")

    experiment_root = reserve_output_directory(proposed_output)
    repository = Path(__file__).resolve().parents[2]
    preparation_script = Path(__file__).resolve().with_name("prepare.py")
    manifest_path = experiment_root / "experiment.json"
    started_at = utc_now()
    started = time.perf_counter_ns()
    manifest: dict[str, Any] = {
        "schema_version": 1,
        "experiment": "loga-nmea-preliminary",
        "status": "running",
        "started_at_utc": started_at,
        "configuration": {
            "prepared_dir": str(resolved_prepared),
            "output_dir": str(experiment_root),
            "passes_per_run": 2,
            "runs": [spec.run_id for spec in runs],
            "loga_options": list(LOGA_OPTIONS),
            "r08_shuffle": {
                "algorithm": R08_SHUFFLE_ALGORITHM,
                "seed": R08_SHUFFLE_SEED,
            },
        },
        "provenance": {
            "loga_executable": file_record(resolved_loga),
            "runner": file_record(Path(__file__).resolve()),
            "expected_preparation_implementation": (
                file_record(preparation_script)
                if preparation_script.is_file()
                else None
            ),
            "repository": git_provenance(repository),
            "python": {
                "executable": sys.executable,
                "version": platform.python_version(),
                "implementation": platform.python_implementation(),
            },
            "platform": {
                "system": platform.system(),
                "release": platform.release(),
                "machine": platform.machine(),
                "logical_cpu_count": os.cpu_count(),
            },
            "runner_invocation": {
                "argv": [sys.executable, *sys.argv],
                "cwd": str(Path.cwd().resolve()),
                "display": display_command([sys.executable, *sys.argv]),
            },
        },
        "runs": [],
    }
    write_json(manifest_path, manifest)

    try:
        completed_runs: dict[str, dict[str, Any]] = {}
        for spec in runs:
            source_path = resolved_prepared / spec.source_group / spec.source_name
            run_manifest = execute_run(
                spec=spec,
                source_path=source_path,
                experiment_root=experiment_root,
                loga_command_prefix=(str(resolved_loga),),
            )
            completed_runs[spec.run_id] = run_manifest
            manifest["runs"].append(
                {
                    "run_id": spec.run_id,
                    "status": run_manifest["status"],
                    "manifest": f"{spec.run_id}/run.json",
                    "input_sha256": run_manifest["input"]["local"]["sha256"],
                    "record_count": run_manifest["input"]["record_count"],
                }
            )
            write_json(manifest_path, manifest)

        if {"R01", "R07"}.issubset(completed_runs) and (
            completed_runs["R01"]["input"]["local"]["sha256"]
            != completed_runs["R07"]["input"]["local"]["sha256"]
        ):
            raise ExperimentError("R07 is not an exact input repeat of R01")

        manifest["status"] = "completed"
        manifest["completed_at_utc"] = utc_now()
        manifest["duration_seconds"] = round(
            (time.perf_counter_ns() - started) / 1_000_000_000, 6
        )
        write_json(manifest_path, manifest)
        print(f"completed {len(runs)} run(s): {experiment_root}")
        return 0
    except BaseException as error:
        manifest["status"] = "interrupted" if isinstance(error, KeyboardInterrupt) else "failed"
        manifest["completed_at_utc"] = utc_now()
        manifest["duration_seconds"] = round(
            (time.perf_counter_ns() - started) / 1_000_000_000, 6
        )
        manifest["error"] = f"{type(error).__name__}: {error}"
        write_json(manifest_path, manifest)
        raise


def parse_args(arguments: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--loga",
        type=Path,
        required=True,
        help="path to the compiled Loga executable",
    )
    parser.add_argument(
        "--prepared-dir",
        type=Path,
        required=True,
        help="prepared input root containing standard/ and prefix-retained/",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        required=True,
        help="new directory in which to write the selected isolated runs",
    )
    parser.add_argument(
        "--runs",
        nargs="+",
        choices=[spec.run_id for spec in RUNS],
        metavar="RUN",
        help="run IDs to execute; omit this option to execute R01 through R09",
    )
    return parser.parse_args(arguments)


def main(arguments: Sequence[str] | None = None) -> int:
    args = parse_args(arguments)
    try:
        runs = select_runs(args.runs)
        return run_experiment(
            args.loga, args.prepared_dir, args.output_dir, runs=runs
        )
    except KeyboardInterrupt:
        print("experiment interrupted", file=sys.stderr)
        return 130
    except (ExperimentError, OSError) as error:
        print(f"experiment failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
