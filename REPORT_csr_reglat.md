# CSR move and PIM register latency — report

Branch `fix/csr-reglat`, from `pim-isa` at `7fa2ee9d`. Not pushed, not merged.
Results: `gem5/m5out/csr_reglat/` (`lat0/`, `lat100/`, `smoke/`, `hol2/` = reruns with the refined
counter). Nothing existing was overwritten.

## In short

1. **The completion CSR is now 0x5C0**, the first number of the custom *supervisor* read/write range.
   A U-mode write now traps as an illegal instruction (test passes). On the old build the same
   U-mode write went through and planted a forged completion in the scoreboard.
2. **0x800 is removed.** Nothing read or wrote it: no source, no binary.
3. **At 0 ns every rerun is identical to the existing result** (`stats.txt` line for line except the
   `host*` lines; `config.ini` differs only by the new `pim_reg_latency=0`).
4. **At 100 ns the cost grows with the number of completions.** BERT pim.gemv (8 completions):
   +0.2 % to +4.8 %. BERT pim.dispatch, 32 then wait (66): +19.5 %. One wait per command (2,112):
   +62.6 %. GPT-3 pim.gemv (25): +0.6 % to +2.5 %; pim.dispatch 32 then wait (680): +18.9 %; one
   wait per command (21,760): +48.1 %. Each completion pays three register round trips (mask read,
   ASID read, W1C write).
5. **Head-of-line blocking happens, but rarely and by little.** At 100 ns: 0–5 DRAM responses
   held back per BERT run, 0–16 per GPT-3 run, none on the Minor and timing CPUs. Their total wait
   is at most 3,247 cycles in a whole run, at most 0.5 % of that run's matmul cycles. A control
   that polls the register window during a heavy DRAM stream shows it can be large (1,401 held
   back).

## Commits

| commit | what |
|---|---|
| `3a21f0b2` | this report's plan |
| `e70f62a9` | CSR 0x801 → 0x5C0; 0x800 removed (`misc.hh`, `isa.cc`) |
| `1d131d1e` | 13 handler sources write 0x5C0; 34 tracked binaries rebuilt |
| `efe099cb` | `tests/test-progs/csr_priv`: U-mode write must trap |
| `40182e33` | `pim_reg_latency` parameter; `--pim-reg-latency` in `fs_bert.py`, `se_bert.py` |
| `0a0fcba2` | counters: completions, register responses, early and held-back responses, per stats dump |
| `cc4b65b5` | held-back counter refined: charged up to the register response's due tick; bus retries no longer hide it |
| `fda73f5d` | `tests/test-progs/reg_hol`: positive control for the held-back counter |
| `37a51cfc`, then the last commit | this report: results, then the GPT-3 held-back counts |

## Fix 1 — the CSR

**Why 0x5C0.** The privileged spec reserves 0x5C0–0x5FF for custom supervisor read/write CSRs:
bits [11:10] = 01 (read/write), [9:8] = 01 (lowest privilege S), [7:6] = 11 (custom). gem5 defines
nothing in 0x500–0x5FF. gem5 checks bits [9:8] on every CSR access
(`gem5/src/arch/riscv/isa/formats/standard.isa:370`, `pm < lowestAllowedMode` → illegal
instruction), so the range alone closes the hole; no extra check was needed. The handler runs in
S-mode, so it may write it. The entry carries `isaExtsFlags('s')` like the other S-mode CSRs.

**0x800.** Uses found: only gem5's own definition (`CSR_PIMDONE`, its `CSRData` entry,
`MISCREG_PIMDONE`, its name and read/write cases in `isa.cc`). No source writes it (grep) and none
of the 194 ELF files under `configs/scratch/progs` and `tests/test-progs` touches it (objdump).
Removed. The only checkpoint, `pim_fullboot_ckpt` (06-29), already could not be restored: it holds
193 misc registers, the build had 195 (now 194).

**Handlers and binaries.** All 13 handler sources changed `csrw 0x801` → `csrw 0x5c0`. Before the
edit each binary below was rebuilt from the unchanged source with its recorded recipe and matched
the binary that ran (byte-identical; test programs by loaded image, because `start.S` records a
random `cc*.o` name). After the edit, every one of them differs from the old one in exactly
2 bytes: the CSR field of its single `csrw` (`80179073` → `5c079073`). Example:
`riscv64-unknown-elf-objdump -d gem5/configs/scratch/progs/bert_pim_g7pt_scrub64 | grep 0x5c0`
→ `800002a6: 5c079073 csrw 0x5c0,a5`.

