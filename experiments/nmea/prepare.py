#!/usr/bin/env python3
"""Prepare the NTNU NMEA traces used by the Loga experiment."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
from typing import Mapping


TRACE_FILES = (
    "Arch1-Normal-UDP-Trial1-LargeShip.txt",
    "Arch1-Normal-UDP-Trial1-SmallShip.txt",
    "ModifyingDTM-1.txt",
    "ModifyingROT-1.txt",
    "Normal+DropROT+ModifyRPM.txt",
    "SpoofPosition.txt",
)


def split_records(raw: bytes) -> list[bytes]:
    """Expand escaped separators and split physical lines."""
    expanded = raw.replace(b"\\r\\n", b"\n")
    expanded = expanded.replace(b"\r\n", b"\n").replace(b"\r", b"\n")
    return [record for record in expanded.split(b"\n") if record]


def prepare_content(raw: bytes, *, retain_prefix: bool = False) -> bytes:
    """Return one validated, LF-terminated NMEA record per line."""
    records = split_records(raw)
    if not records:
        raise ValueError("input contains no NMEA records")

    prepared: list[bytes] = []
    for number, record in enumerate(records, start=1):
        marker = record.find(b"$")
        if marker < 0:
            raise ValueError(f"record {number} contains no NMEA sentence")

        output_record = record if retain_prefix else record[marker:]
        if output_record.count(b"$") != 1:
            raise ValueError(f"record {number} contains multiple NMEA sentences")
        prepared.append(output_record)

    if len(prepared) != raw.count(b"$"):
        raise ValueError("input and output sentence counts differ")
    return b"\n".join(prepared) + b"\n"


def prepare_outputs(source_dir: Path, output_dir: Path) -> dict[Path, bytes]:
    """Compute every standard output and the prefix-retained comparison."""
    outputs: dict[Path, bytes] = {}
    for name in TRACE_FILES:
        raw = (source_dir / name).read_bytes()
        outputs[output_dir / "standard" / name] = prepare_content(raw)

    spoof = (source_dir / "SpoofPosition.txt").read_bytes()
    outputs[output_dir / "prefix-retained" / "SpoofPosition.txt"] = (
        prepare_content(spoof, retain_prefix=True)
    )
    return outputs


def write_outputs(outputs: Mapping[Path, bytes]) -> None:
    """Write computed outputs, creating their parent directories."""
    for path, content in outputs.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)


def mismatched_outputs(outputs: Mapping[Path, bytes]) -> list[Path]:
    """Return missing or stale output paths without changing them."""
    return [
        path
        for path, expected in outputs.items()
        if not path.is_file() or path.read_bytes() != expected
    ]


def describe(path: Path, content: bytes) -> str:
    records = content.count(b"\n")
    digest = hashlib.sha256(content).hexdigest()[:12]
    return f"{path}: {records} records, sha256 {digest}"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source-dir",
        type=Path,
        required=True,
        help="directory containing the six supplied source traces",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        required=True,
        help="directory in which to create standard/ and prefix-retained/",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="verify existing outputs without changing them",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not args.source_dir.is_dir():
        raise SystemExit(f"source directory does not exist: {args.source_dir}")

    try:
        outputs = prepare_outputs(args.source_dir, args.output_dir)
    except (OSError, ValueError) as error:
        raise SystemExit(f"preparation failed: {error}") from error

    if args.check:
        mismatches = set(mismatched_outputs(outputs))
        for path, expected in outputs.items():
            status = "FAIL" if path in mismatches else "OK  "
            print(f"{status} {describe(path, expected)}")
        return 1 if mismatches else 0

    write_outputs(outputs)
    for path, content in outputs.items():
        print(f"WROTE {describe(path, content)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
