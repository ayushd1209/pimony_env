#!/usr/bin/env python3
"""Validate isolated fence_compare runs and summarize their first stats block.

Usage: python3 tests/test-progs/fence_compare/analyze.py RESULTS_RUN_DIRECTORY
The default matrix is two CPU models, three sizes, and three maintenance modes.
Trace counts describe log events, not hardware transactions. In particular, a
WriteClean packet may appear repeatedly as it traverses the cache hierarchy.
"""

import argparse
import configparser
import csv
from decimal import Decimal, InvalidOperation
import hashlib
import json
from pathlib import Path
import re
import sys


BEGIN_STATS = "---------- Begin Simulation Statistics ----------"
END_STATS = "---------- End Simulation Statistics   ----------"
TICK = re.compile(r"^\s*(\d+):")
EXEC = re.compile(
    r"^\s*\d+:\s+system\.cpu:\s+.*?"
    r"0x[0-9a-fA-F]+(?:\s+@[^:]+)?(?:\.\s*\d+)?\s*:\s*"
    r"([A-Za-z_][A-Za-z0-9_.]*)\b"
)
ADDRESS_RANGE = re.compile(r"\[\s*(?:0x)?([0-9a-fA-F]+)\s*:")
BAD_LOG = re.compile(r"\b(?:fatal|panic)\s*:|illegal\s+(?:instruction|inst)|"
                     r"Traceback \(most recent call last\)", re.IGNORECASE)
MODES = {0: "pim.fence.cl/line", 1: "cbo.clean+FENCE/line", 2: "batched cbo.clean+FENCE"}


def number(value):
    """A compact exact representation; retain fractions if boundaries differ."""
    if value is None:
        return ""
    value = Decimal(value)
    if value == value.to_integral_value():
        return str(int(value))
    return format(value.normalize(), "f")


def stats_blocks(path):
    blocks = []
    current = None
    for line in path.read_text(errors="replace").splitlines():
        if line.strip() == BEGIN_STATS:
            if current is not None:
                raise ValueError("nested or unterminated statistics block")
            current = {}
        elif line.strip() == END_STATS:
            if current is None:
                raise ValueError("statistics block end without a start")
            blocks.append(current)
            current = None
        elif current is not None:
            fields = line.split()
            if len(fields) >= 2 and not line.lstrip().startswith("#"):
                try:
                    current[fields[0]] = Decimal(fields[1])
                except InvalidOperation:
                    pass
    if current is not None:
        raise ValueError("unterminated statistics block")
    return blocks


def configuration(path):
    config = configparser.ConfigParser(interpolation=None, strict=False)
    config.read(path)
    clock = Decimal(config["system.clk_domain"]["clock"].split()[0])
    # Compare CPU, caches and memory configuration across modes/sizes for one
    # CPU model. The per-run ELF path and output locations are not settings.
    sections = {}
    prefixes = ("system.cpu", "system.l2cache", "system.l2bus", "system.membus",
                "system.clk_domain", "system.mem_ctrl")
    ignored = {"output_dir", "output_directory", "log_dir", "trace_file", "trace_filename"}
    for section in config.sections():
        if any(section == p or section.startswith(p + ".") for p in prefixes):
            sections[section] = {k: v for k, v in config[section].items()
                                 if k not in ignored}
    for key in ("cache_line_size", "mem_mode", "mem_ranges"):
        sections.setdefault("system", {})[key] = config["system"].get(key, "")
    fingerprint = hashlib.sha256(json.dumps(sections, sort_keys=True).encode()).hexdigest()
    return config, clock, fingerprint


