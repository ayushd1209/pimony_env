# Cross-core survey: does a line clean hold back a later doorbell?

Survey done 2026-10-06, saved here 2026-10-28. It is based only on reading RTL
source code. Nothing was built or simulated.

## The question

This is the PIM fence scenario, with no fence between the steps:

```text
sd    t0, 0(a0)   # a0: cacheable DRAM address -> line becomes dirty in L1 D$
CLEAN a0          # cbo.clean, or the core's own line write-back instruction
sd    t1, 0(a1)   # a1: uncached MMIO address (the doorbell)
```

Can the doorbell reach the device before memory has acknowledged the write-back
of line a0? If it can, which fence closes the gap?

## Answer per core

| Core @ commit | Clean instruction | Can the doorbell overtake the write-back? | How far we checked it |
|---|---|---|---|
| CVA6 @ `81245a4` (HPDcache write-back config) | `cbo.clean` (pim.fence.cl decodes to the same operation) | **No.** An uncached request waits until no write-back is pending. A write-back stays pending until memory's write response arrives. | Our own trace, re-read 2026-10-28 |
| Rocket @ `ece7b9a` | `cflush.d.l1` (optional; cleans and invalidates) | **Yes**, even before the L1 receives `ReleaseAck`. Ordinary fences don't wait for `ReleaseAck` either. | Verified (one link from the report only, see below) |
| BOOM @ `58ef272` | None decoded | **N/A.** The scenario cannot run. | Verified |
| XiangShan @ `09453ff` | `cbo.clean` | **Yes.** The doorbell waits for the cache's CBO acknowledgement, but that can come before external memory's write response (AXI B). | Partly verified: both ends of the chain, not the middle |
| OpenC910 @ `b91c909` | `dcache.cva` / `dcache.cpa` | **Yes.** The clean counts as complete once its data is in the CIU's write buffer, before external memory's write response. | Partly verified: the completion line, not its link to the instruction finishing |

For the thesis, Rocket and BOOM are verified, and XiangShan and OpenC910 are
partially verified. Any claim beyond the lines listed under "What we checked
ourselves" rests on the report alone.

## Why it matters

CVA6 is the only core checked where a plain clean already holds back the
doorbell. That comes from a rule inside its data cache: an uncached request
waits for all pending transactions. It is not an ISA guarantee.

On Rocket, neither the line flush nor a standard fence waits for the
write-back's acknowledgement. So pim.fence.cl is more than a clean plus a fence
folded into one instruction. It promises an ordering that the standard
instructions do not give on every core.

## What we checked ourselves

**CVA6.** Re-read 2026-10-28 in `~/cva6` at CVA6 `81245a4`, with the HPDcache
submodule at `e8f072e`. The flush unit exists in write-back builds
(`HPDcacheCfg.u.wbEn`, `hpdcache.sv:972`). Files are under
`core/cache_subsystem/hpdcache/rtl/src/`.

- `hpdcache_ctrl_pe.sv:571–583`: an uncacheable request is parked in the
  replay table while `st1_no_pend_trans` is 0.
- `hpdcache_ctrl.sv:768–773`: `st1_no_pend_trans` includes `flush_empty_i`.
- `hpdcache_flush.sv:163`: `flush_empty_o` is true only when no flush entry
  is valid.
- `hpdcache_flush.sv:225, 262–265`: an entry is cleared only by `flush_ack`,
  which is the memory write response.
- This is consistent with the directed test on the CVA6 RTL (`~/cva6`, branch
  `pim-ext`). `fence_cl` passes, and fails when the clean is replaced by a nop.

**Rocket.** `src/main/scala/rocket/DCache.scala`, checked 2026-10-06.

- `601–605`: `tl_out_a.valid` for an uncached request
  (`s2_valid_uncached_pending`) has no `release_ack_wait` term. Only the
  cached-miss branch has one.
- `231`: `io.cpu.req.ready` does not include `release_ack_wait`.
- `927`: `io.cpu.ordered` does not include `release_ack_wait`.
- Not re-read: that the fence waits on `ordered`. This comes from the report
  (`RocketCore.scala:407–419, 541–542`).

**BOOM.** `src/main/scala/v4/exu/decode.scala`, checked 2026-10-06.

- `431–438`: the decode table is built from the X, F, FDivSqrt, X64, B and
  RoCC tables. There is no CBO or CFLUSH entry anywhere in the file.

**XiangShan** with OpenNCB `9a83eb7`, checked 2026-10-06.

- `NewStoreQueue.scala:975–1003`: a CBO at the store-queue head waits for the
  D-cache's response (`waitResp`) before it leaves, so the doorbell cannot pass
  it inside the core.