- Rebuilt, tracked (34): `bert_pim_g7pt{,_nf,_nfd}`, `bert_pim_g7pt_scrub`,
  `bert_pim_g7pt_scrub64{,_nf,_nfd}`, `bert_disp_scrub64`, `bert_disp_w32_scrub64`, `gpt3_pt`,
  `gpt3_scr0`, `gpt3_scr1`, `gpt3_att1`, `gpt3_att1b`, `gpt3_disp_w1`, `gpt3_disp_w32`,
  `gpt3_hs2_{n0,n28,n32,mm,att}`, `gpt3_scr64{,_nf,_nfd}`, `gpt3_disp_w{1,32}_scr64`,
  `pim_gemv_ladder`, `attn_context`, `attn_scores`, `pim_gemv_g6`, `fence_e2e`, `pim_group`,
  `pim_sweep`, `pim_baremetal`.
- Rebuilt, git-ignored (8): `gpt3_hs2_n16`, `gpt3_hs2_n32_bk`, `w1_pim_{1,64,check}{,_fast}`.
  Their old versions are not in git, but they rebuild byte-identical from `pim-isa`'s sources.
- **Not rebuilt (68)**: no recipe reproduces them from the current sources (they came from older
  source revisions). They still contain `csrw 0x801`. On the new gem5 such a binary takes an
  illegal-instruction fault in its handler and then **hangs silently** (tried with `iss_16`,
  `m5out/csr_reglat/smoke/stale_iss16_newgem5`). List at the end.

**Privilege test** (`tests/test-progs/csr_priv`, `fs_bert.py timing`, `--debug-flags=Faults,RiscvMisc`).
M-mode writes the CSR (allowed), then `mret` to U-mode writes it again.

| gem5 | binary | result |
|---|---|---|
| new | `csr_priv` (0x5C0) | M-mode write accepted; U-mode write → `Fault (Illegal instruction, 2)` at the U-mode `csrw` (0x8000002c); `m5_exit` = pass |
| new | `csr_priv_801` | 0x801 no longer exists: already the M-mode write traps; `m5_fail` |
| old (`gem5.base.opt`) | `csr_priv_801` | **U-mode write accepted** (`Writing 0x10003 to CSR pimcomplete` twice): the forged completion; `m5_fail` |
| old | `csr_priv` | 0x5C0 does not exist there; `m5_fail` |

## Fix 2 — register latency

`pim_reg_latency = Param.Latency("0ns", ...)` in `gem5/src/mem/PIMony.py`, added in the register
branch of `recvTimingReq` only (`pimony.cc`, `Tick time = curTick() + headerDelay + payloadDelay +
pimRegLatency`). `recvAtomic` and `recvFunctional` unchanged. `fs_bert.py` and `se_bert.py` take
`--pim-reg-latency=<lat>` (default `0ns`); it is removed from `argv` before the positional
arguments are read, so every existing command line still works.

## Smoke test

`pim_gemv_ladder` (3 jobs), `fs_bert.py o3`, `--debug-flags=RiscvMisc,Faults`
(`m5out/csr_reglat/smoke/ladder_{0ns,100ns}_o3/run.log`): three interrupts, the handler writes
`0x10000`, `0x10001`, `0x10002` to `pimcomplete`, all three `pim.wait` return, `m5_exit`.
Exit tick 5,071,923 at 0 ns, 5,715,612 at 100 ns. From interrupt to CSR write: 118,548 ticks at
0 ns, 319,014 at 100 ns, i.e. +200 ns = the two register reads before the `csrw`.

## Regression — 0 ns vs the existing runs

Compared: `stats.txt` without `host*` lines, number of stats blocks, exit tick, `config.ini`,
MAC lines in `run.log` for dispatch runs. Minor runs are compared with both `_minor_fix` (HEAD
gem5) and `_minor` (10-22); those two were already identical.