def trace_counts(path, start, end, nlines, handoff_address=None, handoff_kind="mmio_read"):
    counts = {"trace_custom_insts": 0, "trace_standard_insts": 0,
              "trace_full_fences": 0, "trace_roi_exec_lines": 0,
              "roi_writeclean_log_events": 0, "roi_l1_writeclean_created": 0}
    created_lines = set()
    has_exec = False
    has_cache = False
    in_exec_roi = False
    reset_ticks, dump_ticks = [], []
    dirty_lines, response_lines, memory_lines = set(), set(), set()
    last_response = None
    handoff_requests, handoff_responses = [], []
    active_cleans = peak_cleans = 0
    guard_commits = 0
    guard_follows_request = False
    data_start = 0x81000000
    data_end = data_start + nlines * 64
    for line in path.read_text(errors="replace").splitlines():
        event_tick = TICK.match(line)
        if not event_tick:
            continue
        instruction = EXEC.match(line)
        has_exec |= instruction is not None
        has_cache |= "system.cpu.dcache:" in line and (
            "access for" in line or "Create " in line or "recvTimingReq" in line)
        tick = Decimal(event_tick[1])
        address_match = ADDRESS_RANGE.search(line)
        addr = int(address_match[1], 16) if address_match else None
        operand = addr is not None and data_start <= addr < data_end
        if tick < start and operand and "system.cpu.dcache: access for WriteReq" in line:
            dirty_lines.add(addr & ~63)
        if "pseudo_inst::resetstats(0, 0)" in line:
            reset_ticks.append(tick)
            in_exec_roi = True
        if "pseudo_inst::dumpresetstats(0, 0)" in line:
            dump_ticks.append(tick)
            in_exec_roi = False
        # O3 emits Exec at commit with the instruction's FETCH timestamp.
        # Select committed instructions by marker order in the log, never by
        # that timestamp. Cache/PseudoInst events use current simulation time.
        if instruction and in_exec_roi:
            counts["trace_roi_exec_lines"] += 1
            mnemonic = instruction[1]
            if mnemonic in ("pim_fence_cl", "pim.fence.cl"):
                counts["trace_custom_insts"] += 1
            elif mnemonic in ("cbo_clean", "cbo.clean"):
                counts["trace_standard_insts"] += 1
            elif mnemonic == "fence":
                counts["trace_full_fences"] += 1
            if (handoff_kind == "release_amo" and "amoswap_d[l]" in line and
                    "MemRead" in line and f"A={handoff_address:#x}" in line):
                guard_commits += 1
                guard_follows_request = len(handoff_requests) == 1
        if not start <= tick <= end:
            continue
        if operand and "system.cpu.dcache: access for CleanSharedReq" in line:
            active_cleans += 1
            peak_cleans = max(peak_cleans, active_cleans)
        if operand and "system.cpu.dcache: recvTimingResp: Handling response CleanSharedResp" in line:
            response_lines.add(addr & ~63)
            last_response = tick
            active_cleans -= 1
        memory_access = re.search(r"system\.mem_ctrl: Access for address (\d+)", line)
        if memory_access and data_start <= int(memory_access[1]) < data_end:
            memory_lines.add(int(memory_access[1]) & ~63)
        if handoff_address is not None and addr == handoff_address:
            req_cmd = "SwapReq" if handoff_kind == "release_amo" else "ReadReq"
            resp_cmd = "SwapResp" if handoff_kind == "release_amo" else "ReadResp"
            if f"system.cpu.dcache: access for {req_cmd}" in line:
                handoff_requests.append(tick)
            if (f"system.cpu.dcache: recvTimingResp: Handling response {resp_cmd}" in line or
                (handoff_kind == "release_amo" and "system.cpu.dcache: satisfied " in line and "Swap" in line)):
                handoff_responses.append(tick)
        if "WriteClean" not in line:
            continue
        address = ADDRESS_RANGE.search(line)
        if not address:
            continue
        address = int(address[1], 16)
        if data_start <= address < data_end:
            counts["roi_writeclean_log_events"] += 1
            if "system.cpu.dcache:" in line and "Create WriteClean" in line:
                counts["roi_l1_writeclean_created"] += 1
                created_lines.add(address & ~63)
    counts["roi_l1_writeclean_unique_lines"] = len(created_lines)
    counts["trace_markers_match_stats"] = int(reset_ticks == [start] and dump_ticks == [end])
    counts["initial_dirty_unique_lines"] = len(dirty_lines)
    counts["roi_clean_response_unique_lines"] = len(response_lines)
    counts["roi_memory_write_unique_lines"] = len(memory_lines)
    counts["roi_peak_clean_requests"] = peak_cleans
    counts["last_clean_response_tick"] = number(last_response)
    if handoff_address is not None:
        counts["handoff_request_tick"] = number(handoff_requests[0]) if handoff_requests else ""
        counts["handoff_response_tick"] = number(handoff_responses[0]) if handoff_responses else ""
        counts["handoff_guard_committed"] = guard_commits
        # Cache-hit AMOs return locally and Cache does not log their response
        # packet. Their completed micro-op is emitted at commit; O3's printed
        # timestamp is fetch time, so use log order after the SwapReq instead.
        returned = (guard_commits == 1 and guard_follows_request) if handoff_kind == "release_amo" else (
            len(handoff_responses) == 1 and handoff_responses[0] <= end)
        counts["handoff_order_verified"] = int(
            len(response_lines) == nlines and len(handoff_requests) == 1 and
            last_response is not None and returned and
            last_response <= handoff_requests[0] <= end)
    return counts, has_exec, has_cache


