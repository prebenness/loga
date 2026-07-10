#!/usr/bin/env python3
"""Collect structured results from the preliminary NMEA experiment."""

from __future__ import annotations

import argparse
import csv
import json
import math
import re
import struct
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Any, Iterable, Mapping, Sequence


ANSI_ESCAPE = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
SUMMARY = re.compile(r"^\s*Summary:\s*(\d+)\s+clusters\s*$")
TEMPLATE = re.compile(r"^\s*C(\d+)\s+(?:\N{BLACK CIRCLE}\s+)?(.+?)\s*$")
COMPONENT_HEADER = re.compile(r"^\s*(?:◪\s+)?Label:\s*C(\d+)\s+\((\d+)\)\s*$")
EXCLUDED = re.compile(r"^\s*Excluded\s+(\d+)\s*$")
NMEA_IDENTIFIER = re.compile(r"\$([A-Z0-9]{5})(?=,)")
RUN_DIRECTORY = re.compile(r"R\d{2}")

COMPARISON_PAIRS = (("R01", "R07"), ("R01", "R08"), ("R06", "R09"))


@dataclass(frozen=True)
class Assignment:
    input_row: int
    source_row: int
    label: int
    template: str
    identifier: str
    message: str


@dataclass(frozen=True)
class ComponentCounts:
    component_membership_count: int
    excluded_from_alignment_count: int

    @property
    def alignment_support_count(self) -> int:
        return self.component_membership_count - self.excluded_from_alignment_count


@dataclass(frozen=True)
class CollectedRun:
    run_id: str
    assignments: tuple[Assignment, ...]
    templates: Mapping[int, str]
    component_counts: Mapping[int, ComponentCounts]
    summary: Mapping[str, Any]


def strip_ansi(text: str) -> str:
    return ANSI_ESCAPE.sub("", text).replace("\r", "")


def parse_final_templates(stdout: str) -> dict[int, str]:
    """Parse the final C<label> template lines after the last cluster summary."""
    lines = strip_ansi(stdout).splitlines()
    summaries = [
        (index, int(match.group(1)))
        for index, line in enumerate(lines)
        if (match := SUMMARY.fullmatch(line))
    ]
    if not summaries:
        raise ValueError("second-pass output contains no cluster Summary block")

    start, expected_count = summaries[-1]
    templates: dict[int, str] = {}
    for line in lines[start + 1 :]:
        match = TEMPLATE.fullmatch(line)
        if match:
            label = int(match.group(1))
            if label in templates:
                raise ValueError(f"duplicate final template label C{label}")
            templates[label] = match.group(2)
            if len(templates) == expected_count:
                break
        elif templates and line.strip() and set(line.strip()) != {"-"}:
            break

    if len(templates) != expected_count:
        raise ValueError(
            f"final Summary declares {expected_count} templates; parsed {len(templates)}"
        )
    return templates


def parse_component_counts(stdout: str) -> dict[int, ComponentCounts]:
    """Parse component membership and alignment exclusions from pass-two stdout."""
    counts: dict[int, ComponentCounts] = {}
    labels_with_exclusions: set[int] = set()
    current_label: int | None = None

    for line in strip_ansi(stdout).splitlines():
        if match := COMPONENT_HEADER.fullmatch(line):
            label = int(match.group(1))
            membership = int(match.group(2))
            if label in counts:
                raise ValueError(f"duplicate component header C{label}")
            counts[label] = ComponentCounts(membership, 0)
            current_label = label
            continue

        if match := EXCLUDED.fullmatch(line):
            if current_label is None:
                raise ValueError("alignment exclusion appears before a component header")
            if current_label in labels_with_exclusions:
                raise ValueError(f"duplicate alignment exclusion for C{current_label}")
            excluded = int(match.group(1))
            membership = counts[current_label].component_membership_count
            if excluded > membership:
                raise ValueError(
                    f"C{current_label} excludes {excluded} messages from "
                    f"a component containing {membership}"
                )
            counts[current_label] = ComponentCounts(membership, excluded)
            labels_with_exclusions.add(current_label)

    if not counts:
        raise ValueError("second-pass output contains no component headers")
    return counts