| run | result |
|---|---|
| `bert_pim_g7pt_scrub64_timing`, `_nf_timing` | identical |
| `bert_pim_g7pt_scrub64_minor`, `_nfd_minor` (vs `_fix` and plain) | identical |
| `bert_pim_g7pt_scrub64_o3`, `_nf_o3`, `_o3v61` | identical |
| `bert_disp_scrub64_o3`, `bert_disp_w32_scrub64_o3` | identical (MAC lines 6,336 / 6,336) |
| `bert_blas_fair_scrub_minor`, `_o3`, `_o3v61` (CPU baselines) | identical |
| `gpt3_scr64_minor`, `_nfd_minor` (vs `_fix` and plain) | identical |
| `gpt3_scr64_o3`, `_nf_o3`, `_nfd_o3`, `_o3v61` | identical |
| `gpt3_hs2_n0_o3`, `_n28_o3`, `_n32_o3`, `_mm_o3`, `_att_o3` | identical |
| `gpt3_disp_w1_scr64_o3`, `gpt3_disp_w32_scr64_o3` | identical (MAC lines 65,280 / 65,280) |
| `w1_compare_pim_64_o3`, `w1_compare_cpu_64_o3` | identical |
| `gpt3_blas_scrub_minor`, `_o3`, `_o3v61` (CPU baselines) | identical |

30 runs, 34 comparisons, all identical. Only other difference: `config.ini` gains
`pim_reg_latency=0` under `[system.mem_ctrl]`.

The refined counter (`cc4b65b5`) changes no simulated behaviour either: rebuilt, it reproduces
`bert_pim_g7pt_scrub64_o3` at 0 ns (vs the existing run) and all 9 BERT and 14 GPT-3 PIM runs at
100 ns (vs `lat100/`), every non-`host` line (`m5out/csr_reglat/hol2/`).

## Sensitivity — 0 ns vs 100 ns

Matmul blocks 1/3/5/7, cycles = simTicks/333, asleep = `quiesceCycles` (TimingSimple:
`numIdleCycles`), awake = cycles − asleep. Completions = `pimComplete()` calls in those blocks /
in the whole run.