def analyze_case(run_dir, cpu, mode, nlines):
    case_dir = run_dir / f"{cpu}_m{mode}_n{nlines}"
    row = {"cpu": cpu, "mode": mode, "approach": MODES[mode], "lines": nlines,
           "status": "FAIL", "log": str(case_dir / "run.log")}
    errors, warnings = [], []
    provenance = json.loads((run_dir / "provenance.json").read_text()) if (run_dir / "provenance.json").exists() else {}
    handoff_address = int(provenance["handoff_address"], 0) if "handoff_address" in provenance else None
    if not case_dir.is_dir():
        errors.append("missing case directory")
    for filename in ("run.log", "exit_code.txt", "stats.txt", "config.ini"):
        if not (case_dir / filename).is_file():
            errors.append(f"missing {filename}")
    log_path = case_dir / "run.log"
    if log_path.is_file():
        log = log_path.read_text(errors="replace")
        if "m5_exit instruction encountered" not in log:
            errors.append("normal m5_exit reason missing")
        if BAD_LOG.search(log):
            errors.append("fatal/panic/illegal-instruction/exception in run.log")
        warning_lines = [line.strip() for line in log.splitlines()
                         if re.search(r"\bwarn(?:ing)?\s*:", line, re.IGNORECASE)]
        row["warning_count"] = len(warning_lines)
        warnings.extend(warning_lines)
    exit_path = case_dir / "exit_code.txt"
    if exit_path.is_file():
        row["exit_code"] = exit_path.read_text().strip()
        if row["exit_code"] != "0":
            errors.append("runner exit code is not zero")
    config, clock, blocks = None, None, []
    try:
        if (case_dir / "config.ini").is_file():
            config, clock, fingerprint = configuration(case_dir / "config.ini")
            row["clock_period_ticks"] = number(clock)
            row["config_fingerprint"] = fingerprint
            row["cpu_type"] = config["system.cpu"]["type"]
            if clock <= 0:
                errors.append("nonpositive CPU clock period")
            if config["system.cpu"].get("clk_domain") != "system.clk_domain":
                errors.append("CPU clock domain differs from measured system clock")
            if config["system"].get("cache_line_size") != "64":
                errors.append("cache line size differs from benchmark's 64 bytes")
            if cpu.lower() not in row["cpu_type"].lower():
                errors.append("CPU type does not match case name")
        if (case_dir / "stats.txt").is_file():
            blocks = stats_blocks(case_dir / "stats.txt")
            row["stats_blocks"] = len(blocks)
            if len(blocks) != 2:
                errors.append(f"expected measured and exit stats blocks; found {len(blocks)}")
    except (ValueError, KeyError, configparser.Error) as exc:
        errors.append(f"configuration/statistics parse: {exc}")
    if blocks:
        measured = blocks[0]
        for stat, field in (("simTicks", "simTicks"), ("finalTick", "roi_end_tick"),
                            ("simFreq", "simFreq"),
                            ("system.cpu.numCycles", "cpu_numCycles"),
                            ("simInsts", "simInsts")):
            if stat in measured:
                row[field] = number(measured[stat])
            elif stat != "simInsts":
                errors.append(f"first statistics block lacks {stat}")
        if clock and clock > 0 and "simTicks" in measured:
            row["elapsed_cycles"] = number(measured["simTicks"] / clock)
            if measured["simTicks"] <= 0:
                errors.append("nonpositive measured interval")
            if "system.cpu.numCycles" in measured and abs(
                    measured["system.cpu.numCycles"] - measured["simTicks"] / clock) > 1:
                warnings.append("CPU numCycles differs from elapsed ticks/clock by more than one cycle")
        if "simTicks" in measured and "finalTick" in measured:
            start = measured["finalTick"] - measured["simTicks"]
            row["roi_start_tick"] = number(start)
            if start <= 0:
                errors.append("ROI did not start after initialization")
            if len(blocks) == 2 and blocks[1].get("finalTick", 0) < measured["finalTick"]:
                errors.append("exit statistics precede ROI end")
            if (case_dir / "debug.log").is_file():
                trace, has_exec, has_cache = trace_counts(
                    case_dir / "debug.log", start, measured["finalTick"], nlines, handoff_address,
                    provenance.get("handoff_kind", "mmio_read"))
                row.update(trace)
                row["trace_exec_available"] = int(has_exec)
                row["trace_cache_available"] = int(has_cache)
                if not trace["trace_markers_match_stats"]:
                    errors.append("PseudoInst reset/dump markers do not match ROI stats boundaries")
                if has_cache:
                    for key in ("initial_dirty_unique_lines", "roi_clean_response_unique_lines", "roi_memory_write_unique_lines"):
                        if trace[key] != nlines:
                            errors.append(f"{key}={trace[key]}, expected {nlines}")
                if handoff_address is not None and not trace.get("handoff_order_verified"):
                    errors.append("Completion guard/probe did not issue after all clean responses and return before dump")
                if has_exec:
                    expected = {"trace_custom_insts": nlines if mode == 0 else 0,
                                "trace_standard_insts": nlines if mode != 0 else 0,
                                "trace_full_fences": 0 if mode == 0 else nlines if mode == 1 else 1}
                    for key, value in expected.items():
                        if trace[key] != value:
                            errors.append(f"ROI {key}={trace[key]}, expected {value}")
                else:
                    warnings.append("debug.log has no recognized Exec trace; instruction count unverified")
                if has_cache and trace["roi_l1_writeclean_unique_lines"] != nlines:
                    errors.append("Cache trace does not show one operand WriteClean creation per line in ROI")
                if not has_cache:
                    warnings.append("debug.log has no recognized Cache trace; writebacks unverified")
            else:
                warnings.append("debug.log absent; per-case instruction/writeback traces unavailable")
    row["errors"] = " | ".join(errors)
    row["warnings"] = " | ".join(warnings)
    row["status"] = "FAIL" if errors else "PASS"
    return row


