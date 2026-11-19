# Repository map for `pim-isa`

_Audited 18 November 2026. This is a navigation guide and a cleanup plan, not a claim that every build has been reproduced._ 

## What belongs to this project

This is a gem5 v25-based RISC-V host model connected to PIMony (LPDDR5X DRAM-PIM). The thesis-specific functionality is custom PIM launch/wait/fence instructions, token-based completion, a gem5-to-PIMony memory interface, and a memory-side GEMV sequencer. Much of `gem5/` is **upstream simulator infrastructure**, not disposable clutter. Do not delete upstream directories to make the thesis changes look smaller.

### 1. Implementation source — **KEEP**

| Area | Main entry points | Why it exists |
| --- | --- | --- |
| RISC-V instruction decoding | `gem5/src/arch/riscv/isa/decoder.isa` and RISC-V ISA support files | Custom `pim.dispatch`, `pim.gemv`, `pim.wait`, and `pim.fence` encodings and behavior |
| Token/completion architectural state | `gem5/src/arch/riscv/isa.cc`, `isa.hh`, RISC-V interrupts code | Completion scoreboard, token tracking and interrupt path |
| Out-of-order and in-order integration | `gem5/src/cpu/o3/`, `gem5/src/cpu/minor/`, `gem5/src/cpu/simple/`, `gem5/src/cpu/StaticInstFlags.py` | Ordering, non-speculative launch issue, cache-fence barriers, wait behavior and squash handling |
| PIM memory object and wrapper | `gem5/src/mem/pimony.cc`, `pimony.hh`, `pimony_wrapper.cc`, `pimony_wrapper.hh`, `PIMony.py` | Translates gem5 PIM requests, exchanges commands and completions with PIMony |
| Memory-side command scheduling | `gem5/ext/dramsim3/PIMony/src/memory_system.cc`, `memory_system.h`, and related PIMony sources | GEMV descriptor handling and DRAM command sequencing |
| Third-party simulator files | The remainder of `gem5/` and `gem5/ext/dramsim3/PIMony/` | Required base simulator and PIMony dependencies; not all are thesis modifications |

This table gives **entry points**, not an exhaustive list of changed source files. For the exact changes, compare against pinned upstream gem5/PIMony revisions, not just this repository's `main` branch: that branch also differs in gem5 version.

### 2. Simulator configurations — **KEEP, DOCUMENT WHAT IS CURRENT**

- `gem5/configs/pimony/fs_bert.py`: full-system BERT/PIM-layer simulations.
- `gem5/configs/pimony/se_bert.py`: syscall-emulation CPU baseline.
- `gem5/configs/pimony/fs_pim_gemv.py`, `fs_pim_issue.py`, `fs_pim_group.py`, `fs_fence_*.py`: focused validation and experiment configurations.
- `gem5/configs/pimony/pimony_mem*.json`: memory-system configurations. Identify which one belongs to each reported experiment.
- `gem5/configs/scratch/progs/README.md`: existing build/run notes, including important distinctions among program variants. This is not yet a concise canonical reproduction guide.

### 3. Workload source — **KEEP**

- BERT: `gem5/configs/scratch/progs/bert.c`, `bert_scrub64.c`, `bert_blas.c`, `bert_blas_fair_scrub.c` and `bert_bm.ld`. Different files serve bare-metal PIM and Linux/OpenBLAS CPU baseline cases.
- GPT-3: `gem5/configs/scratch/progs/gpt3/gpt3.c`, `gpt3_scrub64.c`, `gpt3_blas_scrub.c`, `gpt3_prefill.c`.
- Direct 64-token W1 comparison: `gem5/configs/scratch/progs/gpt3/w1_compare/`, including its own README, sources, build scripts, and results.
- Focused helpers: `gem5/configs/scratch/progs/engine_gaps.py`, `writeback_wait.py`, and `gpt3/checks/`.

Several program variants are built by selecting macros such as `PIM_GEMV`, `PIM_BY_DISPATCH`, `NO_PIM_FENCE`, `NO_PIM_FENCE_DATA`, `PHASE_STATS`, and `PIM_LAYOUT`. Binary filenames are **not** a substitute for a recorded compiler command, source revision, and configuration.

### 4. Validation and research records — **PRESERVE**

