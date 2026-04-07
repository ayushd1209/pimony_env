# PIMony Simulator

PIMony is a cycle-accurate simulator designed to model the coexistence of Processing-In-Memory (PIM) operations and normal memory accesses. This simulator is based on the open-source PIM simulator from Neupims(https://github.com/casys-kaist/NeuPIMs.git) and has been extended to enable seamless integration of both computation and memory access within DRAM.

### System Requirements

conan == 1.59.0

cmake >= 3.22.1

gcc >= 8.3

# Getting Started

## Build

```
$ mkdir build && cd build
$ conan install .. -s build_type=Release --build=missing
$ cmake -DCMAKE_BUILD_TYPE=Release ..
$ make -j
```
## Running the Simulator

PIMony can operate in two modes for accepting normal memory requests:

### 1. Trace-based mode
Provide a memory trace file where each line consists of:
1. **Cycle number**  
2. **Transaction type** (`READ` or `WRITE`)  
3. **Address** (in hexadecimal format)

The trace can be generated using `/trace/RandomTraceGenerator.py` (detailed in the source code).

Run the simulator through the `build/pimony` executable with the following arguments:

#### Argument descriptions
- **`--mem_config`**  
  Memory configuration for various scheduling options (e.g., `pim_first`, `mem_first`) and hardware optimizations (`ASYNC`, `DPSA`).  
  Specify a `.json` file located in `/configs/memory_configs/`.

- **`--model_config`**  
  Model configuration (e.g., `GEMV`, `GPT3`). The transactions for PIM operations are generated in `/src/LLM.cc`.  
  Specify a `.json` file located in `/configs/model_configs/`.

- **`--normal_trace`**  
  Pre-defined trace file of normal memory accesses.

- **`--log_level`** (`off` | `info` | `debug`)  
  Controls the verbosity of simulator logs:
  - `off`: disables logs and shows only the final results.  
  - `info`: shows transaction and command-level operations such as `AddCommand`, `IssueCommand`, and `DoneCommand`.  
    Example:
    ```
    [ReturnDoneRead] cid: 3 clk: 7384 | READ | addr: 0X259F2C5E4
    [IssueCommand] cid: 0 clk: 7385 | PRECHARGE | (address) rank: 1 bankgroup: 0 bank: 2 row: 498 column: -1 addr: 0X207CA0000
    [AddTransaction(WR)] cid: 3 clk: 7386 | WRITE | addr: 0X38884574
    [IssueCommand] cid: 0 clk: 7387 | MAC | (address) rank: 0 bankgroup: 3 bank: -1 row: 498 column: 54 addr: 0X207C80180
    [AddCommand] cid: 0 clk: 7387 | MACINTR | (address) rank: 0 bankgroup: 3 bank: -1 row: 498 column: 54 addr: 0X207C80180
    [AddTransaction(RD)] cid: 1 clk: 7388 | READ | addr: 0X223D1BEAF
    [IssueCommand] cid: 0 clk: 7388 | READRES | (address) rank: 1 bankgroup: 3 bank: -1 row: 498 column: 0 addr: 0X207CA0180
    [IssueCommand] cid: 1 clk: 7388 | MAC | (address) rank: 1 bankgroup: 3 bank: -1 row: 498 column: 0 addr: 0X207CA01A0
    [AddCommand] cid: 1 clk: 7388 | MACINTR | (address) rank: 1 bankgroup: 3 bank: -1 row: 498 column: 0 addr: 0X207CA01A0
    [AddCommand] cid: 1 clk: 7388 | READ | (address) rank: 0 bankgroup: 1 bank: 3 row: 2292 column: 31 addr: 0X223D1BEAF
    ```
  - `debug`: prints detailed per-bank states for each channel/rank.  
    Example:
    ```
    [IssueCommand] cid: 2 clk: 511 | ACTIVATE | (address) rank: 0 bankgroup: 2 bank: 3 row: 21333 column: 17 addr: 0X14D55A34B

    ==== PIM States (ch:2) ====
    [rank 0] 
    ===============
    [BankGroup 0] CLOSED CLOSED   OPEN CLOSED |     -1     -1  13586     -1 
    [BankGroup 1]   OPEN   OPEN   OPEN   OPEN |  17767  17767  17767  17767 
    [BankGroup 2]   OPEN   OPEN   OPEN   OPEN |  17767  17767  17767  21333 
    [BankGroup 3] CLOSED   OPEN   OPEN   OPEN |     -1  17767  17767  17767 
    ===============
    ```

- **`--log_dir`**  
  Directory where results and logs will be stored.

You can automate the execution using the provided script:
```
$ ./run.sh
```


### 2. Dynamic insertion / callback mode (e.g., with gem5 interface)

In this mode, the top-level simulator dynamically inserts memory requests (READ/WRITE) and registers callbacks to detect when each operation (READ/WRITE/PIM) is completed.
The interface is defined in /src/memory_system.h

## Baselines

1. pim_first: Prioritizes PIM tile operations over normal memory accesses (READ/WRITE).

2. mem_first: Prioritizes normal memory accesses over PIM tile operations.

3. ASYNC: Asynchronization inter bankgroups with interrupted base commands to prevent stalling across all banks.

4. DPSA: Leverage Dual Path Subarray Access to reduce DRAM resource contention between PIM and normal operations.

5. balanced: Uses MEM_FIRST scheduling with a PIM threshold, allowing more PIM operations to complete before yielding to normal memory accesses. Provides a middle ground between pim_first and mem_first.

6. PIMony: Leverages both ASYNC and DPSA