def write_outputs(run_dir, rows, cpus, line_counts):
    fields = ["cpu", "mode", "approach", "lines", "status", "elapsed_cycles",
              "cpu_numCycles", "simTicks", "clock_period_ticks", "simFreq",
              "roi_start_tick", "roi_end_tick", "stats_blocks", "simInsts",
              "exit_code", "cpu_type", "config_fingerprint", "warning_count",
              "trace_exec_available", "trace_cache_available", "trace_roi_exec_lines",
              "trace_markers_match_stats",
              "trace_custom_insts", "trace_standard_insts", "trace_full_fences",
              "roi_l1_writeclean_created", "roi_l1_writeclean_unique_lines",
              "roi_writeclean_log_events", "errors", "warnings", "log"]
    fields[-3:-3] = ["initial_dirty_unique_lines", "roi_clean_response_unique_lines",
                    "roi_memory_write_unique_lines", "roi_peak_clean_requests",
                    "last_clean_response_tick", "handoff_request_tick",
                    "handoff_response_tick", "handoff_guard_committed", "handoff_order_verified"]
    with (run_dir / "measurements.tsv").open("w", newline="") as output:
        writer = csv.DictWriter(output, fields, delimiter="\t", extrasaction="raise")
        writer.writeheader()
        writer.writerows(rows)
    lookup = {(r["cpu"], r["lines"], r["mode"]): r for r in rows}
    text = ["| CPU | Lines | pim.fence.cl/line (cycles) | cbo.clean+FENCE/line (cycles) | Batched cbo.clean+FENCE (cycles) | Per-line vs custom | Batched vs custom |",
            "|---|---:|---:|---:|---:|---:|---:|"]
    for cpu in cpus:
        for nlines in line_counts:
            cases = [lookup[cpu, nlines, mode] for mode in range(3)]
            values = [r.get("elapsed_cycles", "") if r["status"] == "PASS" else "FAILED"
                      for r in cases]
            percentages = []
            for mode in (1, 2):
                if values[0] not in ("FAILED", "") and values[mode] not in ("FAILED", ""):
                    baseline = Decimal(values[0])
                    difference = 100 * (Decimal(values[mode]) - baseline) / baseline
                    percentages.append(f"{difference:+.2f}%")
                else:
                    percentages.append("—")
            text.append(f"| {cpu} | {nlines} | " + " | ".join(values + percentages) + " |")
    text += ["", "Cycles are elapsed ROI simTicks divided by the configured CPU clock period. "
             "The separate cpu_numCycles counter is retained in measurements.tsv.", "",
             "Percent difference = 100 × (standard cycles − custom cycles) / custom cycles. "
             "Negative values mean the standard sequence is faster.", "",
             "The first statistics block measures reset→dump; the second is the post-ROI exit interval. "
             "These are simulated pipeline/cache costs, not real hardware performance or proof of PIM functional equivalence.", ""]
    if (run_dir / "provenance.json").exists():
        metadata = json.loads((run_dir / "provenance.json").read_text())
        text += ["Measured kernel: " + metadata.get("measurement", "unspecified") + ".", ""]
    failures = [r for r in rows if r["status"] != "PASS"]
    if failures:
        text.append("Failures:")
        text.append("")
        text.extend(f"- {r['cpu']}_m{r['mode']}_n{r['lines']}: {r['errors']}" for r in failures)
        text.append("")
    warned = [r for r in rows if r["warnings"]]
    if warned:
        text += ["Warnings and trace availability:", ""]
        text.extend(f"- {r['cpu']}_m{r['mode']}_n{r['lines']}: {r['warnings']}" for r in warned)
        text.append("")
    (run_dir / "comparison.md").write_text("\n".join(text))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run_dir", type=Path)
    parser.add_argument("--cpus", nargs="+", default=["o3", "minor"], choices=["o3", "minor"])
    parser.add_argument("--lines", nargs="+", type=int, default=[1, 16, 64])
    args = parser.parse_args()
    if not args.run_dir.is_dir():
        parser.error("run_dir must already exist")
    rows = [analyze_case(args.run_dir, cpu, mode, nlines)
            for cpu in args.cpus for nlines in args.lines for mode in range(3)]
    # Every approach/size must retain the same machine for its CPU model.
    for cpu in args.cpus:
        cpu_rows = [r for r in rows if r["cpu"] == cpu and r.get("config_fingerprint")]
        if len({r["config_fingerprint"] for r in cpu_rows}) > 1:
            for row in cpu_rows:
                row["status"] = "FAIL"
                row["errors"] = " | ".join(filter(None, [row["errors"], "CPU/cache/memory settings differ across cases"]))
    clock_periods = {r["clock_period_ticks"] for r in rows if r.get("clock_period_ticks")}
    if len(clock_periods) > 1:
        for row in rows:
            row["status"] = "FAIL"
            row["errors"] = " | ".join(filter(None, [row["errors"], "clock frequency differs across cases"]))
    write_outputs(args.run_dir, rows, args.cpus, args.lines)
    passed = sum(r["status"] == "PASS" for r in rows)
    print(f"Validated {passed}/{len(rows)} cases; {args.run_dir / 'comparison.md'}")
    return 0 if passed == len(rows) else 1


if __name__ == "__main__":
    sys.exit(main())
