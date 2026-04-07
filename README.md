
# PIMony Experiment Setting
PIMony experiment setting for integrating the gem5 simulator with PIMony (a DRAMSim3-based PIM simulator) to provide dynamic memory requests.  
The code is based on the open-source CPU simulator [gem5](https://github.com/gem5) with minimal revisions for integrating PIMony and reproducing the results presented in the paper.


The `PIMony` module should be placed in: /gem5/ext/dramsim3
You can also run the standalone PIMony simulator located at [PIMony](./gem5/ext/dramsim3/PIMony/) and follow the [instructions](./gem5/ext/dramsim3/PIMony/README.md) in here.

This setup assumes that the **CPU2017-1.\*.\*.iso** image (available for purchase from [SPEC CPU2017](https://www.spec.org/cpu2017/)) is properly installed and configured in your environment.

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

## SPEC CPU 2017 License
SPEC CPU 2017 must be **purchased separately** from [spec.org](https://www.spec.org/cpu2017/). The ISO file (`cpu2017-1.1.9.iso`) is not included in this repository.

# Getting Started

## 1. Clone the Repository

```
git clone --recursive https://github.com/SNU-VLSI-DRAM/pimony_env.git
cd pimony_env
```

## 2. Set Docker Environment

```
docker build --network=host -t pimony-env .

docker run -it --user root --privileged \
--name pimony-container \
-v .:/home/spec-2017 pimony-env

```

## 3. PIMony Build

```
git config --global --add safe.directory /home/spec-2017

cd gem5/ext/dramsim3/PIMony
mkdir build && cd build
conan install .. -s build_type=Release --build=missing
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j

```

Now you will get the libdramsim3.so file for gem5 connection.

## 4. Disk-Image Build

```
cd /home/spec-2017/gem5/util/m5
scons build/x86/out/m5
```

Take the benchmark ISO file from the host to your Docker space.
```
# In the host terminal
docker cp <path of the iso file> \
pimony-container:/home/spec-2017/disk-image/spec-2017

```
※ Version pinning
Fix the exact version of `cpu2017-1.*.*.iso` manually in `spec-2017.json` and `install-spec2017.sh`.
The current default is `cpu2017-1.1.9`.

```
cd /home/spec-2017/disk-image
./build.sh
```
※ This step takes a few hours.

※ Only if the ISO download repeatedly fails during this step:
Download the Ubuntu ISO on the host and copy it into the container, then point spec-2017.json to the local file.
If you don’t need this workaround, skip to the 5. Build gem5 step.

```
#Download directly on host
wget -c http://old-releases.ubuntu.com/releases/18.04.2/ubuntu-18.04.2-server-amd64.iso
docker cp ubuntu-18.04.2-server-amd64.iso pimony-container:/home/spec-2017/disk-image/spec-2017/  

# In spec-2017.json, revision:
# "iso_url": "http://old-releases.ubuntu.com/releases/18.04.2/ubuntu-18.04.2-server-amd64.iso"
# --> "iso_url": "file:///home/spec-2017/disk-image/spec-2017/ubuntu-18.04.2-server-amd64.iso"

#Keep the rest the same:

cd /home/spec-2017/disk-image
./build.sh

```

## 5. Build gem5

```
cd /home/spec-2017/gem5
scons build/X86/gem5.opt -j$(nproc) PROTOCOL=MESI_Three_Level
```

Or simply use rebuild.sh.

## 6. Running the gem5 simulator

The running part can be separated into two parts: saving checkpoints & co-running gem5 CPU with PIMony.  

You can automatically run the entire flow through `run.sh` with sweeping options.

### Saving Checkpoints
Running CPU benchmarks with **KVM cpu_type** (default: 20 checkpoints):

```bash
./build/X86/gem5.opt -d <ckpt_out_dir> \
configs/example/gem5_library/x86-spec-cpu2017-checkpoint_<device_type>.py \
--image "../disk-image/spec-2017/spec-2017-image/spec-2017" \
--partition 1 \
--benchmark <cpu_benchmark> \
--size <SIZE>
```

`ckpt_out_dir` : path for saving checkpoint

`device_type` : refers to the platform for the simulator. Valid choices are `mobile` and `laptop`.

`cpu_benchmark` : provided in the SPEC CPU2017 Benchmark Suite.

`SIZE` : refers to the workload size to simulate. Valid choices for --size are `test`, `train`, and `ref`.

### Co-running gem5 CPU with PIMony
Running the CPU benchmarks with Out-of-Order core, co-running PIMony which executes PIM operations.

```
./build/X86/gem5.opt -d <run_dir> \
configs/example/gem5_library/x86-spec-cpu2017-benchmarks_<device_type>.py \
--mem_config <mem_cfg> \
--model_config <model_cfg> \
--log_level <log_level> \
--image "../disk-image/spec-2017/spec-2017-image/spec-2017" \
--partition 1 \
--benchmark <cpu_benchmark> \
--size <SIZE> \
--checkpoint <ckpt>
```
`run_dir` : path for saving simulator results

`mem_cfg` : memory configuration. Details in [--mem_config](./gem5/ext/dramsim3/PIMony/README.md)

`model_cfg` : model configuration. Details in [--model_config](./gem5/ext/dramsim3/PIMony/README.md)

`log_level` : controls the verbosity of simulator logs. Details in [--log_level](./gem5/ext/dramsim3/PIMony/README.md)

`ckpt` : checkpoints generated from the previous step.


# Citation

**PIMony: A DRAM-PIM Design for Harmonizing PIM and Memory Accesses with Minimal Interference**
DAC '26, July 2026, Long Beach, CA, USA

# Project Structure
```text
.
├── README.md
├── disk-image/                     # Packer-based Ubuntu & SPEC2017 disk image builder
│   ├── build.sh                    # Build entry script
│   ├── packer/                     
│   ├── shared/                     
│   │   ├── preseed.cfg             
│   │   └── serial-getty@.service   
│   └── spec-2017/                  # SPEC CPU2017 image build resources
│       ├── cpu2017-1.1.9.iso       # (Manually downloaded) SPEC2017 ISO
│       ├── install-spec2017.sh     
│       ├── post-installation.sh    
│       ├── runscript.sh            
│       ├── spec-2017-image/
│       │   └── spec-2017           # Unpacked or mounted SPEC image
│       ├── spec-2017.json          # Packer configuration for SPEC2017
│       └── ubuntu-18.04.2-server-amd64.iso  
│
└── gem5/                           # gem5 + DRAMSim3 + PIMony integration
    ├── build/                      # Compiled gem5 binaries
    ├── configs/
    │   └── example/
    │       └── gem5_library/       # Python modules for checkpoints & running configs
    ├── ext/
    │   └── dramsim3/
    │       └── PIMony              # Custom DRAM-PIM simulator
    ├── rebuild.sh                  
    └── run.sh                      # Automated sweeping runner
```