| run | total 0 ns | total 100 ns | Δ | awake 0 ns | awake 100 ns | Δ | asleep 0 ns | asleep 100 ns | Δ | completions |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `bert_pim_g7pt_scrub64_timing` | 874,552 | 876,093 | +0.18% | 779,190 | 784,712 | +0.71% | 95,362 | 91,381 | −4.17% | 8 / 8 |
| `bert_pim_g7pt_scrub64_nf_timing` | 807,633 | 815,441 | +0.97% | 716,689 | 723,082 | +0.89% | 90,944 | 92,359 | +1.56% | 8 / 8 |
| `bert_pim_g7pt_scrub64_minor` | 375,816 | 378,805 | +0.80% | 280,177 | 290,151 | +3.56% | 95,639 | 88,654 | −7.30% | 8 / 8 |
| `bert_pim_g7pt_scrub64_nfd_minor` | 343,591 | 349,168 | +1.62% | 250,724 | 256,722 | +2.39% | 92,867 | 92,446 | −0.45% | 8 / 8 |
| `bert_pim_g7pt_scrub64_o3` | 236,107 | 241,589 | +2.32% | 144,669 | 150,482 | +4.02% | 91,438 | 91,107 | −0.36% | 8 / 8 |
| `bert_pim_g7pt_scrub64_nf_o3` | 181,067 | 189,687 | +4.76% | 87,778 | 93,699 | +6.75% | 93,289 | 95,988 | +2.89% | 8 / 8 |
| `bert_pim_g7pt_scrub64_o3v61` | 235,686 | 241,924 | +2.65% | 143,828 | 148,188 | +3.03% | 91,858 | 93,736 | +2.04% | 8 / 8 |
| `bert_disp_w32_scrub64_o3` | 318,229 | 380,369 | +19.53% | 258,586 | 319,718 | +23.64% | 59,643 | 60,651 | +1.69% | 66 / 66 |
| `bert_disp_scrub64_o3` | 2,912,639 | 4,735,520 | +62.59% | 1,072,861 | 2,979,863 | +177.75% | 1,839,778 | 1,755,657 | −4.57% | 2,112 / 2,112 |
| `bert_blas_fair_scrub_o3` (baseline) | 2,291,189 | 2,291,189 | 0 | — | — | — | 0 | 0 | — | 0 (0 register accesses; whole `stats.txt` identical) |
| `gpt3_scr64_minor` | 2,285,603 | 2,308,361 | +1.00% | 1,332,653 | 1,347,320 | +1.10% | 952,950 | 961,041 | +0.85% | 25 / 25 |
| `gpt3_scr64_nfd_minor` | 2,162,209 | 2,175,363 | +0.61% | 1,198,196 | 1,216,805 | +1.55% | 964,013 | 958,558 | −0.57% | 25 / 25 |
| `gpt3_scr64_o3` | 1,584,037 | 1,611,765 | +1.75% | 624,054 | 645,968 | +3.51% | 959,983 | 965,797 | +0.61% | 25 / 25 |
| `gpt3_scr64_nf_o3` | 1,308,908 | 1,338,954 | +2.30% | 349,964 | 375,499 | +7.30% | 958,944 | 963,455 | +0.47% | 25 / 25 |
| `gpt3_scr64_nfd_o3` | 1,313,755 | 1,346,185 | +2.47% | 352,668 | 377,418 | +7.02% | 961,087 | 968,767 | +0.80% | 25 / 25 |
| `gpt3_scr64_o3v61` | 1,586,830 | 1,616,427 | +1.87% | 627,147 | 649,049 | +3.49% | 959,683 | 967,378 | +0.80% | 25 / 25 |
| `gpt3_disp_w32_scr64_o3` | 2,844,295 | 3,382,839 | +18.93% | 2,113,933 | 2,704,319 | +27.93% | 730,362 | 678,520 | −7.10% | 680 / 680 |
| `gpt3_disp_w1_scr64_o3` | 34,327,451 | 50,840,503 | +48.10% | 13,704,491 | 32,591,193 | +137.81% | 20,622,960 | 18,249,310 | −11.51% | 21,760 / 21,760 |
| `gpt3_hs2_n0_o3` matmul | 1,613,918 | 1,632,020 | +1.12% | 647,531 | 672,040 | +3.78% | 966,387 | 959,980 | −0.66% | 25 / 25 |
| `gpt3_hs2_n0_o3` layer (blocks 1–8) | 3,594,981 | 3,608,462 | +0.37% | 2,628,594 | 2,648,482 | +0.76% | 966,387 | 959,980 | −0.66% | 25 / 25 |
| `gpt3_hs2_n0_o3` attention (block 2) | 1,934,445 | 1,929,859 | −0.24% | 1,934,445 | 1,929,859 | −0.24% | 0 | 0 | — | 0 / 25 |
| `gpt3_hs2_mm_o3` matmul | 1,548,697 | 1,583,250 | +2.23% | 645,860 | 669,950 | +3.73% | 902,837 | 913,300 | +1.16% | 25 / 25 |
| `gpt3_hs2_mm_o3` layer | 3,546,063 | 3,566,507 | +0.58% | 2,643,226 | 2,653,207 | +0.38% | 902,837 | 913,300 | +1.16% | 25 / 25 |
| `gpt3_hs2_mm_o3` attention | 1,951,539 | 1,938,350 | −0.68% | 1,951,539 | 1,938,350 | −0.68% | 0 | 0 | — | 0 / 25 |
| `gpt3_hs2_att_o3` matmul | 2,918,604 | 2,943,005 | +0.84% | 2,059,379 | 2,076,750 | +0.84% | 859,225 | 866,255 | +0.82% | 25 / 25 |
| `gpt3_hs2_att_o3` layer | 3,535,938 | 3,559,352 | +0.66% | 2,676,713 | 2,693,097 | +0.61% | 859,225 | 866,255 | +0.82% | 25 / 25 |
| `gpt3_hs2_att_o3` attention | 572,340 | 571,365 | −0.17% | 572,340 | 571,365 | −0.17% | 0 | 0 | — | 0 / 25 |
| `gpt3_hs2_n28_o3` matmul | 1,608,278 | 1,641,120 | +2.04% | 647,774 | 671,356 | +3.64% | 960,504 | 969,764 | +0.96% | 25 / 81 |
| `gpt3_hs2_n28_o3` layer | 2,875,654 | 2,951,690 | +2.64% | 1,831,989 | 1,904,401 | +3.95% | 1,043,665 | 1,047,289 | +0.35% | 81 / 81 |
| `gpt3_hs2_n28_o3` attention | 1,222,930 | 1,263,532 | +3.32% | 1,139,769 | 1,186,007 | +4.06% | 83,161 | 77,525 | −6.78% | 56 / 81 |
| `gpt3_hs2_n32_o3` matmul | 1,605,368 | 1,632,214 | +1.67% | 645,356 | 666,833 | +3.33% | 960,012 | 965,381 | +0.56% | 25 / 89 |
| `gpt3_hs2_n32_o3` layer | 2,724,040 | 2,803,655 | +2.92% | 1,668,561 | 1,746,473 | +4.67% | 1,055,479 | 1,057,182 | +0.16% | 89 / 89 |
| `gpt3_hs2_n32_o3` attention | 1,073,518 | 1,125,386 | +4.83% | 978,051 | 1,033,585 | +5.68% | 95,467 | 91,801 | −3.84% | 64 / 89 |
| `w1_compare_pim_64_o3` block 1 (64 tokens) | 31,128,759 | 31,311,981 | +0.59% | 9,934,876 | 10,069,775 | +1.36% | 21,193,883 | 21,242,206 | +0.23% | 192 / 192 |
| `gpt3_blas_scrub_*`, `w1_compare_cpu_64_o3` (baselines) | not rerun at 100 ns: no register accesses, and the BERT baseline at 100 ns matched in every line | | | | | | | | | |