- OpenNCB `NCBUpstreamRXREQ.scala:507–512`: the memory bridge answers a write
  with `CompDBIDResp`, which is completion before AXI B, under these
  parameters:
  - `writeNoError` is set,
  - `writeCompPreferSeperate` is not set,
  - and either EWA or an AXI master order is configured.
- Not checked: that XiangShan's `Top.scala` sets those parameters (the report
  cites `Top.scala:116–127`), and the L2/LLC chain between the two ends.

**OpenC910.** Checked 2026-10-06.

- `ct_ciu_snb_arb.v:2095`: `sab_memw_cmplt = vb_snb_w_grant`. The write-back
  counts as complete when it is handed to the CIU's write buffer (VB), not at
  the external B response.
- Not checked: how that completion reaches the clean instruction finishing,
  and the doorbell's NCQ path.

## Pinned commits

| Repo | Commit |
|---|---|
| openhwgroup/cva6 | `81245a47fad8fe1a5d562d953ef2662e099def76` |
| openhwgroup/cv-hpdcache (CVA6 submodule) | `e8f072e117ed3282950d4d1e975f462b173f0de0` |
| chipsalliance/rocket-chip | `ece7b9ad544b07df39cd13b6fd7236562a26355d` |
| riscv-boom/riscv-boom | `58ef2720eae13be26b3008c02b5a74ce29c61c44` |
| OpenXiangShan/XiangShan | `09453ff8f44e9d2b526e6149c60e1dc3f08c7192` |
| OpenXiangShan/OpenNCB | `9a83eb7e6c0e36e0d6b72642ff93641ec65819cf` |
| OpenXiangShan/XSCache (coupledL2, openLLC) | `300515bc7b2f65ca988041a984204489d80ec2fb` |
| XUANTIE-RV/openc910 | `b91c90914c19f114d35c8f6b73408eb241ed847c` |

## Provenance

- **The report:** written 2026-10-06 by a Codex CLI agent from shallow clones,
  given the prompt in the appendix. Local log:
  `~/.codex/sessions/2026/10/06/rollout-2026-10-06T18-44-25-01a1121a-22d2-7a72-a90c-46e2aa6440bf.jsonl`
  (last message).
- **The spot check:** Claude Code, 2026-10-06. It fetched the five cited files
  at the pinned commits. The CVA6 lines were re-read 2026-10-28.

## The report, word for word

Unedited. Its links point at the pinned commits.

**For your strict memory-acknowledgment criterion: Rocket—Yes; BOOM—no line-clean instruction; XiangShan—Yes in the checked OpenLLC/AXI integration; OpenC910—Yes.** XiangShan does hold the doorbell until `CBOAck`, but that acknowledgment can precede external memory’s AXI B response.

These are configuration-specific RTL results from shallow clones, without building or simulating. Rocket below uses the blocking D-cache with its optional custom flush enabled. XiangShan below uses the in-tree OpenLLC→OpenNCB→AXI path; other downstream integrations remain **Unclear**.