def read_armadillo_integer_row(path: Path) -> list[int]:
    """Read a native-endian Armadillo binary integer row."""
    with path.open("rb") as stream:
        header = stream.readline().decode("ascii", errors="strict").strip()
        dimensions = stream.readline().decode("ascii", errors="strict").strip()
        payload = stream.read()

    match = re.fullmatch(r"ARMA_MAT_BIN_I([US])(\d{3})", header)
    if not match:
        raise ValueError(f"unsupported Armadillo header in {path}: {header!r}")
    signed = match.group(1) == "S"
    width = int(match.group(2))
    format_code = {
        (False, 4): "I",
        (False, 8): "Q",
        (True, 4): "i",
        (True, 8): "q",
    }.get((signed, width))
    if format_code is None:
        raise ValueError(f"unsupported Armadillo integer width in {path}: {width}")

    try:
        rows, columns = (int(value) for value in dimensions.split())
    except (TypeError, ValueError) as error:
        raise ValueError(f"invalid Armadillo dimensions in {path}: {dimensions!r}") from error
    if rows != 1:
        raise ValueError(f"expected an Armadillo row in {path}; found {rows} x {columns}")

    expected_bytes = columns * width
    if len(payload) != expected_bytes:
        raise ValueError(
            f"invalid payload size in {path}: expected {expected_bytes}, found {len(payload)}"
        )
    if not columns:
        return []
    values = list(struct.unpack(f"={columns}{format_code}", payload))
    if any(value < 0 for value in values):
        raise ValueError(f"component labels must be non-negative: {path}")
    if not signed:
        sentinel = (1 << (width * 8)) - 1
        if sentinel in values:
            raise ValueError(f"components contain the unassigned-label sentinel: {path}")
    return values


def extract_identifier(message: str) -> str:
    match = NMEA_IDENTIFIER.search(message)
    return match.group(1) if match else "UNKNOWN"


def _manifest_value(manifest: Mapping[str, Any], candidates: Sequence[str]) -> Any:
    for candidate in candidates:
        value: Any = manifest
        try:
            for part in candidate.split("."):
                value = value[part]
        except (KeyError, TypeError):
            continue
        if value not in (None, ""):
            return value
    return None


def _result_path(results_dir: Path, value: Any, field: str) -> Path:
    if isinstance(value, Mapping):
        value = value.get("path")
    if not isinstance(value, str):
        raise ValueError(f"run manifest has no usable {field} path")
    relative = PurePosixPath(value)
    if relative.is_absolute() or ".." in relative.parts:
        raise ValueError(f"{field} must be relative to the result root: {value!r}")
    path = results_dir.joinpath(*relative.parts)
    resolved_root = results_dir.resolve()
    resolved_path = path.resolve()
    if resolved_path != resolved_root and resolved_root not in resolved_path.parents:
        raise ValueError(f"{field} escapes the result root: {value!r}")
    return path


