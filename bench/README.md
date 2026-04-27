# LOGA vs Drain — LogHub-2.0 Benchmark

Benchmark harness comparing LOGA against Drain on the LogHub-2.0 evaluation protocol (Khan et al., *A Large-Scale Evaluation for Log Parsing Techniques: How Far Are We?*, ISSTA 2024).

## Prerequisites

Ubuntu 22.04+ (or WSL2). You need the LOGA build dependencies and g++-13:

```bash
sudo apt-get update
sudo apt-get install -y build-essential ninja-build cmake \
    libboost-program-options-dev libarmadillo-dev libcereal-dev \
    liblapack-dev libblas-dev python3-venv g++-13
```

igraph 1.0.0 must be built from source (Ubuntu's packaged version is too old):

```bash
cd /tmp
curl -L https://github.com/igraph/igraph/releases/download/1.0.0/igraph-1.0.0.tar.gz -o igraph-1.0.0.tar.gz
tar -xzf igraph-1.0.0.tar.gz && cd igraph-1.0.0
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
         -DIGRAPH_ENABLE_R=OFF -DIGRAPH_ENABLE_PYTHON=OFF
cmake --build . -j$(nproc)
sudo cmake --install . && sudo ldconfig
```

On Ubuntu 22.04 you may also need to fix the cereal cmake config:

```bash
sudo mkdir -p /usr/lib/x86_64-linux-gnu/cmake
sudo ln -sf /usr/share/cmake/cereal /usr/lib/x86_64-linux-gnu/cmake/cereal
```

## Build LOGA

```bash
cd <repo-root>
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++-13
cmake --build . -j$(nproc)
```

## Set up the Python environment

```bash
cd <repo-root>/bench
git submodule update --init --recursive
python3 -m venv env
source env/bin/activate
pip install -r requirements.txt
pip install chardet deap natsort nltk fastdtw datasketch
```

## Run benchmarks

From the repo root, with the venv activated:

```bash
# Single dataset
python bench/main.py --dataset Apache

# Multiple datasets
python bench/main.py --dataset Apache,Proxifier,Linux,Zookeeper,Mac

# All 14 datasets
python bench/main.py --dataset all

# Re-run a dataset (overwrite cached results)
python bench/main.py --dataset Apache --force

# List available datasets
python bench/main.py --list
```

Results are written to `bench/results/loga_vs_drain.csv` and `bench/results/SUMMARY.md`.

## What the benchmark does

For each requested dataset:

1. Extracts the `Content` column from the LogHub-2.0 ground-truth CSV
2. Runs LOGA (default parameters: K=1, Leiden, threshold=0.9, outlier=1.5) on the content
3. Runs Drain (per-dataset tuned parameters from LogHub-2.0) on the raw log
4. Scores both against ground truth using the canonical LogHub-2.0 evaluator

Metrics (all 0-1, higher is better):
- **GA** — Grouping Accuracy (message-level)
- **FGA** — F1 of Grouping Accuracy (template-level)
- **PA** — Parsing Accuracy (exact template match)
- **FTA** — F1 of Template Accuracy

## Datasets

The 2k samples are bundled via the loghub-2.0 submodule. Full datasets can be downloaded from [Zenodo (record 8275861)](https://zenodo.org/record/8275861) into `bench/data/`; the harness auto-detects them.

## Harness pinned commit

loghub-2.0 submodule: `bench/third_party/loghub-2.0/`