What this does to the thesis numbers (matmul blocks; baselines unchanged by construction):

| quantity | 0 ns (= quoted) | 100 ns |
|---|---:|---:|
| BERT speedup, Minor / O3 / O3\* | 13.93x / 9.70x / 11.76x | 13.82x / 9.48x / 11.46x |
| BERT pim.dispatch ÷ pim.gemv, 32 then wait / 1 then wait | 1.35x / 12.34x | 1.57x / 19.60x |
| BERT fence cost, timing / Minor / O3 | 66,919 (7.7%) / 32,225 (8.6%) / 55,040 (23.3%) | 60,652 (6.9%) / 29,637 (7.8%) / 51,902 (21.5%) |
| GPT-3 speedup, Minor / O3 / O3\* | 28.13x / 15.94x / 18.94x | 27.85x / 15.66x / 18.59x |
| GPT-3 PIM run, Minor ÷ O3 ("only 1.4 times faster") | 1.44x | 1.43x |
| awake share of the O3 PIM run, BERT / GPT-3 ("61 %", "39 %") | 61.3% / 39.4% | 62.3% / 40.1% |
| GPT-3 O3 PIM run: total (÷ PIMony alone, 0.71 M) / awake / asleep vs same jobs (0.94 M) | 1.58 M (2.2x) / 0.62 M / +2.1% | 1.61 M (2.3x) / 0.65 M / +2.7% |
| GPT-3 pim.dispatch ÷ pim.gemv, 32 then wait / 1 then wait | 1.80x / 21.67x | 2.10x / 31.54x |
| GPT-3 one wait per command, asleep / awake (eval line 288) | 20.62 M / 13.70 M | 18.25 M / 32.59 M |
| GPT-3 fence cost, O3 / O3 control that keeps descriptor cleans / Minor | 275,129 (17.4%, 55 per fence) / 270,282 (17.1%) / 123,394 (5.4%, 25 per fence) | 272,811 (16.9%, 55) / 265,580 (16.5%) / 132,998 (5.8%, 27) |
| GPT-3 O3 fences in awake time | 0.28 of 0.62 M | 0.27 of 0.65 M |
| overlap 1, QKV + attention (`hs2_mm` vs `hs2_n0`) | −1.69% | −1.72% |
| overlap 2 on top (`hs2_att` vs `hs2_mm`) | −0.94% | −0.61% |
| attention share of the layer (`hs2_n0`) | 53.8% | 53.5% |
| all 32 heads on the PIM (`hs2_n32` vs `hs2_n0`), attention / layer | −44.5% / −24.2% | −41.7% / −22.3% |
| CPU asleep during attention, 32 PIM heads | 8.9% | 8.2% |
| W1, PIM cycles per token (speedup vs CPU 1,692,194) | 486,387 (3.48x) | 489,250 (3.46x) |

Fence counts (the `nf` control drops 4,985 fences, the `nfd` control 4,960) do not change with the
latency. The PIMony-alone figures (0.71 M, 0.94 M) are the rounded values the thesis quotes. Not
recomputed: the 2–3 % L2-miss slowdown (eval line 344), which needs per-miss latency stats.

How to read it:
- **Where the cost lands.** Per completion the dispatch runs pay +863 cycles (one wait per
  command) and +941 (32 then wait): about 3 × 300 cycles, one 100 ns round trip for the mask
  read, the ASID read and the W1C write each. That shows up as awake time.
- **GPT-3 pays the same per completion.** Awake +868 cycles per completion in both dispatch runs
  (BERT: +903, +926). The asleep part shrinks by 109 and 76 per completion, so the total grows by
  759 (one wait per command) and 792 (32 then wait).
- **Why few-completion runs move by noise-sized amounts.** With 8 completions the awake part grows
  by 4,360–9,974 cycles, while the asleep part moves by −6,985 to +2,699 in either direction.
  *Inferred, not checked:* the PIM jobs start a few hundred ns later at 100 ns and meet DRAM
  refresh and bank state at different moments, so the job times shift. Per block, one O3 block
  even gets 465 cycles faster (`bert_pim_g7pt_scrub64_o3`, block 3).
