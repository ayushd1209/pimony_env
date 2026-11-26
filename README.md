# RISC-V Host for Asynchronous DRAM-PIM

gem5 (v25.1, RISC-V) connected to PIMony, an LPDDR5X DRAM-PIM timing model, with
four custom RISC-V instructions for asynchronous PIM: `pim.dispatch`, `pim.gemv`,
`pim.wait` and `pim.fence`. The work is on the `pim-isa` branch.

Built on the PIMony experiment setting
([SNU-VLSI-DRAM/pimony_env](https://github.com/SNU-VLSI-DRAM/pimony_env)). Its
original X86 + SPEC CPU 2017 flow is on the `main` branch.

## What is the thesis work in this repo

Most of `gem5/` is the original gem5 simulator (v25.1). The thesis work is only in
these places:

```
experiments/                      thesis results, one folder per section (start here)
docs/diagrams/                    the thesis drawings: sequencer, attention head, encodings
gem5/
  src/arch/riscv/                 the four PIM instructions, tokens, completion state
  src/cpu/  o3/ minor/ simple/    the pipeline changes: launch order, fence barrier, wait
  src/mem/  pimony*, PIMony.py    the link between gem5 and PIMony
  ext/dramsim3/PIMony/src/        the pim.gemv sequencer (memory_system.*)
  configs/pimony/                 run scripts: fs_bert.py (PIM runs), se_bert.py (CPU baseline), test configs
  configs/scratch/progs/          workloads, one folder each (bert/, gpt3/), with their programs and lab notes
  tests/test-progs/               instruction tests, run by configs/pimony/run_validation.sh
  m5out/                          simulation output, not in git (see "Raw simulation results")
```

Everything else in `gem5/` is unchanged gem5. To list every gem5 file the thesis
changed (`258db28f` is the commit that brought in stock gem5 v25.1.0.1):

```
git diff --stat 258db28f HEAD -- gem5/src gem5/ext
```

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
