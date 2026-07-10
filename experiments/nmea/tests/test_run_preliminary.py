from __future__ import annotations

import csv
import json
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

from experiments.nmea.run_preliminary import (
    LARGE_SHIP,
    RUNS,
    ExperimentError,
    RunSpec,
    execute_run,
    permutation_indices,
    reserve_output_directory,
    select_runs,
    write_shuffle_input,
)


class RunDefinitionTests(unittest.TestCase):
    def test_canonical_runs_are_defined_once_and_in_order(self) -> None:
        self.assertEqual([spec.run_id for spec in RUNS], [f"R{i:02d}" for i in range(1, 10)])
        self.assertEqual(RUNS[0].source_name, LARGE_SHIP)
        self.assertEqual(RUNS[6].source_name, LARGE_SHIP)
        self.assertEqual(RUNS[7].transformation, "shuffle")
        self.assertEqual(RUNS[8].source_group, "prefix-retained")

    def test_subset_selection_preserves_requested_order(self) -> None:
        self.assertEqual(
            [spec.run_id for spec in select_runs(["R08", "R01"])],
            ["R08", "R01"],
        )
        with self.assertRaisesRegex(ExperimentError, "duplicate"):
            select_runs(["R01", "R01"])


class ShuffleTests(unittest.TestCase):
    def test_permutation_is_stable_and_complete(self) -> None:
        first = permutation_indices(20)
        second = permutation_indices(20)

        self.assertEqual(first, second)
        self.assertEqual(sorted(first), list(range(20)))
        self.assertNotEqual(first, list(range(20)))

    def test_shuffle_writes_a_reversible_row_mapping(self) -> None:
        records = [f"$T{i}*00\n".encode("ascii") for i in range(8)]
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            destination = root / "shuffled.txt"
            mapping = root / "shuffle-index.csv"

            write_shuffle_input(records, destination, mapping)
            shuffled = destination.read_bytes().splitlines(keepends=True)
            with mapping.open(encoding="utf-8", newline="") as stream:
                rows = list(csv.DictReader(stream))

            reconstructed: list[bytes | None] = [None] * len(records)
            for row, record in zip(rows, shuffled):
                reconstructed[int(row["source_row_0based"])] = record
            self.assertEqual(
                [int(row["input_row_0based"]) for row in rows],
                list(range(len(records))),
            )
            self.assertEqual(reconstructed, records)


class OutputSafetyTests(unittest.TestCase):
    def test_existing_output_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            existing = Path(temporary_directory) / "existing"
            existing.mkdir()
            with self.assertRaisesRegex(ExperimentError, "already exists"):
                reserve_output_directory(existing)


class SyntheticExecutionTests(unittest.TestCase):
    def test_run_is_isolated_and_invokes_loga_twice(self) -> None:
        fake_loga_source = textwrap.dedent(
            """
            import argparse
            from pathlib import Path

            parser = argparse.ArgumentParser()
            parser.add_argument("--cluster-algo")
            parser.add_argument("--refine-algo")
            parser.add_argument("--threshold")
            parser.add_argument("--outlier")
            parser.add_argument("--input", required=True)
            args = parser.parse_args()

            input_path = Path(args.input)
            output = Path.cwd() / (input_path.name + "_d")
            output.mkdir(exist_ok=True)
            counter = output / "invocations.txt"
            previous = int(counter.read_text()) if counter.exists() else 0
            counter.write_text(str(previous + 1))
            (output / (input_path.name + ".components")).write_bytes(b"components")
            if previous:
                print("Loaded components")
            print(f"pass={previous + 1}")
            print("synthetic stderr", file=__import__("sys").stderr)
            """
        ).strip()

        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            source = root / "source.txt"
            source.write_bytes(b"$A,1*00\n$B,2*01\n")
            fake_loga = root / "fake_loga.py"
            fake_loga.write_text(fake_loga_source + "\n", encoding="utf-8")
            output = root / "results"
            output.mkdir()
            spec = RunSpec("R01", "standard", source.name, "synthetic")

            manifest = execute_run(
                spec=spec,
                source_path=source,
                experiment_root=output,
                loga_command_prefix=(sys.executable, str(fake_loga)),
            )

            self.assertEqual(manifest["status"], "completed")
            self.assertEqual(len(manifest["passes"]), 2)
            invocation_file = (
                output / "R01" / "work" / "source.txt_d" / "invocations.txt"
            )
            self.assertEqual(invocation_file.read_text(), "2")
            saved = json.loads((output / "R01" / "run.json").read_text())
            self.assertEqual(
                saved["components_file"],
                "R01/work/source.txt_d/source.txt.components",
            )
            for pass_number in (1, 2):
                pass_dir = output / "R01" / "passes" / f"pass-{pass_number:02d}"
                self.assertTrue((pass_dir / "stdout.log").is_file())
                self.assertTrue((pass_dir / "stderr.log").is_file())
                self.assertTrue((pass_dir / "command.json").is_file())
                self.assertTrue((pass_dir / "metadata.json").is_file())


if __name__ == "__main__":
    unittest.main()
