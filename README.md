# loga

Loga is an experimental tool for extracting log message templates and placeholders from raw log files, 
without requiring structured or labeled training data or extraction rules. It performs 
a pipeline of sequence-alignment–based operations to align log messages and the patterns derived from them.

The reproducible workflow for the preliminary NMEA study is documented in
[`experiments/nmea/README.md`](experiments/nmea/README.md).

> Initial code extracted and refactored from the [`neel/prova`](https://github.com/neel/prova) repository.

> The example log files in logs/ directory are from [`logpai/loghub`](https://github.com/logpai/loghub) repository which are freely available for research or academic work. 


## Table of Contents

- [Usage](#usage)
- [Building](#building)
  - [Dependencies](#dependencies)
  - [Build with system packages (no vcpkg)](#build-with-system-packages-no-vcpkg)
    - [Arch Linux](#arch-linux)
    - [Ubuntu 24.04](#ubuntu-2404)
      - [Install igraph from source](#install-igraph-from-source)
    - [Compile loga from source](#compile-loga-from-source)
  - [Build using vcpkg](#build-using-vcpkg)
    - [Compile](#compile)
    - [Compile (Windows without MinGW)](#compile-windows-without-mingw)
- [Run prebuilt binaries](#run-prebuilt-binaries)


## Usage

loga is a command line application. Only necessary input is the log file. 
It is assumed that the logfile is a sequence of ASCII messages seperated by new line characters.
A terminal that supports ANSI colors is necessary to view the output.

```bash
./loga Logfile.log
```

![Apache Log Demo](https://github.com/user-attachments/assets/baec328f-f0e8-41d1-9701-8ad3c59f44bb)


## Building

This project can be built in two ways:

1. **Using system packages only** (recommended on Linux if your distro provides everything).
2. **Using [vcpkg](https://github.com/microsoft/vcpkg)** to fetch all C/C++ dependencies.

The CMake build itself is the same in both cases; the only difference is how you install the libraries.

---

### Dependencies

Common requirements: CMake, C++20 compiler 

C/C++ libraries:

- Boost (`icl`, `graph`, `asio`, `program_options`)
- Armadillo
- cereal
- igraph
---

### Build with system packages (no vcpkg)

Install dependencies:

#### Arch Linux

```bash
sudo pacman -Syu
sudo pacman -S base-devel cmake git boost cereal igraph
yay -S armadillo
```

#### Ubuntu 24.04

```bash
sudo apt-get update
sudo apt-get install -y build-essential ninja-build cmake tar git zip unzip curl pkg-config libboost-program-options-dev libarmadillo-dev libcereal-dev 
```

##### Install igraph from source

The default version of igraph that comes in Ubuntu 24.04 is too old. Loga requires igraph version 1.0.0. Please uninstall tthe existing igraph (if already installed) and install 1.0.0 version from source. 

```bash
sudo apt-get remove -y libigraph-dev || true
sudo apt-get install -y liblapack-dev libblas-dev
curl -L https://github.com/igraph/igraph/releases/download/1.0.0/igraph-1.0.0.tar.gz -o igraph-1.0.0.tar.gz
tar -xzf igraph-1.0.0.tar.gz
cd igraph-1.0.0
mkdir build && cd build
cmake ..  -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DIGRAPH_ENABLE_R=OFF -DIGRAPH_ENABLE_PYTHON=OFF

cmake --build . -- -j"$(nproc)"
sudo cmake --install .
sudo ldconfig
```

#### Compile loga from source

```bash
git clone https://github.com/neel/loga.git
cd loga
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

### Build using vcpkg

```bash
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
./bootstrap-vcpkg.sh   # Linux or Mac
.\bootstrap-vcpkg.bat  # Windows
```

#### Compile 

```bash
git clone https://github.com/neel/loga.git
cd loga

mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=../../vcpkg/scripts/buildsystems/vcpkg.cmake

cmake --build .
```

#### Compile (Windows without MinGW) 

```bash
cmake -S .. -B . -DCMAKE_BUILD_TYPE=Release "-DCMAKE_TOOLCHAIN_FILE=../../vcpkg/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows-static-md -DVCPKG_HOST_TRIPLET=x64-windows

cmake --build build --config Release --parallel
```


## Run prebuilt binaries 

If you don’t want to build from source, you can download the prebuilt binaries produced by the GitHub Actions CI.

### Download the artifact

1. Go to the repository **Actions** tab.
2. Open the latest successful run of the CI workflow.
3. In the **Artifacts** section, download the one that matches your OS (names may vary), e.g.:
   - `loga-ubuntu-latest-stage`
   - `loga-windows-latest-stage`
   - `loga-macos-latest-stage`

After extracting the ZIP, you should see a `stage/` directory that contains:
- `stage/bin/` (the `loga` executable)
- `stage/lib/` (runtime libraries bundled by CI)
- `stage/deps/` (additional runtime libraries bundled by CI)
- a small launcher script in `stage/`:
  - Linux/macOS: `loga` (shell script)
  - Windows: `loga.cmd` (batch script)

### Run it

#### Linux

```bash
unzip loga-ubuntu-latest-stage.zip
cd stage
./loga ./logs/Apache.log
```

#### MacOS

```bash
unzip loga-macos-latest-stage.zip
cd stage

# If macOS blocks the binary after download/unzip:
xattr -dr com.apple.quarantine .

./loga ./logs/Apache.log
```

#### Windows (PowerShell)

Download and extract loga-windows-latest-stage.zip

```
cd stage
.\loga .\logs\Apache.log
```

> **Note: missing shared library errors (runtime dependencies)**
>
> On some machines, `loga` may fail at runtime due to a missing shared library (e.g., `.so` on Linux, `.dylib` on macOS, or `.dll` on Windows).
> However, some resolved dependencies are stored under `stage/deps/` while preparing the artifact.
>
> **Fix:**
> 1. Find the missing library name (e.g., `libXYZ.so` / `libXYZ.dylib`).
> 2. Check if that file exists in `stage/deps/`.
> 3. Copy it into `stage/lib/`.
> 4. Run again. If another library is reported missing, repeat until it runs cleanly.
>
> **Fix (Windows):**
> - If `loga` fails silently or you don’t get a clear “missing DLL” message, use **Dependencies**:
>   - https://github.com/lucasg/Dependencies
> - Open `stage/bin/loga.exe` and `stage/bin/loga.dll` in Dependencies.
> - Identify the missing `.dll`, then copy it from `stage/deps/` to `stage/lib/`.
> - Retry and repeat until all dependencies are resolved.