- **Fence cost** is a difference of two runs, so it inherits that noise; at 100 ns it moves
  −5.7 % (O3) to −9.4 % (timing). That is not a change in what a fence costs.

## Head-of-line: can a delayed register response hold back DRAM responses?

**In the model, yes.** Register responses, DRAM read/write responses and launch acks share one
FIFO, `responseQueue`, drained by one event (`pimony.cc`, `sendResponse`). The event is scheduled
for the due tick of whichever response finds the queue empty, and after each send the next
response goes out in the same tick. So a register response that finds the queue empty holds the
event until its delayed due tick, and any DRAM response that arrives meanwhile waits behind it.
The reverse also happens: a register response that finds the queue non-empty leaves with the
responses ahead of it and **skips** most of its latency.

**In the runs, rarely, and by little.** A response counts as held back when it was due before the
register response at the head of the queue; it is charged only its wait up to that register
response's due tick (`cc4b65b5`). Whole run, 100 ns (`m5out/csr_reglat/hol2/lat100/`):

| run | completions | register responses | held back | total wait (cycles) |
|---|---:|---:|---:|---:|
| `bert_pim_g7pt_scrub64_timing`, `_nf_timing` | 8 | 24 | 0 | 0 |
| `bert_pim_g7pt_scrub64_minor`, `_nfd_minor` | 8 | 24 | 0 | 0 |
| `bert_pim_g7pt_scrub64_o3` | 8 | 24 | 1 | 233 |
| `bert_pim_g7pt_scrub64_nf_o3` | 8 | 24 | 4 | 883 |
| `bert_pim_g7pt_scrub64_o3v61` | 8 | 24 | 1 | 233 |
| `bert_disp_w32_scrub64_o3` | 66 | 198 | 5 | 1,116 |
| `bert_disp_scrub64_o3` | 2,112 | 6,336 | 3 | 553 |
| `gpt3_scr64_minor`, `_nfd_minor` | 25 | 75 | 0 | 0 |
| `gpt3_scr64_o3` | 25 | 75 | 14 | 2,741 |
| `gpt3_scr64_nf_o3` | 25 | 75 | 15 | 3,127 |
| `gpt3_scr64_nfd_o3` | 25 | 75 | 12 | 2,442 |
| `gpt3_scr64_o3v61` | 25 | 75 | 14 | 2,784 |
| `gpt3_hs2_n0_o3` | 25 | 75 | 11 | 2,197 |
| `gpt3_hs2_mm_o3` | 25 | 75 | 8 | 1,707 |
| `gpt3_hs2_att_o3` | 25 | 75 | 9 | 1,996 |
| `gpt3_hs2_n28_o3` | 81 | 243 | 16 | 3,247 |
| `gpt3_hs2_n32_o3` | 89 | 267 | 16 | 3,171 |
| `gpt3_disp_w32_scr64_o3` | 680 | 2,040 | 2 | 471 |
| `gpt3_disp_w1_scr64_o3` | 21,760 | 65,280 | 2 | 417 |
| `w1_compare_pim_64_o3` | 192 | 576 | 2 | 413 |

At 0 ns, measured with the refined counter on `bert_pim_g7pt_scrub64_o3` and the control: 0 held
back. The total wait is delay to DRAM responses, an upper bound on what the core loses. Its
largest share of a run's matmul cycles: 883 of 189,687 (0.5 %, `bert_pim_g7pt_scrub64_nf_o3`);
for GPT-3, 3,127 of 1,338,954 (0.2 %, `gpt3_scr64_nf_o3`). Only O3 runs show any, and the count
does not grow with completions: 2 in 21,760 (`gpt3_disp_w1_scr64_o3`), 8–15 in 25 (the GPT-3
pim.gemv O3 runs). Which requests were held back is not traced. Register responses sent early at
100 ns: one, in `gpt3_hs2_att_o3`, which skipped the full 100 ns; every other register response
waited the full 100 ns. At 0 ns: at most one per run, 1–2 cycles early. Register responses:
exactly 3 per completion.

The first counter (`0a0fcba2`) showed 0 held back in all of these runs: a bus retry early in such
a burst reset its flag.

**Control** (`tests/test-progs/reg_hol`, O3, `m5out/csr_reglat/hol2/ctl/`):

