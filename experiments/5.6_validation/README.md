# 5.6 Validation of the CPU-PIM implementation (Table A.1)

**Instruction tests:** the twelve tests of Table A.1 (`asid`, `csr_priv`, `e2e`,
`fence_cbo`, `gemv`, `group`, `issue`, `order_f`, `order_nf`, `sweep`, `wait`,
`wrongpath`) on TimingSimpleCPU, MinorCPU and O3CPU. The runner is
`gem5/configs/pimony/run_validation.sh`; the test programs are in
`gem5/tests/test-progs/` (built by `gem5/tests/test-progs/build_validation.sh`). The
runner checks each test's exit status, that PIMony receives the same commands and
launches from all three CPUs, and the per-test checks below, and writes `summary.txt`.

**BERT layer:** `bert/bert_pim_g7pt` on O3 with PIMony's transaction lines kept in
`run.log`: the MAC and READRES commands that reach the PIM, and the `pim.wait` sleeps.
Expected: 1,536 MACs for the four attention projections and W1, 576 for W2 (its dot
products split across three operations), one READRES per MAC, one sleep per `pim.gemv` (8).

Not in the kept outputs:
- **The 32 global-buffer fills** (8 `pim.gemv` x 4 channels) quoted from PIMony's
  statistics. This run's log keeps only the MAC and READRES lines, and PIMony's statistics
  file is not stored per run.
- **Figure 5.4**, the O3 timeline of a reload after `pim.fence.inv` (fence response 14
  cycles after commit, L1D miss at +15, memory-controller enqueue at +17). It came from a
  detailed debug trace that is not kept, and its drawing is not in this repo.

## Steps

Run from the repo root.

1. **Run** the tests (36 runs, in parallel inside `gem5-container`) and the BERT layer.
   Existing output folders are skipped, so give a new output folder for a fresh copy
   (`DRY=1` prints only):
   ```
   experiments/5.6_validation/run.sh m5out/rerun
   ```
2. **Numbers**: prints them and writes `validation.csv`.
   ```
   python3 experiments/5.6_validation/parse.py m5out/rerun
   ```
   Without an argument it reads the thesis runs in `gem5/m5out` (`validation_1110`, run
   on 2026-11-10).

## Expected result

What `parse.py` prints for the thesis runs:

```
validation 2026-11-10 12:31:50, gem5: build/RISCV/gem5.opt (2026-11-04 20:29:06)
12 tests x 3 CPUs: 36 pass; commands identical across CPUs in 12 tests
  fence_cbo  inv/flush line DRAM accesses (expect 2/3): 2/3 2/3 2/3
  sweep      O3 launches squashed after taking a token: 0
  wait       pim.wait sleeps, woken-early/already-done (expect 2/0): 2/0 2/0 2/0
  wrongpath  O3 launches squashed after taking a token: 420 (need >= 64)
ALL PASS

BERT layer (bert_pim_g7pt_o3_wlog): MAC 2,112, READRES 2,112, pim.wait sleeps 8  (expected: 1,536 + 576 = 2,112 MACs, one READRES each, one sleep per pim.gemv = 8)
```
