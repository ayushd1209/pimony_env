# Figure choice for the sequence-length evaluation

Reviewed 2026-10-26. This is a targeted review of primary inference and PIM papers,
not an exhaustive survey of all LLM papers. Figure numbers below refer to the
specific linked versions.

## Recommendation for the measurements available

Use two aligned empirical panels: cycles per processed token, followed by
multiply-adds per byte read from DRAM. Vary prefill prompt length on a logarithmic
horizontal axis. Show the single batch-one decode measurement as an explicitly
fixed reference, with its 256-token KV cache stated in the caption.

This pairs the observed performance change with evidence of increased reuse.
Read bandwidth and IPC remain useful supporting numbers in the text. The earlier
reuse/bandwidth pair is valid, but does not directly display the performance
result. Neither version measures a PIM speedup crossover.

The revised draft is in `diagrams/seqlen_performance.excalidraw`, with PDF, SVG,
PNG, and a LaTeX caption alongside it. The earlier figure is retained separately.

## Primary-source precedents

| Paper and location | What it actually shows | Application here |
|---|---|---|
| [SARATHI, 2023 preprint, Figure 4, PDF p.4](https://www.microsoft.com/en-us/research/uploads/prod/2023/09/sarathi_preprint-6507c9fc0ce1b.pdf#page=4) | Throughput above and operator arithmetic intensity below, distinguishing prefill and decode. Its Figure 3 shows per-token operator costs. | Closest precedent for pairing performance and reuse rather than requiring a roofline. Its sweep also varies request batch size, unlike the present fixed-batch experiment. |
| [Sarathi-Serve, OSDI 2024, Figures 5–6, PDF p.7](https://www.usenix.org/system/files/osdi24-agrawal.pdf#page=7) | Intensity versus tokens in a batch, followed by linear-layer invocation time versus tokens. | Connects an intensity argument to observed performance. Its hardware-specific bottleneck threshold cannot be copied onto this O3CPU. |
| [DistServe, OSDI 2024, Figure 3, PDF p.6](https://www.usenix.org/system/files/osdi24-zhong-yinmin.pdf#page=6) | Separate prefill and decode throughput-versus-batch plots, with input-length curves. | Makes prompt length and request batch size distinct variables. |
| [Splitwise, ISCA 2024, Figures 5–6](https://www.microsoft.com/en-us/research/wp-content/uploads/2023/12/Splitwise_ISCA24.pdf) | Prompt/decode latency characterization and phase-specific throughput curves. | Ordinary performance sweeps are a well-established alternative to roofline plots. |
| [NeuPIMs, ASPLOS 2024, Figure 4, PDF p.3](https://jongse-park.github.io/files/paper/2024-asplos-neupims.pdf#page=3) | A genuine roofline: performance against arithmetic intensity, with hardware bounds and operators from the two phases. | Suitable precedent if matched hardware ceilings and traffic counts are added. |
| [AttAcc, ASPLOS 2024, Figure 3, PDF p.4](https://scale.snu.ac.kr/papers/2024-04-Conference-ASPLOS-AttAcc.pdf#page=4) | A roofline for summarization and first generation, across batch sizes. FC intensity increases with batching while generation attention remains low-intensity. | Whole-layer reuse does not establish that every operator loses its PIM opportunity. Figure 4 also uses ordinary throughput and cost breakdowns. |
| [PAPI, 2025, Figures 2 and 4, PDF pp.3–4](https://arxiv.org/pdf/2502.15470#page=3) | Rooflines with stated A100 ceilings, followed by FC latency relative to A100 for PIM architectures across batch sizes and speculation lengths. | Strongest direct example for a claim about when PIM loses an advantage: it compares PIM and conventional execution, beyond characterizing the conventional processor. |

Two additional primary examples considered:

- [Efficiently Scaling Transformer Inference, MLSys 2023](https://proceedings.mlsys.org/paper_files/paper/2023/file/c4be71ab8d24cdfb45e3d06dbfca2780-Paper-mlsys2023.pdf): Figure 1 uses phase-specific cost/latency frontiers; Figure B.1 sweeps prefill length at batch one. Appendix C discusses assumptions in roofline traffic accounting.
- [PIM-GPT, linked arXiv version](https://arxiv.org/pdf/2310.09385): uses computation/parameter comparisons, speedup bars, latency breakdowns, and movement/energy results rather than a roofline in this version. Its generation workload includes many successive tokens.

## Precisely what one-token decode means

Standard autoregressive decode processes one newly supplied token per sequence
per iteration. A batch of B requests processes B token positions together. That
does not make each request's context length one. [ORCA, abstract and section 2](https://www.usenix.org/system/files/osdi22-yu.pdf)
describes the per-request iteration and batching semantics.

In this experiment, the decode input has one new token and 256 previously cached
tokens; attention uses 257 positions. Prefill processes one prompt of S tokens
with no inherited KV cache. Both experiments have request batch size one.

Thus, prefill prompt length S, request batch size B, and cached context length C
are separate quantities. Increasing B can reuse weights during decode as well;
increasing C adds attention work and KV traffic. A longer context alone does not
turn ordinary decode into an S-token prefill.

The universal word "always" also excludes valid alternatives: [Leviathan et al.,
ICML 2023](https://proceedings.mlr.press/v202/leviathan23a.html) introduce speculative
decoding, which can process several candidate tokens in parallel.

## Why the current plot is not a roofline

The [original Roofline model, section 3 and Figure 1](https://www2.eecs.berkeley.edu/Pubs/TechRpts/2008/EECS-2008-134.pdf)
plots operations per DRAM byte against operation rate, bounded by

\[
P \leq \min(P_{\rm compute}, B_{\rm memory} I).
\]

Its compute ceiling comes from specifications or microbenchmarks, and its memory
bound uses sustainable bandwidth. The traffic is measured after cache filtering.

Application to this experiment:

- The existing plot puts tokens on the horizontal axis. It is an empirical sweep.
- The available intensity counts only reads. A conventional total-traffic roofline
  needs a matching read-plus-write count, or an explicitly justified read-only
  model and bandwidth ceiling. Writebacks are substantial at larger prompts.
- MAC counts cover the matrix multiplications. A whole-layer FLOP model would
  also need a clear convention for the remaining operations, or explicitly use a
  useful-MAC rate whose ceiling is defined consistently.
- One multiply-add is conventionally two FLOPs. Changing that unit does not fix
  omitted traffic or introduce the missing hardware limits.
- FP16 weight storage does not, by itself, establish an FP16 compute ceiling.
  The actual instructions and precision must match the simulated CPU bound.
- The observed 17.9 GB/s decode bandwidth is not automatically the machine ceiling.

The table can produce achieved MAC throughput without new simulations:
`(MACs / read byte) × (read GB/s) = GMAC/s`. From the rounded table values this is
8.77 for decode and 5.26, 17.43, 40.16, 49.97, 52.22, 54.71 for prefill lengths
4, 16, 64, 128, 256, 512. These are measured-work rates, not compute ceilings.
Cycles per token is the more direct presentation because it uses the recorded
timing without relying on an operation-count convention.

## Boundaries on the thesis conclusion

The available runs demonstrate higher work per DRAM read byte and much lower
cycles per processed token for longer prefills. They support reduced opportunity
to accelerate weight fetching. Low average read bandwidth alone does not prove
compute saturation, and these CPU-only runs do not locate a PIM crossover at
64 tokens. A lower bandwidth can also reflect kernel overhead or other stalls.

The per-token prefill and decode costs are phase-specific costs, not interchangeable
time-per-generated-token serving metrics. No comparison should be labelled as a
measured PIM speedup.

A more directly supported section title is "Sequence Length and Weight Reuse".
A claim that PIM stops helping at a particular prompt length would need PIM
comparisons at those lengths or a separately validated predictive model.

## Local measurement provenance

The supplied table matches `gem5/configs/scratch/progs/gpt3/GAPS_gpt3.md`,
"PREFILL vs DECODE — CPU-only token sweep", including its definitions and
reproduction commands. That record specifies one layer, batch one, FP16 weights,
default O3CPU, and a 2 MiB L2. It counts reads using 64-byte ReadSharedReq
transactions at the memory bus and records dirty writebacks separately. Prefill
computes the full score matrices before causal masking; this is not a fused
attention implementation. The explanatory cache-capacity hypothesis at long
prompts remains untraced.
