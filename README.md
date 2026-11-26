# RISC-V Host for Asynchronous DRAM-PIM

gem5 (v25.1, RISC-V) connected to PIMony, an LPDDR5X DRAM-PIM timing model, with
four custom RISC-V instructions for asynchronous PIM: `pim.dispatch`, `pim.gemv`,
`pim.wait` and `pim.fence`. The work is on the `pim-isa` branch.

Built on the PIMony experiment setting
([SNU-VLSI-DRAM/pimony_env](https://github.com/SNU-VLSI-DRAM/pimony_env)). Its
original X86 + SPEC CPU 2017 flow is on the `main` branch.

## Where things are

| What | Where |
|---|---|
| Instruction decoding and execution | `gem5/src/arch/riscv/isa/decoder.isa`, `gem5/src/arch/riscv/isa.cc` |
| CPU pipeline integration (O3, Minor, TimingSimple) | `gem5/src/cpu/o3/`, `gem5/src/cpu/minor/`, `gem5/src/cpu/simple/` |
| gem5 memory object and PIMony wrapper | `gem5/src/mem/pimony.*`, `gem5/src/mem/pimony_wrapper.*`, `gem5/src/mem/PIMony.py` |
| `pim.gemv` sequencer (memory side) | `gem5/ext/dramsim3/PIMony/src/memory_system.*` |
| Simulation scripts | `gem5/configs/pimony/fs_bert.py` (PIM runs, bare-metal full system), `gem5/configs/pimony/se_bert.py` (CPU-only OpenBLAS baseline) |
| Workloads, their binaries, build and run lines | `gem5/configs/scratch/progs/`, start with its `README.md` |
| Results, each with the command that reproduces it | `gem5/configs/scratch/progs/RESULTS_pim_gemv.md` |
| Instruction tests | `gem5/tests/test-progs/`, all run by `gem5/configs/pimony/run_validation.sh` |
| Thesis figures and the scripts that draw them | `docs/diagrams/` (`gen_*_v2.py`) |

## Setup

gem5 is built and run inside a Docker container that mounts the repo at
`/home/pimony`. If the repo has moved to another GitHub account, replace
`ayushd1209` in the commands below with that account's name.

```
git clone --recursive -b pim-isa https://github.com/ayushd1209/pimony_env.git
cd pimony_env
docker build --network=host -t gem5_env .
docker run -it --user root --privileged --name gem5-container -v $(pwd):/home/pimony gem5_env
```

Inside the container, build PIMony (this gives `libdramsim3.so`), then gem5:

```
git config --global --add safe.directory '*'
cd /home/pimony/gem5/ext/dramsim3/PIMony
git submodule update --init --recursive
mkdir -p build && cd build
conan install .. -s build_type=Release --build=missing
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)

cd /home/pimony/gem5
scons build/RISCV/gem5.opt -j$(nproc)
```

## Running

Run from the gem5 directory and always filter stdout: PIMony prints every DRAM
command, several GB per GPT-3 run. The CPU model argument is `timing`, `minor`
or `o3`.

PIM run, GPT-3 decode layer on O3CPU:

```
docker exec -w /home/pimony/gem5 gem5-container sh -c "d=m5out/gpt3_scr64_o3; mkdir -p \$d && build/RISCV/gem5.opt -d \$d configs/pimony/fs_bert.py o3 configs/scratch/progs/gpt3/gpt3_scr64 2>&1 | grep -a -E 'workload|layer|checksum|Exiting|panic|fatal' > \$d/run.log"
```

CPU-only OpenBLAS baseline for the same layer:

```
docker exec -w /home/pimony/gem5 gem5-container sh -c "d=m5out/gpt3_blas_scrub_o3; mkdir -p \$d && build/RISCV/gem5.opt -d \$d configs/pimony/se_bert.py o3 configs/scratch/progs/gpt3/gpt3_blas_scrub 2>&1 | grep -a -E 'workload|layer|checksum|Exiting|panic|fatal' > \$d/run.log"
```

All instruction tests on all three CPU models:

```
docker exec -w /home/pimony/gem5 gem5-container bash configs/pimony/run_validation.sh
```

## Raw simulation results (`gem5/m5out`)

The raw gem5 output behind the thesis numbers (394 run folders, each with
`stats.txt`, `config.ini` and `run.log`) is not in git because of its size.
It is stored as one file in the GitHub release `m5out-2026-11-26`. To restore
it, run these from the folder that contains `pimony_env`:

```
gh release download m5out-2026-11-26 --repo ayushd1209/pimony_env
tar -xzf m5out_results_2026-11-26.tar.gz -C pimony_env/gem5
```

## Citation

PIMony: **PIMony: A DRAM-PIM Design for Harmonizing PIM and Memory Accesses with
Minimal Interference**, DAC '26, July 2026, Long Beach, CA, USA.
