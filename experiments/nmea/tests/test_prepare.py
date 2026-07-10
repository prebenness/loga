from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from experiments.nmea.prepare import (
    TRACE_FILES,
    mismatched_outputs,
    prepare_content,
    prepare_outputs,
    split_records,
    write_outputs,
)


class PrepareContentTests(unittest.TestCase):
    def test_split_records_accepts_escaped_and_physical_separators(self) -> None:
        raw = b"$A,1*00\\r\\n$B,2*01\r\n$C,3*02\r$D,4*03\n"

        self.assertEqual(
            split_records(raw),
            [b"$A,1*00", b"$B,2*01", b"$C,3*02", b"$D,4*03"],
        )

    def test_standard_output_removes_prefix_and_preserves_order_and_duplicates(
        self,
    ) -> None:
        raw = b"capture one\t$A,1*00\\r\\ncapture two\t$B,2*01\n$B,2*01\n"

        self.assertEqual(
            prepare_content(raw),
            b"$A,1*00\n$B,2*01\n$B,2*01\n",
        )

    def test_prefix_retained_output_changes_only_record_separators(self) -> None:
        raw = b"capture\t$A,1*00\\r\\n$B,2*01\r\n"

        self.assertEqual(
            prepare_content(raw, retain_prefix=True),
            b"capture\t$A,1*00\n$B,2*01\n",
        )

    def test_invalid_records_are_rejected(self) -> None:
        with self.subTest("empty input"):
            with self.assertRaisesRegex(ValueError, "no NMEA records"):
                prepare_content(b"")
        with self.subTest("missing sentence marker"):
            with self.assertRaisesRegex(ValueError, "contains no NMEA sentence"):
                prepare_content(b"capture only\n")
        with self.subTest("multiple sentence markers"):
            with self.assertRaisesRegex(ValueError, "multiple NMEA sentences"):
                prepare_content(b"$A,1*00$B,2*01\n")


class DirectoryWorkflowTests(unittest.TestCase):
    def test_outputs_are_written_and_checked_from_explicit_directories(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            source_dir = root / "source"
            output_dir = root / "output"
            source_dir.mkdir()

            for index, name in enumerate(TRACE_FILES):
                prefix = b"capture\t" if name == "SpoofPosition.txt" else b""
                content = prefix + f"$T{index},value*00\\r\\n".encode("ascii")
                (source_dir / name).write_bytes(content)

            outputs = prepare_outputs(source_dir, output_dir)
            self.assertEqual(len(outputs), len(TRACE_FILES) + 1)
            self.assertEqual(set(mismatched_outputs(outputs)), set(outputs))

            write_outputs(outputs)
            self.assertEqual(mismatched_outputs(outputs), [])
            self.assertEqual(
                (output_dir / "standard" / "SpoofPosition.txt").read_bytes(),
                b"$T5,value*00\n",
            )
            self.assertEqual(
                (
                    output_dir / "prefix-retained" / "SpoofPosition.txt"
                ).read_bytes(),
                b"capture\t$T5,value*00\n",
            )

            stale_path = output_dir / "standard" / TRACE_FILES[0]
            stale_path.write_bytes(b"stale\n")
            self.assertEqual(mismatched_outputs(outputs), [stale_path])


if __name__ == "__main__":
    unittest.main()