| phase | 0 ns | 100 ns |
|---|---|---|
| 1: 8 completions; the handler runs while loads stream | 0 held back | 0 held back |
| 2: `PIM_DONE` read every 64 lines while loads stream (2,048 reads) | 0 held back; 442 reads early | **1,401 held back, 84 ns each on average**; 1,950 reads early (98 ns each on average) |

So blocking shows up when register reads mix with a heavy DRAM stream. The first counter saw 0
in phase 2 as well (memory-bus response layer about 70 % busy there, so retries were common;
0–10 % in the thesis runs).

## Mentions outside the code

Thesis (Overleaf zip `Master_Thesis_Ayush (21).zip`, 11-02):
- `1_mainmatter/5_implementation.tex:113` — "a custom CSR (\texttt{0x801})": → 0x5C0, and it is
  now an S-mode CSR that U-mode cannot write.
- `1_mainmatter/5_implementation.tex:111` — register window "accessed as uncacheable memory": no
  access latency stated; the model adds `pim_reg_latency` (0 ns in all quoted runs).
- `1_mainmatter/6_evaluation.tex`, Table 6.1 (`tab:eval:config`, lines 9–66): no register-latency
  row.
- `1_mainmatter/6_evaluation.tex:288` — "Each of the 21,760 commands also brings its own completion
  interrupt and trap handler, so the awake part grows to 13.70 million": grows further with
  register latency (at 100 ns: awake 32.59 million, asleep 18.25 million).
- `1_mainmatter/4_design.tex:67` — "an interrupt and a trap handler ... take longer than the MAC
  itself": holds more strongly with a register latency.

Repo (diagram sources and docs; figure PDFs not touched):
- `docs/diagrams/pimony_system.excalidraw:542,556` — "pimdone CSR 0x800 / csrw: OR mask →
  scoreboard" (that CSR is gone); `:1047`, `:1115` — register block and membus, no timing.
- `docs/diagrams/completion_overhead.excalidraw:1786` and `docs/diagrams/gen_completion_overhead.py:15,172-173`
  — "MMIO read → bitmask scan → MMIO read → MMIO ack", handler = 1,361 cyc (old 1 GHz measurement).
- `docs/diagrams/pim_completion_datapath.excalidraw:158`, `pim_dispatch_mac_model.excalidraw:2723` —
  scoreboard / "trap_handler → pim.wait wakes", no CSR number.
- `gem5/configs/scratch/progs/GAPS_pim_gemv.md:1256` — `PIM_DONE` description, no CSR number.
- `~/obsidian-notes/laptop-extras/claude-memory/project_mmio_completion_mvp.md` — old notes copy,
  says `csrw 0x800` / `0x801`.

Checked, no mention: `~/thesis-report` (slides), `~/cva6` `pim-offload` (no completion CSR in the
RTL yet; when `pim.wait` is ported, use 0x5C0).

## Binaries still containing `csrw 0x801` (68, not rebuilt)

progs: `bert_pim`, `bert_pim_a16`, `bert_pim_a32`, `bert_pim_flush`, `bert_pim_g6`,
`bert_pim_g6_a16`, `bert_pim_g6_a32`, `bert_pim_g6w`, `bert_pim_g7`, `bert_pim_g7_nf`,
`bert_pim_g7pt_a16`, `bert_pim_g7pt_a32`, `bert_pim_phase`, `bert_pim_zfh`, `bert_disp`,
`bert_disp_w32`, `bert_disp_w192`; gpt3: `gpt3_att`, `gpt3_dec`, `gpt3_dec_b`, `gpt3_dec_c`,
`gpt3_dec_ov`, `gpt3_hs_{n0,n28,n28f,n32,mm,att}`, `gpt3_mm_ctl`, `gpt3_mm_ov`, `gpt3_pim`,
`gpt3_rt0`, `gpt3_rt28`, `gpt3_tiny`, `gpt3_tiny_{n0,n3f,n4,mm,att,ckpt}`, `gpt3_v2_ctl`,
`gpt3_v2_ov`; test-progs: `pim_gemv/{pim_gemv, pim_gemv16, pim_gemv_1e64, pim_gemv_1e65,
pim_gemv_b64, pim_gemv_b65, pim_gemv_clean, pim_gemv_cur, pim_gemv_desc, pim_gemv_e1, pim_gemv_e4,
pim_gemv_e8, pim_gemv_e16, pim_gemv_e32, pim_gemv_w82, gemv_32, gemv_64, gemv_128, gemv_192}`,
`pim_issue/{pim_issue, pim_issue16, iss_16, iss_32, iss_64, iss_128, iss_192}`.
Note `iss_16` is the default of `fs_pim_issue.py` and `gemv_*` are marked current in their README;
their sources build to different code today, so they need a fresh build and a fresh baseline.

