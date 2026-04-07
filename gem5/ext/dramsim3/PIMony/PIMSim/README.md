# PIM Simulator

This simulator is based on DRAMsim3.

### PIM command


- `COMP` : Execute a single MAC operation in the adder-tree-based MAC unit. The operation is broadcast to **all banks** within a channel.
- `MAC` : Execute continuous MAC operations until the end of an open row or until a `MACINTR` command is received. The operation is broadcast to **all banks** within the designated **bankgroup**.
- `MACINTR` : Interrupt ongoing MAC operations for specific bankgroups using **bankgroup bitmasks** to identify which groups to halt.
- `READRES`: Read accumulated results from banks.  
  - In **all-bank synchronization** mode: reads from all banks simultaneously.  
  - In **ASYNC** mode: reads from designated bankgroups independently.
- `H2GWRITE`: Write input data into the global buffer from host side. From the DRAM’s point of view, it behaves similarly to a normal `WRITE` command but does **not** require an `ACT` (Activate).
- `D2GWRITE`: Write input data into the global buffer from a **designated bank**. ⚠️ **Warning:** This command is **not used or tested** in the current version of PIMony. It is reserved for **future work** if such functionality becomes necessary.
