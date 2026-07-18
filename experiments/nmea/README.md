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

`message-assignments.csv` records each input message's final component,
associated template and whether the row contributed to the alignment that
produced that template.

`templates.csv` reports component membership, LOF exclusions, subsequent
alignment exclusions, total exclusions and the exact contributor count for
every template. `summary.json` records the same values by component and in
total. Historical Loga output without contributor row identifiers remains
readable, but its exact contributor fields are reported as `unknown`.

## 4. Apply the templates

Use the raw second-pass output as the template source:

```sh
build/loga match \
  --templates "/path/to/results/R01/passes/pass-02/stdout.log" \
  --input "/path/to/unseen-messages.log" \
  --output "/path/to/matches.jsonl"
```

Each JSON line contains the source line number, normalised message and every
matching final component template. The same command can be applied to the
training input to audit the retained-contributor guarantee.

## Data handling

The runner copies prepared messages and representative examples into the result tree. Keep source data and result directories outside this public repository unless their redistribution has been authorised.
