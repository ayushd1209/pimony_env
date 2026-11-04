# CSR move + PIM register latency — report

Branch `fix/csr-reglat`, from `pim-isa` at `7fa2ee9d`. Not pushed, not merged.

## Plan (written 2026-11-04, before any change)

1. **Baseline binary.** The current `build/RISCV/gem5.opt` (11-03 19:42) is *not* a HEAD build: its
   last compile rebuilt only `cpu/o3/lsq_unit.o` and `cpu/o3/mem_dep_unit.o`, and both sources were
   rewritten again on 11-04 11:46 (content now equals HEAD). So first rebuild HEAD unchanged and keep
   it as `build/RISCV/gem5.base.opt`.
2. **Fix 1.** `CSR_PIMCOMPLETE` 0x801 → 0x5C0 (custom S-mode read/write range). Remove the legacy
   0x800 `CSR_PIMDONE` / `MISCREG_PIMDONE` if nothing uses it. Update every handler source, rebuild the
   binaries with their recorded recipes (first proving each recipe byte-identical on the unchanged
   source), objdump-check the new number.
3. **Privilege test.** gem5 checks CSR bits [9:8] (`formats/standard.isa:370`); small bare-metal test:
   drop to U-mode, write 0x5C0, expect an illegal-instruction trap.
4. **Fix 2.** `pim_reg_latency` (`Param.Latency("0ns")`) added to the register branch of
   `recvTimingReq` only; `--pim-reg-latency` in `fs_bert.py` and `se_bert.py`. Count register
   responses and head-of-line waits without touching `stats.txt`.
5. **Smoke.** `pim_gemv_ladder` (3 jobs) on the new build: handler writes the new CSR, every token
   completes.
6. **Regression (0 ns).** Every thesis run, shortest first; `stats.txt` must equal the existing run
   except `host_*` lines. Any difference is rerun on `gem5.base.opt` to split my change from drift.
7. **Sensitivity (100 ns).** Every thesis PIM run again; total / awake / asleep cycles over matmul
   blocks 1/3/5/7 (simTicks/333, asleep = quiesceCycles) and completions.
8. Results in new folders under `gem5/m5out/csr_reglat/`; nothing existing is overwritten.

Thesis runs (from `GAPS_pim_gemv.md`, `gpt3/GAPS_gpt3.md`, `gpt3/w1_compare/RESULTS.md`):
BERT `bert_pim_g7pt_scrub64` on timing / Minor / O3 / O3\*, fence controls `_nf` (timing, O3) and
`_nfd` (Minor), `bert_disp_scrub64` and `bert_disp_w32_scrub64` (O3); GPT-3 `gpt3_scr64` on
Minor / O3 / O3\*, `_nf` (O3), `_nfd` (Minor, O3), `gpt3_disp_w1_scr64` and `gpt3_disp_w32_scr64`
(O3); head split `gpt3_hs2_{n0,n28,n32,mm,att}` (O3); W1 projection `w1_pim_64` (O3). CPU baselines
(SE) at 0 ns only. Old wall times: BERT 10–21 min, GPT-3 78–139 min per run.