def resolve_run_files(
    results_dir: Path, run_dir: Path, manifest: Mapping[str, Any]
) -> dict[str, Path | None]:
    """Resolve runner-recorded POSIX paths, with limited schema compatibility."""
    input_value = _manifest_value(
        manifest,
        (
            "input_file",
            "input_path",
            "paths.input",
            "input.file",
            "input.path",
            "input.local.path",
        ),
    )
    components_value = _manifest_value(
        manifest,
        (
            "components_file",
            "components_path",
            "paths.components",
            "artifacts.components",
            "artifacts.components.path",
        ),
    )
    stdout_value = _manifest_value(
        manifest,
        (
            "second_pass_stdout",
            "second_pass_stdout_file",
            "paths.second_pass_stdout",
            "passes.pass_02.stdout",
            "passes.pass_02.stdout_file",
            "pass_02.stdout",
            "pass_02.stdout_file",
        ),
    )
    mapping_value = _manifest_value(
        manifest,
        (
            "shuffle_index_file",
            "shuffle_mapping_file",
            "source_row_map_file",
            "paths.shuffle_index",
            "artifacts.shuffle_index",
            "shuffle.mapping.path",
        ),
    )

    if stdout_value is None and isinstance(manifest.get("passes"), list):
        for pass_record in manifest["passes"]:
            if not isinstance(pass_record, Mapping):
                continue
            pass_id = pass_record.get("pass", pass_record.get("id", pass_record.get("name")))
            normalised_id = str(pass_id).lower().replace("_", "-")
            if normalised_id not in {"2", "02", "pass-2", "pass-02", "second"}:
                continue
            stdout_value = _manifest_value(
                pass_record,
                ("stdout.path", "stdout_file", "files.stdout", "stdout"),
            )
            if stdout_value is not None:
                break

    # The strict runner layout is also accepted if an older manifest omits paths.
    if input_value is None:
        inputs = sorted(path for path in (run_dir / "input").glob("*") if path.is_file())
        if len(inputs) == 1:
            input_value = inputs[0].relative_to(results_dir).as_posix()
    if stdout_value is None:
        candidate = run_dir / "passes" / "pass-02" / "stdout.log"
        if candidate.is_file():
            stdout_value = candidate.relative_to(results_dir).as_posix()
    if components_value is None:
        components = sorted((run_dir / "work").glob("*_d/*.components"))
        if len(components) == 1:
            components_value = components[0].relative_to(results_dir).as_posix()
    if mapping_value is None:
        candidate = run_dir / "shuffle-index.csv"
        if candidate.is_file():
            mapping_value = candidate.relative_to(results_dir).as_posix()

    return {
        "input": _result_path(results_dir, input_value, "input"),
        "components": _result_path(results_dir, components_value, "components"),
        "second_pass_stdout": _result_path(
            results_dir, stdout_value, "second-pass stdout"
        ),
        "source_row_map": (
            _result_path(results_dir, mapping_value, "source-row map")
            if mapping_value is not None
            else None
        ),
    }


def read_source_row_map(path: Path, message_count: int) -> list[int]:
    with path.open("r", encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream)
        if not reader.fieldnames:
            raise ValueError(f"source-row map has no header: {path}")
        input_field = next(
            (
                name
                for name in (
                    "input_row_0based",
                    "input_row",
                    "run_row",
                    "shuffled_row",
                )
                if name in reader.fieldnames
            ),
            None,
        )
        source_field = next(
            (
                name
                for name in ("source_row_0based", "source_row", "original_row")
                if name in reader.fieldnames
            ),
            None,
        )
        if source_field is None:
            raise ValueError(f"source-row map has no source-row column: {path}")

        mapping: dict[int, int] = {}
        for implicit_row, row in enumerate(reader):
            try:
                input_row = int(row[input_field]) if input_field else implicit_row
                source_row = int(row[source_field])
            except (KeyError, TypeError, ValueError) as error:
                raise ValueError(f"invalid source-row map entry in {path}") from error
            if input_row in mapping:
                raise ValueError(f"duplicate input row {input_row} in {path}")
            mapping[input_row] = source_row

    expected = set(range(message_count))
    if set(mapping) != expected or set(mapping.values()) != expected:
        raise ValueError(f"source-row map is not a permutation of 0..{message_count - 1}")
    return [mapping[row] for row in range(message_count)]


def _distinct_examples(assignments: Iterable[Assignment], limit: int = 3) -> list[str]:
    examples: list[str] = []
    seen: set[str] = set()
    for assignment in assignments:
        if assignment.message not in seen:
            examples.append(assignment.message)
            seen.add(assignment.message)
            if len(examples) == limit:
                break
    return examples