- `gem5/tests/test-progs/pim_dispatch/`, `pim_gemv/`, `fence_e2e/`, `fence_order/`, `fence_baremetal/`, `pim_issue/`, `pim_group/`, `pim_sweep/`: targeted test programs. Keep source and document expected test behavior before removing checked-in binaries.
- `gem5/configs/scratch/progs/RESULTS_pim_gemv.md`: experiment measurements and commands.
- `gem5/configs/scratch/progs/GAPS_pim_gemv.md` and `gpt3/GAPS_gpt3.md`: investigation/history. **Historical notes, not a guaranteed current specification.**
- `gem5/configs/scratch/progs/pimony_baseline/`: comparison patch and logged standalone experiments.
- `docs/diagrams/`: figures, editable Excalidraw source, and generator scripts; export PDFs/PNGs/SVGs may be reproducible from source. Preserve the versions actually referenced by the submitted thesis before pruning.
- `docs/rtl_cross_core_survey.md`: design research notes, not necessarily the CVA6 RTL implementation. The inspected repository primarily contains the gem5/PIMony implementation.

**Important:** `RESULTS_pim_gemv.md` says it was current as of 10 October 2026. Some listed headline values differ from those in the final thesis submitted in November. Reconcile runs, source revisions, and measurement methodology before calling any old result definitive.

### 5. Cleanup candidates — **REVIEW FIRST; DO NOT DELETE BLINDLY**

| Path or class | Why it needs review | Safe next step |
| --- | --- | --- |
| `gem5/configs/scratch/progs/` and `gpt3/` extensionless program files | Many compiled variants are mixed into the source directory | Make a manifest of input source, compiler flags, role, and thesis figure; then move reproducible builds to ignored `build/` or `artifacts/` output paths |
| `gem5/pim_fullboot_ckpt/` | Three tracked checkpoint files total about 104 MB | Find run scripts that require it; if necessary, publish a versioned artifact with SHA-256 and restore instructions before untracking |
| `gem5/configs/pimony/run_pim_test.sh` | ~682 KB generated shell script containing an embedded base64 binary | Replace with source + documented build steps once that test's reproducibility is verified |
| `gpt3/src_history/` and old `*_g6`, `*_g7`, `*_nf` etc. | Historical experiments are hard to distinguish from current runs | Keep as historical evidence, ideally in a tagged archival snapshot or explicitly labeled `archive/` area |
| `gem5/rebuild.sh`, `gem5/run.sh`, `disk-image/` | X86/SPEC-era or placeholder instructions alongside RISCV work | Correct or mark legacy; do not advertise as current thesis build entry points |
| `README.md` | Still gives X86 build, SPEC-era setup and clone instructions for the original repository | Replace with a tested RISC-V quick start after canonical commands are established |
| `.claude/settings.local.json` | Local tool settings are tracked | Review whether it belongs in a public shared repository; don't assume it is sensitive without inspection |

Deleting files from the current tree does **not** remove their bytes from Git history. Don't rewrite shared history as a first cleanup step.

## Recommended cleanup sequence

1. **Freeze a reference.** Create a tag for the current thesis branch HEAD before moving or deleting artifacts. Record the commit SHA in the reproduction notes.
2. **Choose a canonical route.** Identify the exact source/binary/configs for (a) the BERT PIM run, (b) the GPT-3 PIM run, (c) each OpenBLAS baseline, and (d) the validation tests. Do not pick binaries based on suffixes alone.
3. **Make builds repeatable.** Write `scripts/build_workloads.sh` (or a Makefile) with pinned GCC/OpenBLAS versions and flags, separate bare-metal and Linux targets, and build outputs outside tracked source directories.
4. **Write a results manifest.** For every thesis table/figure, record input source, Git revision, build command, sim config, PIMony config, CPU model, stats command, and expected value.
5. **Separate source from artifacts.** Only after verifying rebuilds, untrack generated binaries/checkpoints; ignore future outputs. Keep a downloadable/archive copy where exact binaries matter for reproduction.
6. **Fix the entry points.** Replace the top README's SPEC/X86 instructions with a tested RISC-V/PIMony guide and links to the implementation and experiment map.
7. **Validate before merging.** Run at least smoke tests for dispatch/wait, fence, GEMV, and a short representative BERT/GPT-3 experiment. Any measured difference needs explanation.

## What this document does NOT change

No source files, simulations, checked-in binaries, history, or checkpoint files are deleted or modified by this navigation-only first pass. The next pass should be a separate reviewed PR with a testable manifest, not a broad `rm -rf` cleanup.
