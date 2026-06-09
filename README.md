
# PIMony Experiment Setting
PIMony experiment setting for integrating the gem5 simulator with PIMony (a DRAMSim3-based PIM simulator) to provide dynamic memory requests.  
The code is based on the open-source CPU simulator [gem5](https://github.com/gem5) with minimal revisions for integrating PIMony and reproducing the results presented in the paper.


The `PIMony` module should be placed in: /gem5/ext/dramsim3
You can also run the standalone PIMony simulator located at [PIMony](./gem5/ext/dramsim3/PIMony/) and follow the [instructions](./gem5/ext/dramsim3/PIMony/README.md) in here.

> **Branch note (`riscv_cpu`):** SPEC CPU 2017 wiring has been removed on this
> branch. The CPU model is being ported from x86 to RISC-V, and a free
> benchmark suite (e.g. PARSEC, GAP, or NPB) will replace SPEC once the port
> is far enough along. See `main` for the original SPEC 2017 setup.

# Prerequisites

## Host Requirements
- **OS**: Linux only (KVM is required for the checkpoint phase)
- **CPU**: Hardware virtualization support (Intel VT-x or AMD-V), enabled in BIOS/UEFI

## Host Software
- **Docker** (with permission to access `/dev/kvm`)
- **KVM** accessible inside the container (`--privileged` is used for this)

## Inside Docker (handled by Dockerfile)
All build dependencies are installed automatically inside the container:

| Category | Packages |
|----------|----------|
| Build tools | `build-essential`, `scons`, `cmake ≥ 3.12`, `g++` (C++17) |
| gem5 deps | `libprotobuf-dev`, `protobuf-compiler`, `libgoogle-perftools-dev`, `libboost-all-dev`, `zlib1g-dev`, `libglib2.0-dev`, `libpixman-1-dev` |
| Simulation | `qemu`, `qemu-system-x86` |
| Python | `python3`, `pip`, `conan 1.59.0` |
| PIMony C++ deps (via Conan) | `fmt 9.1.0`, `boost 1.79.0`, `spdlog 1.11.0`, `nlohmann_json 3.11.2`, `robin-hood-hashing 3.11.5` |
| Disk-image builder | Packer 1.7.8 (auto-downloaded by `build.sh`) |

## Benchmark suite
TBD — see the branch note above. No paid benchmark license is required on this
branch.

# Getting Started

## 1. Clone the Repository

```
git clone --recursive https://github.com/SNU-VLSI-DRAM/pimony_env.git
cd pimony_env
```

## 2. Set Docker Environment

```
docker build --network=host -t gem5_env .

docker run -it --user root --privileged \
  --name gem5-container \
  -v $(pwd):/home/pimony \
  gem5_env
```

> The Dockerfile currently still creates a user named `spec-2017` (a SPEC-era
> leftover). The bind mount above exposes the repo at `/home/pimony` regardless;
> the username is only cosmetic and will be renamed in a later cleanup pass.

## 3. PIMony Build

```
git config --global --add safe.directory '*'
cd /home/pimony/gem5/ext/dramsim3/PIMony
git submodule update --init --recursive
mkdir -p build && cd build
conan install .. -s build_type=Release --build=missing
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)

```

Now you will get the libdramsim3.so file for gem5 connection.

## 4. m5 helper + Disk Image

### 4a. Build the `m5` guest helper

`m5` is a small binary that runs inside the simulated guest OS and lets the
workload talk back to gem5 (checkpoint, switch CPU model, dump stats,
mark region-of-interest). It is benchmark-agnostic — needed regardless of
which benchmark suite ends up here.

```
cd /home/pimony/gem5/util/m5
scons build/x86/out/m5
```

> When the RISC-V port lands, this becomes `scons build/riscv/out/m5` instead.

### 4b. Disk image — **TBD**

The SPEC CPU 2017 disk-image pipeline has been removed on this branch (see
`disk-image/spec-2017/PLACEHOLDER.md`). A new Packer recipe + install scripts
will live here once the free benchmark suite is chosen and cross-compiled
for the target ISA.

## 5. Build gem5

```
cd /home/pimony/gem5
scons build/X86/gem5.opt -j$(nproc) PROTOCOL=MESI_Three_Level
```

Or simply use rebuild.sh.

## 6. Running the gem5 simulator — **TBD**

The original two-phase flow (fast checkpoint creation, then detailed run with
PIMony co-simulation) lived here. On this branch the wiring is stubbed:

- `gem5/configs/example/gem5_library/x86-spec-cpu2017-checkpoint_*.py` — checkpoint phase, stub (`raise NotImplementedError`)
- `gem5/configs/example/gem5_library/x86-spec-cpu2017-benchmarks_*.py` — detailed-run phase, stub (`raise NotImplementedError`)
- `gem5/run.sh` — sweep orchestration, stub

These will be rewritten once:
1. The CPU is ported to RISC-V.
2. A free benchmark suite is chosen.
3. A new disk image is built for that benchmark (see step 4b).

See the `main` branch for the original SPEC 2017 flow.


# Citation

**PIMony: A DRAM-PIM Design for Harmonizing PIM and Memory Accesses with Minimal Interference**
DAC '26, July 2026, Long Beach, CA, USA

# Project Structure
```text
.
├── README.md
├── disk-image/                     # Disk-image builder (Packer + benchmark install)
│   ├── build.sh                    # STUB on this branch (SPEC pipeline removed)
│   ├── packer/
│   ├── shared/
│   │   ├── preseed.cfg
│   │   └── serial-getty@.service
│   └── spec-2017/                  # Legacy dir name; contents removed
│       └── PLACEHOLDER.md          # Notes on what was here + where it's going
│
└── gem5/                           # gem5 + DRAMSim3 + PIMony integration
    ├── build/                      # Compiled gem5 binaries
    ├── configs/
    │   └── example/
    │       └── gem5_library/       # Python configs — SPEC entries are STUBS on this branch
    ├── ext/
    │   └── dramsim3/
    │       └── PIMony              # Custom DRAM-PIM simulator
    ├── rebuild.sh
    └── run.sh                      # STUB on this branch (was sweep orchestrator)
```
