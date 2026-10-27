#!/usr/bin/env python3
"""Report only the first, timed projection block; elapsed cycles include PIM sleep."""

import argparse
import configparser
from pathlib import Path


def first_block(path):
    stats, inside, complete = {}, False, False
    with path.open() as stream:
        for line in stream:
            if "Begin Simulation Statistics" in line:
                if inside:
                    break
                inside = True
            elif "End Simulation Statistics" in line and inside:
                complete = True
                break
            elif inside:
                fields = line.split()
                if len(fields) >= 2 and not line.startswith("#"):
                    try:
                        stats[fields[0]] = float(fields[1])
                    except ValueError:
                        pass
    if not complete or "simTicks" not in stats:
        raise ValueError(f"{path}: first statistics block is incomplete")
    return stats


def read_run(root, tag, backend, vectors):
    directory = root / f"{tag}_{backend}_{vectors}_o3"
    log = (directory / "run.log").read_text(errors="replace")
    if "Exiting @ tick" not in log or any(word in log.lower() for word in
                                          ("m5_fail", "fatal:", "panic:", "w1 check: fail")):
        raise ValueError(f"{directory}: run did not finish successfully")
    if backend == "cpu" and "w1 check: PASS" not in log:
        raise ValueError(f"{directory}: CPU reference check did not pass")
    normal_exit = ("because m5_exit instruction encountered" if backend == "pim"
                   else "because exiting with last active thread context")
    if normal_exit not in log:
        raise ValueError(f"{directory}: unexpected simulator exit cause")
    expected_binary = f"w1_{backend}_{vectors}"
    config = configparser.ConfigParser(interpolation=None, strict=False)
    config.read(directory / "config.ini")
    binary_key = "bootloader" if backend == "pim" else "cmd"
    section = "system.workload" if backend == "pim" else "system.cpu.workload"
    if expected_binary not in config.get(section, binary_key):
        raise ValueError(f"{directory}: unexpected workload binary")
    period = int(config.get("system.clk_domain", "clock"))
    if period <= 0:
        raise ValueError(f"{directory}: invalid CPU clock period")
    stats = first_block(directory / "stats.txt")
    cycles = stats["simTicks"] / period
    if cycles <= 0:
        raise ValueError(f"{directory}: nonpositive elapsed time")
    measured_cpu = "system.switch_cpu" if config.has_section("system.switch_cpu") else "system.cpu"
    if config.get(measured_cpu, "type") != "BaseO3CPU":
        raise ValueError(f"{directory}: measured CPU is not O3")
    sleep = stats.get(measured_cpu + ".quiesceCycles", 0)
    return directory, cycles, sleep, stats, config


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--m5out", type=Path, default=Path(__file__).resolve().parents[5] / "m5out")
    parser.add_argument("--tag", default="w1_compare")
    args = parser.parse_args()
    rows = {}
    for vectors in (1, 64):
        for backend in ("cpu", "pim"):
            rows[backend, vectors] = read_run(args.m5out, args.tag, backend, vectors)
    common = [("system.cpu", "type"), ("system.clk_domain", "clock"),
              ("system.cpu.icache", "size"), ("system.cpu.dcache", "size"),
              ("system.l2cache", "size"), ("system.mem_ctrl", "mem_config"),
              ("system.mem_ctrl", "model_config")]
    reference = rows["cpu", 1][4]
    for directory, _, _, _, config in rows.values():
        for section, key in common:
            if config.get(section, key) != reference.get(section, key):
                raise ValueError(f"{directory}: {section}.{key} differs from CPU-1")

    print("W1 projection only: 2560 inputs, 10240 outputs, FP16 weights")
    print("First stats block only; elapsed cycles = simTicks / configured clock period")
    print(f"{'Vectors':>7} {'CPU cycles':>14} {'PIM cycles':>14} {'CPU/vector':>14} {'PIM/vector':>14} {'CPU/PIM':>9}")
    for vectors in (1, 64):
        cpu = rows["cpu", vectors][1]
        pim = rows["pim", vectors][1]
        print(f"{vectors:7d} {cpu:14,.0f} {pim:14,.0f} {cpu/vectors:14,.0f} {pim/vectors:14,.0f} {cpu/pim:8.2f}x")
        print(f"        PIM sleep included: {rows['pim', vectors][2]:,.0f} cycles")
    print("CPU/PIM > 1 means PIM is faster. These are projection timings, not full-prefill speedups.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, configparser.Error) as error:
        raise SystemExit(str(error))