| Core | Line-clean instruction | Doorbell can overtake write-back? | Mechanism (file:line) | Fence that closes it | Confidence |
|---|---|---|---|---|---|
| Rocket @ `ece7b9a` | Optional `cflush.d.l1 a0`; cleans **and invalidates** | **Yes**, even before L1 `ReleaseAck` | Uncached A-channel request bypasses the `release_ack_wait` restriction: [Rocket@ece7b9a src/main/scala/rocket/DCache.scala:601–609](https://github.com/chipsalliance/rocket-chip/blob/ece7b9ad544b07df39cd13b6fd7236562a26355d/src/main/scala/rocket/DCache.scala#L601-L609) | Neither ordinary fence waits for `ReleaseAck`. Whole-cache `cflush.d.l1 x0` closes the **L1-ack gap**, but an end-to-end DRAM drain is unverified. | High for checked D-cache |
| BOOM @ `58ef272` | **None decoded** in v3 or v4 | **Unclear—N/A:** scenario cannot execute | Decode tables omit line-clean operations: [BOOM@58ef272 src/main/scala/v4/exu/decode.scala:431–438](https://github.com/riscv-boom/riscv-boom/blob/58ef2720eae13be26b3008c02b5a74ce29c61c44/src/main/scala/v4/exu/decode.scala#L431-L438) | N/A; stop at missing instruction | High |
| XiangShan @ `09453ff` | `cbo.clean` | **Yes before external AXI B; No before CBOAck** | CBO completion holds the SQ head, but the memory bridge permits early `CompDBIDResp`: [XiangShan@09453ff src/main/scala/xiangshan/mem/lsqueue/NewStoreQueue.scala:975–1003](https://github.com/OpenXiangShan/XiangShan/blob/09453ff8f44e9d2b526e6149c60e1dc3f08c7192/src/main/scala/xiangshan/mem/lsqueue/NewStoreQueue.scala#L975-L1003), [OpenNCB@9a83eb7 src/main/scala/openncb/logical/NCBUpstreamRXREQ.scala:507–512](https://github.com/OpenXiangShan/OpenNCB/blob/9a83eb7e6c0e36e0d6b72642ff93641ec65819cf/src/main/scala/openncb/logical/NCBUpstreamRXREQ.scala#L507-L512) | Neither `fence w,o` nor `fence rw,rw` closes this external-B gap; both drain local stores. No end-to-end drain instruction verified. | High, scoped to OpenLLC/NCB |
| OpenC910 @ `b91c909` | `dcache.cva` / `dcache.cpa`; separate L1-only variants | **Yes** | L2 clean completion follows transfer into CIU’s write buffer, rather than external B: [OpenC910@b91c909 C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_ciu_snb_arb.v:2093–2098](https://github.com/XUANTIE-RV/openc910/blob/b91c90914c19f114d35c8f6b73408eb241ed847c/C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_ciu_snb_arb.v#L2093-L2098) | Custom `sync` drains writes admitted to EBIU. **Full closure remains Unclear** for CIU-buffered writes awaiting EBIU admission; standard-fence sufficiency also unverified. | High on Yes; fence proof incomplete |

Rocket:

- **Instruction and configuration.** `CFLUSH_D_L1` decodes to `M_FLUSH_ALL`; the core encodes nonzero `rs1` as the line form. Decode inclusion depends on `haveCFlush`, whose default is false. [Rocket@ece7b9a src/main/scala/rocket/IDecode.scala:148–156](https://github.com/chipsalliance/rocket-chip/blob/ece7b9ad544b07df39cd13b6fd7236562a26355d/src/main/scala/rocket/IDecode.scala#L148-L156), [Rocket@ece7b9a src/main/scala/rocket/RocketCore.scala:238–239](https://github.com/chipsalliance/rocket-chip/blob/ece7b9ad544b07df39cd13b6fd7236562a26355d/src/main/scala/rocket/RocketCore.scala#L238-L239), [Rocket@ece7b9a src/main/scala/rocket/RocketCore.scala:561–563](https://github.com/chipsalliance/rocket-chip/blob/ece7b9ad544b07df39cd13b6fd7236562a26355d/src/main/scala/rocket/RocketCore.scala#L561-L563), [Rocket@ece7b9a src/main/scala/rocket/RocketCore.scala:42](https://github.com/chipsalliance/rocket-chip/blob/ece7b9ad544b07df39cd13b6fd7236562a26355d/src/main/scala/rocket/RocketCore.scala#L42).
- **Write-back path:** dirty flush emits a data-bearing TileLink Release on C. Its first transfer sets `release_ack_wait`; finishing the data transfers advances to metadata update, and metadata-update acceptance restores `s_ready` independently of the acknowledgment. [Rocket@ece7b9a src/main/scala/rocket/DCache.scala:861–879](https://github.com/chipsalliance/rocket-chip/blob/ece7b9ad544b07df39cd13b6fd7236562a26355d/src/main/scala/rocket/DCache.scala#L861-L879), [Rocket@ece7b9a src/main/scala/rocket/DCache.scala:906–912](https://github.com/chipsalliance/rocket-chip/blob/ece7b9ad544b07df39cd13b6fd7236562a26355d/src/main/scala/rocket/DCache.scala#L906-L912).
- **Line-command completion:** the replayed line flush succeeds when the line no longer hits. It does not test `release_ack_wait`; that flag clears separately upon `ReleaseAck`. [Rocket@ece7b9a src/main/scala/rocket/DCache.scala:439–443](https://github.com/chipsalliance/rocket-chip/blob/ece7b9ad544b07df39cd13b6fd7236562a26355d/src/main/scala/rocket/DCache.scala#L439-L443), [Rocket@ece7b9a src/main/scala/rocket/DCache.scala:705–707](https://github.com/chipsalliance/rocket-chip/blob/ece7b9ad544b07df39cd13b6fd7236562a26355d/src/main/scala/rocket/DCache.scala#L705-L707).
- **Doorbell path:** CPU request readiness requires `s_ready`, but not `!release_ack_wait`. The uncached A-channel branch can issue a Put while that flag remains set; the acknowledgment restriction appears only in the cached-miss branch. Thus C-channel write-back and A-channel MMIO lack the required acknowledgment ordering. [Rocket@ece7b9a src/main/scala/rocket/DCache.scala:231](https://github.com/chipsalliance/rocket-chip/blob/ece7b9ad544b07df39cd13b6fd7236562a26355d/src/main/scala/rocket/DCache.scala#L231), [Rocket@ece7b9a src/main/scala/rocket/DCache.scala:601–609](https://github.com/chipsalliance/rocket-chip/blob/ece7b9ad544b07df39cd13b6fd7236562a26355d/src/main/scala/rocket/DCache.scala#L601-L609).
- **Ordinary fences:** the core holds later memory instructions using `id_reg_fence` and `dmem.ordered`. However, blocking-D$ `ordered` includes pipeline accesses, cached grants and uncached transactions, **excluding pending ReleaseAck**. Consequently neither requested FENCE form closes this gap. [Rocket@ece7b9a src/main/scala/rocket/RocketCore.scala:407–419](https://github.com/chipsalliance/rocket-chip/blob/ece7b9ad544b07df39cd13b6fd7236562a26355d/src/main/scala/rocket/RocketCore.scala#L407-L419), [Rocket@ece7b9a src/main/scala/rocket/RocketCore.scala:541–542](https://github.com/chipsalliance/rocket-chip/blob/ece7b9ad544b07df39cd13b6fd7236562a26355d/src/main/scala/rocket/RocketCore.scala#L541-L542), [Rocket@ece7b9a src/main/scala/rocket/DCache.scala:927](https://github.com/chipsalliance/rocket-chip/blob/ece7b9ad544b07df39cd13b6fd7236562a26355d/src/main/scala/rocket/DCache.scala#L927).
- **Stronger existing operation:** whole-cache `cflush.d.l1 x0` waits for `!release_ack_wait` during scanning and before clearing `flushing`; its completion requires `flushed && !flushing`. This verifies a drain through L1’s ReleaseAck boundary, not an unspecified downstream DRAM acknowledgment. [Rocket@ece7b9a src/main/scala/rocket/DCache.scala:1012–1040](https://github.com/chipsalliance/rocket-chip/blob/ece7b9ad544b07df39cd13b6fd7236562a26355d/src/main/scala/rocket/DCache.scala#L1012-L1040), [Rocket@ece7b9a src/main/scala/rocket/DCache.scala:440](https://github.com/chipsalliance/rocket-chip/blob/ece7b9ad544b07df39cd13b6fd7236562a26355d/src/main/scala/rocket/DCache.scala#L440).

BOOM:

- **It does have a write-back D-cache:** dirty victim handling generates a write-back request and waits for its response. [BOOM@58ef272 src/main/scala/v4/lsu/mshrs.scala:307–326](https://github.com/riscv-boom/riscv-boom/blob/58ef2720eae13be26b3008c02b5a74ce29c61c44/src/main/scala/v4/lsu/mshrs.scala#L307-L326).
- **There is no decoded line-clean instruction:** both v4’s assembled decode tables and v3’s assembled tables omit Zicbom/custom CFlush decoding. The `haveCFlush` parameter defaults false, but changing that parameter does not add a clean entry to these tables. [BOOM@58ef272 src/main/scala/v4/exu/decode.scala:431–438](https://github.com/riscv-boom/riscv-boom/blob/58ef2720eae13be26b3008c02b5a74ce29c61c44/src/main/scala/v4/exu/decode.scala#L431-L438), [BOOM@58ef272 src/main/scala/v3/exu/decode.scala:482–486](https://github.com/riscv-boom/riscv-boom/blob/58ef2720eae13be26b3008c02b5a74ce29c61c44/src/main/scala/v3/exu/decode.scala#L482-L486), [BOOM@58ef272 src/main/scala/v4/common/parameters.scala:99](https://github.com/riscv-boom/riscv-boom/blob/58ef2720eae13be26b3008c02b5a74ce29c61c44/src/main/scala/v4/common/parameters.scala#L99).
- **Stop here:** unmatched instructions receive the illegal default and trigger the illegal-instruction condition. The proposed CLEAN sequence therefore has no supported execution path in this revision. [BOOM@58ef272 src/main/scala/v4/exu/decode.scala:65](https://github.com/riscv-boom/riscv-boom/blob/58ef2720eae13be26b3008c02b5a74ce29c61c44/src/main/scala/v4/exu/decode.scala#L65), [BOOM@58ef272 src/main/scala/v4/exu/decode.scala:461–466](https://github.com/riscv-boom/riscv-boom/blob/58ef2720eae13be26b3008c02b5a74ce29c61c44/src/main/scala/v4/exu/decode.scala#L461-L466).

XiangShan:

- **Instruction and local ordering:** `CBO_CLEAN` maps to `cbo_clean`. At the committed SQ head, CBO handling drains the store buffer, sends the CMO and waits for its response before writeback. MMIO handling also requires the head entry, so the later doorbell cannot pass this CBO locally. [XiangShan@09453ff src/main/scala/xiangshan/backend/vector/Decoder/Uop/ScalaUopTable.scala:570–576](https://github.com/OpenXiangShan/XiangShan/blob/09453ff8f44e9d2b526e6149c60e1dc3f08c7192/src/main/scala/xiangshan/backend/vector/Decoder/Uop/ScalaUopTable.scala#L570-L576), [XiangShan@09453ff src/main/scala/xiangshan/mem/lsqueue/NewStoreQueue.scala:962–1003](https://github.com/OpenXiangShan/XiangShan/blob/09453ff8f44e9d2b526e6149c60e1dc3f08c7192/src/main/scala/xiangshan/mem/lsqueue/NewStoreQueue.scala#L962-L1003), [XiangShan@09453ff src/main/scala/xiangshan/mem/lsqueue/NewStoreQueue.scala:1021–1032](https://github.com/OpenXiangShan/XiangShan/blob/09453ff8f44e9d2b526e6149c60e1dc3f08c7192/src/main/scala/xiangshan/mem/lsqueue/NewStoreQueue.scala#L1021-L1032).
- **The local response really is CBOAck:** the CMO unit waits for D-channel response; only `CBOAck` is routed into it. L2 orders dirty release/data handling before its downstream clean request and gates CBO response generation on completion state. [XiangShan@09453ff src/main/scala/xiangshan/cache/dcache/mainpipe/MissQueue.scala:375–415](https://github.com/OpenXiangShan/XiangShan/blob/09453ff8f44e9d2b526e6149c60e1dc3f08c7192/src/main/scala/xiangshan/cache/dcache/mainpipe/MissQueue.scala#L375-L415), [XiangShan@09453ff src/main/scala/xiangshan/cache/dcache/mainpipe/MissQueue.scala:1808–1809](https://github.com/OpenXiangShan/XiangShan/blob/09453ff8f44e9d2b526e6149c60e1dc3f08c7192/src/main/scala/xiangshan/cache/dcache/mainpipe/MissQueue.scala#L1808-L1809), [XSCache@300515b src/main/scala/coupledL2/MSHR.scala:268–288](https://github.com/OpenXiangShan/XSCache/blob/300515bc7b2f65ca988041a984204489d80ec2fb/src/main/scala/coupledL2/MSHR.scala#L268-L288).
- **The memory bridge can acknowledge early:** Top selects `axiMasterOrder=WriteAddress` and `writeNoError=true`; `writeCompPreferSeperate` defaults false. These settings enable `CompDBIDResp`, whose queue operation has no AXI-B barrier. The separate `Comp` operation has that barrier. [XiangShan@09453ff src/main/scala/top/Top.scala:116–127](https://github.com/OpenXiangShan/XiangShan/blob/09453ff8f44e9d2b526e6149c60e1dc3f08c7192/src/main/scala/top/Top.scala#L116-L127), [OpenNCB@9a83eb7 src/main/scala/openncb/NCBParameters.scala:123](https://github.com/OpenXiangShan/OpenNCB/blob/9a83eb7e6c0e36e0d6b72642ff93641ec65819cf/src/main/scala/openncb/NCBParameters.scala#L123), [OpenNCB@9a83eb7 src/main/scala/openncb/logical/NCBUpstreamRXREQ.scala:507–512](https://github.com/OpenXiangShan/OpenNCB/blob/9a83eb7e6c0e36e0d6b72642ff93641ec65819cf/src/main/scala/openncb/logical/NCBUpstreamRXREQ.scala#L507-L512), [OpenNCB@9a83eb7 src/main/scala/openncb/logical/NCBTransactionQueue.scala:54–70](https://github.com/OpenXiangShan/OpenNCB/blob/9a83eb7e6c0e36e0d6b72642ff93641ec65819cf/src/main/scala/openncb/logical/NCBTransactionQueue.scala#L54-L70).
- **The LLC accepts that early completion:** memory writes use `WriteNoSnpFull`; `CompDBIDResp` sets `w_comp`, and the memory entry can free after sending data without waiting for AXI B. The same-address clean hazard depends on that entry remaining valid, so it does not extend ordering through B. [XSCache@300515b src/main/scala/openLLC/MainPipe.scala:442–459](https://github.com/OpenXiangShan/XSCache/blob/300515bc7b2f65ca988041a984204489d80ec2fb/src/main/scala/openLLC/MainPipe.scala#L442-L459), [XSCache@300515b src/main/scala/openLLC/MemUnit.scala:253–293](https://github.com/OpenXiangShan/XSCache/blob/300515bc7b2f65ca988041a984204489d80ec2fb/src/main/scala/openLLC/MemUnit.scala#L253-L293), [XSCache@300515b src/main/scala/openLLC/RequestArb.scala:123–137](https://github.com/OpenXiangShan/XSCache/blob/300515bc7b2f65ca988041a984204489d80ec2fb/src/main/scala/openLLC/RequestArb.scala#L123-L137).
- **Two downstream paths:** LLC response handling accepts `CompDBIDResp` as completion and can return the final clean `Comp`. Once CBOAck releases the SQ head, MMIO travels through a separate per-core bridge, while DRAM traffic travels through OpenLLC and its memory bridge. Nothing in this connection waits for the memory bridge’s B before permitting MMIO. [XSCache@300515b src/main/scala/openLLC/ResponseUnit.scala:163–174](https://github.com/OpenXiangShan/XSCache/blob/300515bc7b2f65ca988041a984204489d80ec2fb/src/main/scala/openLLC/ResponseUnit.scala#L163-L174), [XSCache@300515b src/main/scala/openLLC/ResponseUnit.scala:288–300](https://github.com/OpenXiangShan/XSCache/blob/300515bc7b2f65ca988041a984204489d80ec2fb/src/main/scala/openLLC/ResponseUnit.scala#L288-L300), [XiangShan@09453ff src/main/scala/top/Top.scala:503–528](https://github.com/OpenXiangShan/XiangShan/blob/09453ff8f44e9d2b526e6149c60e1dc3f08c7192/src/main/scala/top/Top.scala#L503-L528).
- **Fence implementation:** ordinary FENCE waits for `sbEmpty`, supplied by store-buffer and uncache-buffer emptiness. Store-buffer emptiness includes SQ and store MSHRs, but carries no downstream memory-bridge B status. Thus both standard forms can finish after early CBOAck while external B remains pending. [XiangShan@09453ff src/main/scala/xiangshan/backend/fu/Fence.scala:64–87](https://github.com/OpenXiangShan/XiangShan/blob/09453ff8f44e9d2b526e6149c60e1dc3f08c7192/src/main/scala/xiangshan/backend/fu/Fence.scala#L64-L87), [XiangShan@09453ff src/main/scala/xiangshan/mem/MemBlock.scala:1175–1186](https://github.com/OpenXiangShan/XiangShan/blob/09453ff8f44e9d2b526e6149c60e1dc3f08c7192/src/main/scala/xiangshan/mem/MemBlock.scala#L1175-L1186), [XiangShan@09453ff src/main/scala/xiangshan/mem/sbuffer/Sbuffer.scala:537–557](https://github.com/OpenXiangShan/XiangShan/blob/09453ff8f44e9d2b526e6149c60e1dc3f08c7192/src/main/scala/xiangshan/mem/sbuffer/Sbuffer.scala#L537-L557).

OpenC910:

- **Instruction choice matters:** `dcache.cva`/`cpa` decode as address clean operations with size `01`; `cval1`/`cpal1` use size `00`. The WMB generates CleanShared, and CIU distinguishes L1-only from L2 clean using the request’s cache attributes. Use the wider operation for the DRAM scenario; an L1-only clean does not establish a DRAM-clean guarantee. [OpenC910@b91c909 C910_RTL_FACTORY/gen_rtl/idu/rtl/ct_idu_rf_pipe4_decd.v:1063–1172](https://github.com/XUANTIE-RV/openc910/blob/b91c90914c19f114d35c8f6b73408eb241ed847c/C910_RTL_FACTORY/gen_rtl/idu/rtl/ct_idu_rf_pipe4_decd.v#L1063-L1172), [OpenC910@b91c909 C910_RTL_FACTORY/gen_rtl/lsu/rtl/ct_lsu_wmb.v:4066–4069](https://github.com/XUANTIE-RV/openc910/blob/b91c90914c19f114d35c8f6b73408eb241ed847c/C910_RTL_FACTORY/gen_rtl/lsu/rtl/ct_lsu_wmb.v#L4066-L4069), [OpenC910@b91c909 C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_ciu_snb_sab_entry.v:1730–1735](https://github.com/XUANTIE-RV/openc910/blob/b91c90914c19f114d35c8f6b73408eb241ed847c/C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_ciu_snb_sab_entry.v#L1730-L1735).
- **The WMB does not globally wait for the clean response:** its write pointer advances on write/VB-create grant, and its read pointer advances on AR grant. An ordinary MMIO store’s bus request tests its own read response; the `vb_wmb_empty` requirement belongs to the sync/fence branch. This permits younger MMIO progress for addresses without an index hazard. [OpenC910@b91c909 C910_RTL_FACTORY/gen_rtl/lsu/rtl/ct_lsu_wmb.v:3237–3271](https://github.com/XUANTIE-RV/openc910/blob/b91c90914c19f114d35c8f6b73408eb241ed847c/C910_RTL_FACTORY/gen_rtl/lsu/rtl/ct_lsu_wmb.v#L3237-L3271), [OpenC910@b91c909 C910_RTL_FACTORY/gen_rtl/lsu/rtl/ct_lsu_wmb_entry.v:1819–1838](https://github.com/XUANTIE-RV/openc910/blob/b91c90914c19f114d35c8f6b73408eb241ed847c/C910_RTL_FACTORY/gen_rtl/lsu/rtl/ct_lsu_wmb_entry.v#L1819-L1838).
- **Even clean completion can precede memory B:** a dirty L2 clean enters `MEMW`, then moves to `POP` on `memw_cmplt`, enabling its read response. That completion equals `vb_snb_w_grant`, which equals SNB write-data-valid into CIU’s VB—not external B. [OpenC910@b91c909 C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_ciu_snb_sab_entry.v:855–875](https://github.com/XUANTIE-RV/openc910/blob/b91c90914c19f114d35c8f6b73408eb241ed847c/C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_ciu_snb_sab_entry.v#L855-L875), [OpenC910@b91c909 C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_ciu_snb_sab_entry.v:937–944](https://github.com/XUANTIE-RV/openc910/blob/b91c90914c19f114d35c8f6b73408eb241ed847c/C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_ciu_snb_sab_entry.v#L937-L944), [OpenC910@b91c909 C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_ciu_snb_arb.v:2095–2098](https://github.com/XUANTIE-RV/openc910/blob/b91c90914c19f114d35c8f6b73408eb241ed847c/C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_ciu_snb_arb.v#L2095-L2098), [OpenC910@b91c909 C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_ciu_vb.v:491–492](https://github.com/XUANTIE-RV/openc910/blob/b91c90914c19f114d35c8f6b73408eb241ed847c/C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_ciu_vb.v#L491-L492).
- **Two downstream paths:** cacheable clean AR routes to SNB0/1; uncached doorbell AW routes to NCQ. NCQ admission checks its barrier and queue capacity, not pending SNB write-back acknowledgment, and can forward to APB or EBIU independently. [OpenC910@b91c909 C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_piu_top.v:871–877](https://github.com/XUANTIE-RV/openc910/blob/b91c90914c19f114d35c8f6b73408eb241ed847c/C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_piu_top.v#L871-L877), [OpenC910@b91c909 C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_piu_top.v:1002–1005](https://github.com/XUANTIE-RV/openc910/blob/b91c90914c19f114d35c8f6b73408eb241ed847c/C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_piu_top.v#L1002-L1005), [OpenC910@b91c909 C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_ciu_ncq.v:1026–1030](https://github.com/XUANTIE-RV/openc910/blob/b91c90914c19f114d35c8f6b73408eb241ed847c/C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_ciu_ncq.v#L1026-L1030), [OpenC910@b91c909 C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_ciu_ncq.v:1215–1216](https://github.com/XUANTIE-RV/openc910/blob/b91c90914c19f114d35c8f6b73408eb241ed847c/C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_ciu_ncq.v#L1215-L1216).
- **Verified custom-sync machinery:** `sync` (`0x0180000b`) is classified as a serializing instruction; ID waits for pipeline empty after issuing it. WMB completion requires both read and write responses. With barriers enabled, CIU waits for module completions and `ebiu_piu_no_op`; EBIU’s CAWT entries are removed by external B responses. [OpenC910@b91c909 C910_RTL_FACTORY/gen_rtl/idu/rtl/ct_idu_id_decd_special.v:189–196](https://github.com/XUANTIE-RV/openc910/blob/b91c90914c19f114d35c8f6b73408eb241ed847c/C910_RTL_FACTORY/gen_rtl/idu/rtl/ct_idu_id_decd_special.v#L189-L196), [OpenC910@b91c909 C910_RTL_FACTORY/gen_rtl/idu/rtl/ct_idu_id_fence.v:298–322](https://github.com/XUANTIE-RV/openc910/blob/b91c90914c19f114d35c8f6b73408eb241ed847c/C910_RTL_FACTORY/gen_rtl/idu/rtl/ct_idu_id_fence.v#L298-L322), [OpenC910@b91c909 C910_RTL_FACTORY/gen_rtl/lsu/rtl/ct_lsu_wmb_entry.v:2048–2054](https://github.com/XUANTIE-RV/openc910/blob/b91c90914c19f114d35c8f6b73408eb241ed847c/C910_RTL_FACTORY/gen_rtl/lsu/rtl/ct_lsu_wmb_entry.v#L2048-L2054), [OpenC910@b91c909 C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_piu_top.v:2095–2099](https://github.com/XUANTIE-RV/openc910/blob/b91c90914c19f114d35c8f6b73408eb241ed847c/C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_piu_top.v#L2095-L2099), [OpenC910@b91c909 C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_ebiu_write_channel.v:1492–1513](https://github.com/XUANTIE-RV/openc910/blob/b91c90914c19f114d35c8f6b73408eb241ed847c/C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_ebiu_write_channel.v#L1492-L1513).
- **Unresolved fence coverage:** CIU VB can contain a write awaiting EBIU AW grant, while CAWT allocation occurs only upon that grant. EBIU admission can stall on capacity/address/output-buffer conditions; the checked per-core `no_op` expression examines EBIU trackers rather than CIU VB validity. I therefore cannot certify `sync`, `fence w,o`, or `fence rw,rw` as a complete memory-ack drain without resolving that pre-admission interval. [OpenC910@b91c909 C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_ciu_vb.v:456–466](https://github.com/XUANTIE-RV/openc910/blob/b91c90914c19f114d35c8f6b73408eb241ed847c/C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_ciu_vb.v#L456-L466), [OpenC910@b91c909 C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_ebiu_write_channel.v:545–598](https://github.com/XUANTIE-RV/openc910/blob/b91c90914c19f114d35c8f6b73408eb241ed847c/C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_ebiu_write_channel.v#L545-L598), [OpenC910@b91c909 C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_ebiu_write_channel.v:1492–1513](https://github.com/XUANTIE-RV/openc910/blob/b91c90914c19f114d35c8f6b73408eb241ed847c/C910_RTL_FACTORY/gen_rtl/ciu/rtl/ct_ebiu_write_channel.v#L1492-L1513).

## Appendix: the prompt given to the agent

Line breaks from the paste cleaned up; text unchanged.

```text
TASK: Does a cache-line clean order a later MMIO store without a fence? Check 4 open-source RISC-V cores in their RTL.

CONTEXT
I'm designing a RISC-V "PIM fence" instruction. Software writes data (it sits dirty in the L1 D-cache),
cleans that line so DRAM holds the data, then does an uncached MMIO store (a "doorbell") that tells a
memory-side device to read that DRAM. I need to know, per core, whether the hardware already guarantees
that the doorbell cannot reach the device before the clean's write-back has landed in memory, with NO
fence between them.

SCENARIO (no fence)
    sd    t0, 0(a0)   # a0: cacheable DRAM address -> line becomes dirty in L1 D$
    CLEAN a0          # Zicbom cbo.clean, or the core's own line write-back instruction
    sd    t1, 0(a1)   # a1: uncached MMIO device address (the doorbell)

QUESTION, PER CORE
Can the doorbell store reach its target before the write-back of line a0 has been acknowledged by memory?
- If NO: name the mechanism that holds the doorbell back, with file:line.
- If YES: show the two paths and why nothing orders them, with file:line.
- Also: which fence closes the gap on this core (fence w,o / fence rw,rw / a custom sync), and how
  that fence is implemented (file:line).
- If the core has no write-back L1 D-cache or no line-clean instruction, say so and stop for that core.

CORES
1. Rocket    - https://github.com/chipsalliance/rocket-chip
2. BOOM      - https://github.com/riscv-boom/riscv-boom
3. XiangShan - https://github.com/OpenXiangShan/XiangShan  (init only the submodules you need)
4. OpenC910  - https://github.com/T-head-Semi/openc910

REFERENCE ANSWER (already done, use it to calibrate depth and format)
CVA6 (openhwgroup/cva6 @ 81245a4, HPDcache write-back config): NO.
Every data access, cached or not, goes through the HPDcache. It holds any uncacheable request while any
transaction is pending (core/cache_subsystem/hpdcache/rtl/src/hpdcache_ctrl_pe.sv:571-583). "Pending"
includes the flush unit (hpdcache_ctrl.sv:768-773), and a flush entry stays pending until the memory
write response arrives (hpdcache_flush.sv:225, 262-265).

RULES
- Evidence = RTL source only (Chisel / Verilog / SystemVerilog), cited as repo@commit path:line.
  Docs, READMEs and code comments are hints, not evidence.
- Shallow clones. Grep first, then read only the line ranges you need. Do not build or simulate.
- Budget: aim for under ~80k tokens per core. If a trace runs out of budget, report what is verified
  and what is not. Never guess: "Unclear" is a valid answer.

OUTPUT
1. One table: Core | Line-clean instruction | Doorbell can overtake write-back? (Yes / No / Unclear) |
   Mechanism (file:line) | Fence that closes it | Confidence
2. Under the table, for each core, 3-6 bullets giving the evidence chain, each with file:line.
```