def analyse_run(
    run_id: str,
    messages: Sequence[str],
    labels: Sequence[int],
    templates: Mapping[int, str],
    source_rows: Sequence[int] | None = None,
    component_counts: Mapping[int, ComponentCounts] | None = None,
) -> CollectedRun:
    if len(messages) != len(labels):
        raise ValueError(
            f"{run_id}: input has {len(messages)} messages but components has {len(labels)} labels"
        )
    if source_rows is None:
        source_rows = list(range(len(messages)))
    if len(source_rows) != len(messages):
        raise ValueError(f"{run_id}: source-row map length differs from input length")

    label_set = set(labels)
    template_set = set(templates)
    if label_set != template_set:
        missing = sorted(label_set - template_set)
        unused = sorted(template_set - label_set)
        raise ValueError(
            f"{run_id}: template/component labels differ; missing={missing}, unused={unused}"
        )

    membership_counts = Counter(labels)
    if component_counts is None:
        component_counts = {
            label: ComponentCounts(membership, 0)
            for label, membership in membership_counts.items()
        }
    if set(component_counts) != label_set:
        missing = sorted(label_set - set(component_counts))
        unused = sorted(set(component_counts) - label_set)
        raise ValueError(
            f"{run_id}: component-count labels differ; missing={missing}, unused={unused}"
        )
    for label, counts in component_counts.items():
        observed_membership = membership_counts[label]
        if counts.component_membership_count != observed_membership:
            raise ValueError(
                f"{run_id}: C{label} stdout reports "
                f"{counts.component_membership_count} component members; "
                f"components file contains {observed_membership}"
            )
        if counts.excluded_from_alignment_count < 0:
            raise ValueError(f"{run_id}: C{label} has a negative exclusion count")
        if counts.alignment_support_count <= 0:
            raise ValueError(f"{run_id}: C{label} has no alignment-support messages")

    assignments = tuple(
        Assignment(
            input_row=input_row,
            source_row=source_rows[input_row],
            label=label,
            template=templates[label],
            identifier=extract_identifier(messages[input_row]),
            message=messages[input_row],
        )
        for input_row, label in enumerate(labels)
    )

    by_template: dict[int, list[Assignment]] = defaultdict(list)
    by_identifier: dict[str, set[int]] = defaultdict(set)
    identifier_totals: Counter[str] = Counter()
    for assignment in assignments:
        by_template[assignment.label].append(assignment)
        by_identifier[assignment.identifier].add(assignment.label)
        identifier_totals[assignment.identifier] += 1

    fragmentation = {
        identifier: {
            "message_count": identifier_totals[identifier],
            "template_count": len(template_labels),
            "template_labels": [f"C{label}" for label in sorted(template_labels)],
            "fragmented": len(template_labels) > 1,
        }
        for identifier, template_labels in sorted(by_identifier.items())
    }
    mixed_labels = [
        f"C{label}"
        for label, members in sorted(by_template.items())
        if len({member.identifier for member in members}) > 1
    ]
    component_support = {
        f"C{label}": {
            "component_membership_count": counts.component_membership_count,
            "excluded_from_alignment_count": counts.excluded_from_alignment_count,
            "alignment_support_count": counts.alignment_support_count,
        }
        for label, counts in sorted(component_counts.items())
    }
    total_excluded = sum(
        counts.excluded_from_alignment_count for counts in component_counts.values()
    )
    summary = {
        "run_id": run_id,
        "message_count": len(messages),
        "component_membership_count": len(messages),
        "template_count": len(templates),
        "identifier_counts": dict(sorted(identifier_totals.items())),
        "mixed_identifier_template_count": len(mixed_labels),
        "mixed_identifier_templates": mixed_labels,
        "identifier_fragmentation": fragmentation,
        "fragmented_identifier_count": sum(
            item["fragmented"] for item in fragmentation.values()
        ),
        "excluded_from_alignment_count": total_excluded,
        "alignment_support_count": len(messages) - total_excluded,
        "components_with_exclusions_count": sum(
            counts.excluded_from_alignment_count > 0
            for counts in component_counts.values()
        ),
        "component_support": component_support,
    }
    return CollectedRun(
        run_id, assignments, dict(templates), dict(component_counts), summary
    )