## Other things found

- The `build/RISCV/gem5.opt` on disk at the start (built 11-03 19:42) was **not** a HEAD build: its last
  compile rebuilt only `cpu/o3/lsq_unit.o` and `cpu/o3/mem_dep_unit.o`, whose sources were
  rewritten again on 11-04 11:46 (now equal to HEAD). Rebuilding HEAD recompiled exactly those two
  files. No run in `m5out` was made with that binary (no `config.ini` newer than 11-03 19:42).
  The clean HEAD build is kept as `build/RISCV/gem5.base.opt`; old configs for it are in
  `m5out/csr_reglat/base_configs/` (the edited `fs_bert.py` passes a parameter the old binary
  does not know).

## Reproduce

Run from `~/pimony_env/gem5`. Job lists, the runner and the analysis scripts are saved in
`m5out/csr_reglat/scripts/`. A job line is `gem5|out dir|config|args|MAC lines 0/1`; `run_job.sh`
logs to the path in its `LOG=` line.

- Build: `docker exec -w /home/pimony/gem5 gem5-container scons build/RISCV/gem5.opt -j14`
- Privilege test (expect `Fault (Illegal instruction, 2)` at 0x8000002c, then `m5_exit`; with
  `csr_priv_801` expect `m5_fail`):
  `docker exec -w /home/pimony/gem5 gem5-container build/RISCV/gem5.opt --debug-flags=Faults,RiscvMisc -d m5out/<new dir> configs/pimony/fs_bert.py timing tests/test-progs/csr_priv/csr_priv`
- One thesis run, e.g. BERT O3 at 100 ns:
  `docker exec -w /home/pimony/gem5 gem5-container sh -c "build/RISCV/gem5.opt -d m5out/<new dir> configs/pimony/fs_bert.py o3 configs/scratch/progs/bert_pim_g7pt_scrub64 --pim-reg-latency=100ns 2>&1 | grep -a -E 'PIMREG|Exiting'"`
- Every run of a list: `xargs -P 14 -d '\n' -I{} m5out/csr_reglat/scripts/run_job.sh {} < m5out/csr_reglat/scripts/wave1.txt`
  (also `wave2a.txt`, `wave2b.txt`, `wave2c.txt`; `hol2.txt`, `hol3.txt` for the refined counter)
- One 0 ns rerun vs the existing run:
  `cmp <(grep -v ^host m5out/<run>/stats.txt) <(grep -v ^host m5out/csr_reglat/lat0/<run>/stats.txt) && echo same`
- Refined-counter rerun vs the first 100 ns run:
  `cmp <(grep -v ^host m5out/csr_reglat/hol2/lat100/<run>/stats.txt) <(grep -v ^host m5out/csr_reglat/lat100/<run>/stats.txt) && echo same`
- Regression table: `python3 m5out/csr_reglat/scripts/regress.py`
- Sensitivity table and register counts: `python3 m5out/csr_reglat/scripts/tables.py`
- GPT-3 thesis quantities: `python3 m5out/csr_reglat/scripts/gq.py`
- Held back in one run (count, cycles):
  `awk '/^PIMREG/{h+=$15; t+=$17} END{print h, t/333}' m5out/csr_reglat/hol2/lat100/<run>/run.log`
- Handler uses the new CSR: `riscv64-unknown-elf-objdump -d <binary> | grep -c 'csrw\s*0x5c0'`
- Rebuild the 42 binaries in place: `bash m5out/csr_reglat/scripts/rebuild.sh` (`progs` come out
  byte-identical; test-prog ELFs load the same image but can differ in a few symbol-table bytes)

## Plan as written at the start (12:05)

1. Baseline binary: rebuild HEAD unchanged, keep as `build/RISCV/gem5.base.opt`.
2. Fix 1: 0x801 → 0x5C0; remove 0x800 if unused; update handlers; rebuild binaries with proven
   recipes; objdump check.
3. Privilege test: U-mode write to 0x5C0 must trap.
4. Fix 2: `pim_reg_latency` in the register branch of `recvTimingReq`; `--pim-reg-latency`;
   count register responses and head-of-line waits without touching `stats.txt`.
5. Smoke: `pim_gemv_ladder`.
6. Regression at 0 ns, shortest runs first; differences rerun on `gem5.base.opt`.
7. Sensitivity at 100 ns.
8. Results in new folders under `gem5/m5out/csr_reglat/`.
