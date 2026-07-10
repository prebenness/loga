# Preliminary NMEA experiment

This directory contains the reproducible workflow for the preliminary Loga study on the supplied NMEA traces. It requires Python 3.9 or newer and uses only the standard library. The data are not included in this repository.

## 1. Prepare the inputs

From the repository root:

```sh
python -m experiments.nmea.prepare \
  --source-dir "/path/to/Ready NMEA Messages" \
  --output-dir "/path/to/prepared-nmea"

python -m experiments.nmea.prepare \
  --source-dir "/path/to/Ready NMEA Messages" \
  --output-dir "/path/to/prepared-nmea" \
  --check
```

The preparation step writes six standard traces and one prefix-retained `SpoofPosition.txt` comparison. It normalises record separators, removes capture prefixes from the standard inputs, and otherwise preserves message order, duplicates, fields and checksums.

## 2. Build and test Loga

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
python -m unittest discover -s experiments/nmea/tests -t . -v
```

The executable is normally `build/loga` for a single-configuration build. Pass the appropriate executable path on other platforms.

## 3. Run the experiment

Run a single smoke case first:

```sh
python -m experiments.nmea.run_preliminary \
  --loga build/loga \
  --prepared-dir "/path/to/prepared-nmea" \
  --output-dir "/path/to/smoke-results" \
  --runs R01

python -m experiments.nmea.collect \
  --results-dir "/path/to/smoke-results"
```

For the complete R01--R09 matrix, omit `--runs R01` and use a new output directory. The runner invokes Loga twice in a separate working directory for every run. It records commands, hashes, timings, software provenance and the deterministic R08 row permutation. Existing output directories are rejected.

The collector writes `message-assignments.csv`, `templates.csv` and `summary.json` for each run, followed by `comparisons.json` for the repeat, reordering and prefix comparisons.

`message-assignments.csv` records each input message's final component membership and the template associated with that component. It does not assert that the template accepts the message. Loga may exclude component members from the multi-message alignment before it constructs the template, and its current output does not preserve unambiguous row identifiers for excluded duplicate messages. The assignment file therefore contains no row-level exclusion flag.

`templates.csv` reports `component_membership_count`, `excluded_from_alignment_count` and `alignment_support_count` for every component template. `summary.json` records the same values by component, together with their run-level totals. These counts distinguish all messages placed in a component from the subset used to infer its template.

## Data handling

The runner copies prepared messages and representative examples into the result tree. Keep source data and result directories outside this public repository unless their redistribution has been authorised.