def template_rows(run: CollectedRun) -> list[dict[str, Any]]:
    by_template: dict[int, list[Assignment]] = defaultdict(list)
    identifier_templates: dict[str, set[int]] = defaultdict(set)
    for assignment in run.assignments:
        by_template[assignment.label].append(assignment)
        identifier_templates[assignment.identifier].add(assignment.label)

    rows: list[dict[str, Any]] = []
    for label, template in sorted(run.templates.items()):
        members = by_template[label]
        counts = run.component_counts[label]
        identifier_counts = Counter(member.identifier for member in members)
        fragmented = sorted(
            identifier
            for identifier in identifier_counts
            if len(identifier_templates[identifier]) > 1
        )
        examples = _distinct_examples(members)
        rows.append(
            {
                "run_id": run.run_id,
                "template_label": f"C{label}",
                "template": template,
                "component_membership_count": counts.component_membership_count,
                "excluded_from_alignment_count": counts.excluded_from_alignment_count,
                "alignment_support_count": counts.alignment_support_count,
                "identifier_counts_json": json.dumps(
                    dict(sorted(identifier_counts.items())), separators=(",", ":")
                ),
                "mixed_identifier": str(len(identifier_counts) > 1).lower(),
                "fragmented_identifiers": ";".join(fragmented),
                "example_1": examples[0] if len(examples) > 0 else "",
                "example_2": examples[1] if len(examples) > 1 else "",
                "example_3": examples[2] if len(examples) > 2 else "",
            }
        )
    return rows


