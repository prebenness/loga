from __future__ import annotations

import csv
import json
import struct
import tempfile
import unittest
from collections import Counter
from pathlib import Path

from experiments.nmea.collect import (
    ComponentCounts,
    adjusted_rand_index,
    analyse_run,
    collect_experiment,
    compare_runs,
    exact_partition_equivalence,
    parse_component_counts,
    parse_final_templates,
    read_armadillo_integer_row,
    read_source_row_map,
    template_rows,
)


def write_armadillo_row(path: Path, labels: list[int]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    header = f"ARMA_MAT_BIN_IU008\n1 {len(labels)}\n".encode("ascii")
    payload = struct.pack(f"={len(labels)}Q", *labels)
    path.write_bytes(header + payload)


def summary_output(templates: dict[int, str]) -> str:
    lines = [
        "----------------------------",
        f"Summary: {len(templates)} clusters",
        "----------------------------",
    ]
    lines.extend(
        f" C{label} \x1b[1;33m●\x1b[0m {template}" for label, template in templates.items()
    )
    return "\n".join(lines) + "\n"


def second_pass_output(
    templates: dict[int, str],
    component_sizes: dict[int, int],
    exclusions: dict[int, int] | None = None,
    contributors: dict[int, list[int]] | None = None,
) -> str:
    exclusions = exclusions or {}
    lines: list[str] = []
    for label in templates:
        lines.append(f"\x1b[1;33m◪\x1b[0m Label: C{label} ({component_sizes[label]})")
        if contributors is not None and label in contributors:
            row_ids = "".join(f" {row_id}" for row_id in contributors[label])
            lines.append(f"Contributors C{label}:{row_ids}")
        if label in exclusions:
            lines.append(f"\x1b[1;31m    Excluded {exclusions[label]}")
    return "\n".join(lines) + "\n" + summary_output(templates)


class ParserTests(unittest.TestCase):
    def test_only_the_last_summary_block_is_collected(self) -> None:
        output = (
            summary_output({0: "$OLD,$0"})
            + "phase boundary\n"
            + summary_output({0: "$GPRMC,$0", 1: "$GPGLL,$0"})
            + "elapsed=1.0\n"
        )

        self.assertEqual(
            parse_final_templates(output),
            {0: "$GPRMC,$0", 1: "$GPGLL,$0"},
        )

    def test_component_membership_and_exclusions_are_collected(self) -> None:
        output = second_pass_output(
            {0: "$GPRMC,$0", 1: "$GPGLL,$0"},
            {0: 5, 1: 3},
            {0: 2},
        )

        self.assertEqual(
            parse_component_counts(output),
            {
                0: ComponentCounts(5, 2),
                1: ComponentCounts(3, 0),
            },
        )

    def test_component_exclusion_cannot_exceed_membership(self) -> None:
        output = second_pass_output({0: "$GPRMC,$0"}, {0: 2}, {0: 3})

        with self.assertRaisesRegex(ValueError, "excludes 3 messages"):
            parse_component_counts(output)

    def test_exact_contributors_and_exclusion_types_are_collected(self) -> None:
        output = second_pass_output(
            {0: "$GPRMC,$0", 1: "$GPGLL,$0"},
            {0: 5, 1: 3},
            {0: 1},
            {0: [0, 2, 4], 1: [1, 3, 5]},
        )

        counts = parse_component_counts(output)
        self.assertEqual(counts[0], ComponentCounts(5, 1, frozenset({0, 2, 4})))
        self.assertEqual(counts[0].accidental_excluded_count, 1)
        self.assertEqual(counts[0].excluded_from_alignment_count, 2)
        self.assertEqual(counts[0].alignment_support_count, 3)
        self.assertEqual(counts[1], ComponentCounts(3, 0, frozenset({1, 3, 5})))

    def test_contributor_lists_must_be_complete_and_ids_unique(self) -> None:
        incomplete = second_pass_output(
            {0: "$GPRMC,$0", 1: "$GPGLL,$0"},
            {0: 2, 1: 1},
            contributors={0: [0]},
        )
        with self.assertRaisesRegex(ValueError, "missing contributor lists"):
            parse_component_counts(incomplete)

        duplicate = second_pass_output(
            {0: "$GPRMC,$0"},
            {0: 2},
            contributors={0: [0, 0]},
        )
        with self.assertRaisesRegex(ValueError, "duplicate contributor row ID"):
            parse_component_counts(duplicate)

    def test_armadillo_unsigned_64_bit_row_is_read(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / "input.components"
            write_armadillo_row(path, [2, 2, 0, 1])

            self.assertEqual(read_armadillo_integer_row(path), [2, 2, 0, 1])

    def test_armadillo_unassigned_label_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / "input.components"
            write_armadillo_row(path, [0, (1 << 64) - 1])

            with self.assertRaisesRegex(ValueError, "unassigned-label sentinel"):
                read_armadillo_integer_row(path)

    def test_source_row_map_must_be_a_complete_permutation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / "shuffle-index.csv"
            path.write_text(
                "input_row_0based,source_row_0based\n0,2\n1,0\n2,1\n",
                encoding="utf-8",
            )
            self.assertEqual(read_source_row_map(path, 3), [2, 0, 1])

            path.write_text(
                "input_row_0based,source_row_0based\n0,2\n1,0\n2,0\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(ValueError, "not a permutation"):
                read_source_row_map(path, 3)


class AnalysisTests(unittest.TestCase):
    def test_identifier_composition_examples_and_fragmentation(self) -> None:
        messages = [
            "$GPRMC,one*00",
            "$GPGLL,two*01",
            "$GPRMC,three*02",
            "$TIROT,four*03",
            "$TIROT,four*03",
        ]
        run = analyse_run(
            "R01",
            messages,
            [0, 0, 1, 2, 2],
            {0: "$GP$0", 1: "$GPRMC,$0", 2: "$TIROT,$0"},
            component_counts={
                0: ComponentCounts(2, 1),
                1: ComponentCounts(1, 0),
                2: ComponentCounts(2, 0),
            },
        )

        self.assertEqual(run.summary["mixed_identifier_templates"], ["C0"])
        self.assertTrue(run.summary["identifier_fragmentation"]["GPRMC"]["fragmented"])
        self.assertEqual(
            run.summary["identifier_fragmentation"]["GPRMC"]["template_labels"],
            ["C0", "C1"],
        )
        rows = {row["template_label"]: row for row in template_rows(run)}
        self.assertEqual(rows["C0"]["identifier_counts_json"], '{"GPGLL":1,"GPRMC":1}')
        self.assertEqual(rows["C0"]["component_membership_count"], 2)
        self.assertEqual(rows["C0"]["lof_excluded_count"], 1)
        self.assertEqual(rows["C0"]["accidental_excluded_count"], "unknown")
        self.assertEqual(rows["C0"]["excluded_from_alignment_count"], "unknown")
        self.assertEqual(rows["C0"]["alignment_support_count"], "unknown")
        self.assertEqual(run.summary["component_membership_count"], 5)
        self.assertEqual(run.summary["lof_excluded_count"], 1)
        self.assertIsNone(run.summary["accidental_excluded_count"])
        self.assertIsNone(run.summary["excluded_from_alignment_count"])
        self.assertIsNone(run.summary["alignment_support_count"])
        self.assertIsNone(run.summary["components_with_exclusions_count"])
        self.assertFalse(run.summary["contributor_status_available"])
        self.assertEqual(rows["C2"]["example_1"], "$TIROT,four*03")
        self.assertEqual(rows["C2"]["example_2"], "")

    def test_exact_contributors_are_validated_and_marked(self) -> None:
        messages = ["$GPRMC,a*00", "$GPRMC,b*01", "$GPGLL,c*02"]
        run = analyse_run(
            "R01",
            messages,
            [0, 0, 1],
            {0: "$GPRMC,$0", 1: "$GPGLL,$0"},
            component_counts={
                0: ComponentCounts(2, 0, frozenset({0})),
                1: ComponentCounts(1, 0, frozenset({2})),
            },
        )

        self.assertEqual(
            [assignment.alignment_contributor for assignment in run.assignments],
            [True, False, True],
        )
        self.assertTrue(run.summary["contributor_status_available"])
        self.assertEqual(run.summary["lof_excluded_count"], 0)
        self.assertEqual(run.summary["accidental_excluded_count"], 1)
        self.assertEqual(run.summary["excluded_from_alignment_count"], 1)
        self.assertEqual(run.summary["alignment_support_count"], 2)

        wrong_component = {
            0: ComponentCounts(2, 0, frozenset({2})),
            1: ComponentCounts(1, 0, frozenset({1})),
        }
        with self.assertRaisesRegex(ValueError, "assigned to C1, not C0"):
            analyse_run(
                "R01",
                messages,
                [0, 0, 1],
                {0: "$GPRMC,$0", 1: "$GPGLL,$0"},
                component_counts=wrong_component,
            )

        out_of_range = {
            0: ComponentCounts(2, 0, frozenset({0, 3})),
            1: ComponentCounts(1, 0, frozenset({2})),
        }
        with self.assertRaisesRegex(ValueError, "row 3 is out of range"):
            analyse_run(
                "R01",
                messages,
                [0, 0, 1],
                {0: "$GPRMC,$0", 1: "$GPGLL,$0"},
                component_counts=out_of_range,
            )

    def test_template_and_component_labels_must_agree(self) -> None:
        with self.assertRaisesRegex(ValueError, "template/component labels differ"):
            analyse_run("R01", ["$GPRMC,one*00"], [1], {0: "$GPRMC,$0"})

    def test_partition_metrics_ignore_label_names(self) -> None:
        left = [0, 0, 1, 1]
        renamed = [7, 7, 3, 3]
        different = [0, 1, 0, 1]

        self.assertTrue(exact_partition_equivalence(left, renamed))
        self.assertEqual(adjusted_rand_index(left, renamed), 1.0)
        self.assertFalse(exact_partition_equivalence(left, different))
        self.assertAlmostEqual(adjusted_rand_index(left, different), -0.5)

    def test_cross_run_comparison_uses_source_rows(self) -> None:
        messages = ["$GPRMC,a*00", "$GPRMC,b*01", "$GPGLL,c*02", "$GPGLL,d*03"]
        left = analyse_run("R01", messages, [0, 0, 1, 1], {0: "A", 1: "B"})
        right = analyse_run(
            "R08",
            [messages[index] for index in [2, 0, 3, 1]],
            [4, 9, 4, 9],
            {4: "B", 9: "A"},
            [2, 0, 3, 1],
        )

        comparison = compare_runs(left, right)
        self.assertTrue(comparison["exact_partition_equivalence"])
        self.assertEqual(comparison["adjusted_rand_index"], 1.0)
        self.assertEqual(comparison["exact_template_set_jaccard"], 1.0)


class CollectionWorkflowTests(unittest.TestCase):
    def create_run(
        self,
        root: Path,
        run_id: str,
        messages: list[str],
        labels: list[int],
        templates: dict[int, str],
        source_rows: list[int] | None = None,
        contributors: dict[int, list[int]] | None = None,
    ) -> None:
        run_dir = root / run_id
        input_path = run_dir / "input" / "input.txt"
        stdout_path = run_dir / "passes" / "pass-02" / "stdout.log"
        components_path = run_dir / "work" / "input.txt_d" / "input.txt.components"
        input_path.parent.mkdir(parents=True)
        stdout_path.parent.mkdir(parents=True)
        input_path.write_text("\n".join(messages) + "\n", encoding="utf-8")
        stdout_path.write_text(
            second_pass_output(
                templates,
                dict(Counter(labels)),
                contributors=contributors,
            ),
            encoding="utf-8",
        )
        write_armadillo_row(components_path, labels)

        manifest: dict[str, object] = {
            "run_id": run_id,
            "input": {"local": {"path": input_path.relative_to(root).as_posix()}},
            "components_file": components_path.relative_to(root).as_posix(),
            "passes": [
                {
                    "pass": 2,
                    "stdout": {"path": stdout_path.relative_to(root).as_posix()},
                }
            ],
        }
        if source_rows is not None:
            mapping_path = run_dir / "shuffle-index.csv"
            with mapping_path.open("w", encoding="utf-8", newline="") as stream:
                writer = csv.writer(stream)
                writer.writerow(("input_row_0based", "source_row_0based"))
                writer.writerows(enumerate(source_rows))
            manifest["shuffle"] = {
                "mapping": {"path": mapping_path.relative_to(root).as_posix()}
            }
        (run_dir / "run.json").write_text(
            json.dumps(manifest), encoding="utf-8"
        )

    def test_collection_writes_per_run_outputs_and_available_comparisons(self) -> None:
        source_messages = [
            "$GPRMC,a*00",
            "$GPRMC,b*01",
            "$GPGLL,c*02",
            "$GPGLL,d*03",
        ]
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            (root / "experiment.json").write_text("{}\n", encoding="utf-8")
            self.create_run(
                root,
                "R01",
                source_messages,
                [0, 0, 1, 1],
                {0: "$GPRMC,$0", 1: "$GPGLL,$0"},
            )
            self.create_run(
                root,
                "R07",
                source_messages,
                [1, 1, 0, 0],
                {0: "$GPGLL,$0", 1: "$GPRMC,$0"},
            )
            order = [2, 0, 3, 1]
            self.create_run(
                root,
                "R08",
                [source_messages[index] for index in order],
                [0, 1, 0, 1],
                {0: "$GPGLL,$0", 1: "$GPRMC,$0"},
                order,
            )

            runs = collect_experiment(root)

            self.assertEqual(set(runs), {"R01", "R07", "R08"})
            for run_id in runs:
                self.assertTrue((root / run_id / "message-assignments.csv").is_file())
                self.assertTrue((root / run_id / "templates.csv").is_file())
                self.assertTrue((root / run_id / "summary.json").is_file())
                with (root / run_id / "message-assignments.csv").open(
                    encoding="utf-8", newline=""
                ) as stream:
                    assignment_reader = csv.DictReader(stream)
                    assignment_rows = list(assignment_reader)
                    assignment_fields = assignment_reader.fieldnames
                self.assertIn("component_label", assignment_fields or [])
                self.assertIn("associated_template", assignment_fields or [])
                self.assertIn("alignment_contributor", assignment_fields or [])
                self.assertEqual(
                    {row["alignment_contributor"] for row in assignment_rows},
                    {"unknown"},
                )
                with (root / run_id / "templates.csv").open(
                    encoding="utf-8", newline=""
                ) as stream:
                    template_reader = csv.DictReader(stream)
                    template_rows_output = list(template_reader)
                    template_fields = template_reader.fieldnames
                self.assertIn("component_membership_count", template_fields or [])
                self.assertIn("lof_excluded_count", template_fields or [])
                self.assertIn("accidental_excluded_count", template_fields or [])
                self.assertIn("excluded_from_alignment_count", template_fields or [])
                self.assertIn("alignment_support_count", template_fields or [])
                self.assertEqual(
                    {row["alignment_support_count"] for row in template_rows_output},
                    {"unknown"},
                )

            comparison_report = json.loads(
                (root / "comparisons.json").read_text(encoding="utf-8")
            )
            self.assertEqual(len(comparison_report["comparisons"]), 2)
            self.assertTrue(
                all(
                    comparison["exact_partition_equivalence"]
                    for comparison in comparison_report["comparisons"]
                )
            )
            self.assertEqual(
                comparison_report["skipped_comparisons"],
                [
                    {
                        "left_run": "R06",
                        "missing_runs": ["R06", "R09"],
                        "right_run": "R09",
                    }
                ],
            )

    def test_collection_writes_exact_contributor_status(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            (root / "experiment.json").write_text("{}\n", encoding="utf-8")
            self.create_run(
                root,
                "R01",
                ["$GPRMC,a*00", "$GPRMC,b*01", "$GPGLL,c*02"],
                [0, 0, 1],
                {0: "$GPRMC,$0", 1: "$GPGLL,$0"},
                contributors={0: [0], 1: [2]},
            )

            run = collect_experiment(root)["R01"]
            self.assertEqual(run.summary["accidental_excluded_count"], 1)
            self.assertEqual(run.summary["excluded_from_alignment_count"], 1)
            self.assertEqual(run.summary["alignment_support_count"], 2)

            with (root / "R01" / "message-assignments.csv").open(
                encoding="utf-8", newline=""
            ) as stream:
                assignment_rows = list(csv.DictReader(stream))
            self.assertEqual(
                [row["alignment_contributor"] for row in assignment_rows],
                ["true", "false", "true"],
            )

            with (root / "R01" / "templates.csv").open(
                encoding="utf-8", newline=""
            ) as stream:
                rows_by_label = {
                    row["template_label"]: row for row in csv.DictReader(stream)
                }
            self.assertEqual(rows_by_label["C0"]["lof_excluded_count"], "0")
            self.assertEqual(rows_by_label["C0"]["accidental_excluded_count"], "1")
            self.assertEqual(rows_by_label["C0"]["excluded_from_alignment_count"], "1")
            self.assertEqual(rows_by_label["C0"]["alignment_support_count"], "1")


if __name__ == "__main__":
    unittest.main()