def write_run_outputs(run_dir: Path, run: CollectedRun) -> None:
    assignment_fields = (
        "run_id",
        "input_row_0based",
        "source_row_0based",
        "component_label",
        "associated_template",
        "nmea_identifier",
        "message",
    )
    with (run_dir / "message-assignments.csv").open(
        "w", encoding="utf-8", newline=""
    ) as stream:
        writer = csv.DictWriter(stream, fieldnames=assignment_fields)
        writer.writeheader()
        for assignment in run.assignments:
            writer.writerow(
                {
                    "run_id": run.run_id,
                    "input_row_0based": assignment.input_row,
                    "source_row_0based": assignment.source_row,
                    "component_label": f"C{assignment.label}",
                    "associated_template": assignment.template,
                    "nmea_identifier": assignment.identifier,
                    "message": assignment.message,
                }
            )

    template_fields = (
        "run_id",
        "template_label",
        "template",
        "component_membership_count",
        "excluded_from_alignment_count",
        "alignment_support_count",
        "identifier_counts_json",
        "mixed_identifier",
        "fragmented_identifiers",
        "example_1",
        "example_2",
        "example_3",
    )
    with (run_dir / "templates.csv").open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=template_fields)
        writer.writeheader()
        writer.writerows(template_rows(run))

    (run_dir / "summary.json").write_text(
        json.dumps(run.summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


def collect_run(results_dir: Path, run_dir: Path) -> CollectedRun:
    manifest_path = run_dir / "run.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    run_id = manifest.get("run_id", run_dir.name)
    if not isinstance(run_id, str) or not RUN_DIRECTORY.fullmatch(run_id):
        raise ValueError(f"invalid run ID in {manifest_path}: {run_id!r}")

    files = resolve_run_files(results_dir, run_dir, manifest)
    input_path = files["input"]
    components_path = files["components"]
    stdout_path = files["second_pass_stdout"]
    assert isinstance(input_path, Path)
    assert isinstance(components_path, Path)
    assert isinstance(stdout_path, Path)

    messages = input_path.read_text(encoding="utf-8").splitlines()
    if not messages or any(not message for message in messages):
        raise ValueError(f"{run_id}: prepared input contains an empty record")
    labels = read_armadillo_integer_row(components_path)
    stdout = stdout_path.read_text(encoding="utf-8")
    templates = parse_final_templates(stdout)
    component_counts = parse_component_counts(stdout)

    mapping_path = files["source_row_map"]
    if mapping_path is not None:
        assert isinstance(mapping_path, Path)
        source_rows = read_source_row_map(mapping_path, len(messages))
    else:
        source_rows = list(range(len(messages)))
    if run_id == "R08" and mapping_path is None:
        raise ValueError("R08 requires the runner-recorded shuffle index")

    return analyse_run(
        run_id,
        messages,
        labels,
        templates,
        source_rows,
        component_counts,
    )


def exact_partition_equivalence(left: Sequence[int], right: Sequence[int]) -> bool:
    if len(left) != len(right):
        raise ValueError("partitions have different lengths")
    left_to_right: dict[int, int] = {}
    right_to_left: dict[int, int] = {}
    for left_label, right_label in zip(left, right):
        if left_label in left_to_right and left_to_right[left_label] != right_label:
            return False
        if right_label in right_to_left and right_to_left[right_label] != left_label:
            return False
        left_to_right[left_label] = right_label
        right_to_left[right_label] = left_label
    return True


def adjusted_rand_index(left: Sequence[int], right: Sequence[int]) -> float:
    if len(left) != len(right):
        raise ValueError("partitions have different lengths")
    if len(left) < 2:
        return 1.0

    contingency = Counter(zip(left, right))
    left_counts = Counter(left)
    right_counts = Counter(right)
    pairs = math.comb(len(left), 2)
    joined = sum(math.comb(count, 2) for count in contingency.values())
    left_pairs = sum(math.comb(count, 2) for count in left_counts.values())
    right_pairs = sum(math.comb(count, 2) for count in right_counts.values())
    expected = left_pairs * right_pairs / pairs
    maximum = 0.5 * (left_pairs + right_pairs)
    denominator = maximum - expected
    if denominator == 0:
        return 1.0 if exact_partition_equivalence(left, right) else 0.0
    return (joined - expected) / denominator


def compare_runs(left: CollectedRun, right: CollectedRun) -> dict[str, Any]:
    left_by_source = {item.source_row: item for item in left.assignments}
    right_by_source = {item.source_row: item for item in right.assignments}
    if len(left_by_source) != len(left.assignments):
        raise ValueError(f"{left.run_id} contains duplicate source-row indices")
    if len(right_by_source) != len(right.assignments):
        raise ValueError(f"{right.run_id} contains duplicate source-row indices")
    if set(left_by_source) != set(right_by_source):
        raise ValueError(
            f"{left.run_id}/{right.run_id} do not cover the same source-row indices"
        )

    source_rows = sorted(left_by_source)
    left_labels = [left_by_source[row].label for row in source_rows]
    right_labels = [right_by_source[row].label for row in source_rows]
    left_templates = set(left.templates.values())
    right_templates = set(right.templates.values())
    union = left_templates | right_templates
    intersection = left_templates & right_templates
    return {
        "left_run": left.run_id,
        "right_run": right.run_id,
        "messages_compared": len(source_rows),
        "exact_partition_equivalence": exact_partition_equivalence(
            left_labels, right_labels
        ),
        "adjusted_rand_index": adjusted_rand_index(left_labels, right_labels),
        "exact_template_set_jaccard": len(intersection) / len(union) if union else 1.0,
        "left_template_count": len(left_templates),
        "right_template_count": len(right_templates),
        "shared_exact_template_count": len(intersection),
    }


def collect_experiment(results_dir: Path) -> dict[str, CollectedRun]:
    experiment_manifest = results_dir / "experiment.json"
    if not experiment_manifest.is_file():
        raise ValueError(f"missing experiment manifest: {experiment_manifest}")

    run_dirs = sorted(
        path
        for path in results_dir.iterdir()
        if path.is_dir() and RUN_DIRECTORY.fullmatch(path.name)
    )
    if not run_dirs:
        raise ValueError(f"no Rxx run directories found in {results_dir}")

    runs: dict[str, CollectedRun] = {}
    for run_dir in run_dirs:
        run = collect_run(results_dir, run_dir)
        if run.run_id in runs:
            raise ValueError(f"duplicate run ID: {run.run_id}")
        runs[run.run_id] = run
        write_run_outputs(run_dir, run)

    comparisons: list[dict[str, Any]] = []
    skipped_comparisons: list[dict[str, Any]] = []
    for left, right in COMPARISON_PAIRS:
        missing = [run_id for run_id in (left, right) if run_id not in runs]
        if missing:
            skipped_comparisons.append(
                {
                    "left_run": left,
                    "right_run": right,
                    "missing_runs": missing,
                }
            )
        else:
            comparisons.append(compare_runs(runs[left], runs[right]))
    (results_dir / "comparisons.json").write_text(
        json.dumps(
            {
                "comparisons": comparisons,
                "skipped_comparisons": skipped_comparisons,
            },
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
    )
    return runs


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--results-dir",
        required=True,
        type=Path,
        help="experiment result root containing experiment.json and Rxx directories",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        runs = collect_experiment(args.results_dir)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        raise SystemExit(f"collection failed: {error}") from error
    print(f"Collected {len(runs)} runs in {args.results_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
