"""
RandomTraceGenerator.py
------------------------

This script generates a synthetic DRAM trace file for use in memory system simulations.

Each trace entry corresponds to a DRAM transaction that is issued at a particular cycle,
with the type (READ or WRITE) and target address selected probabilistically.

Usage:
    python RandomTraceGenerator.py -t 0.01 -r 0.5 -f output -n 10000 -m 65536

Arguments:
    -t, --tran_p     : Probability of issuing a transaction at any given cycle (default: 0.05)
    -r, --read_p     : Probability that the issued transaction is a READ (vs WRITE) (default: 0.8)
    -f, --file_name  : Name prefix for the generated output trace file (no extension)
    -n, --num_cycles : Total number of cycles to simulate (default: 10000)
    -c, --config       : Path to memoryconfiguration .ini file

Output:
    A `.trace` file named `<file_name>.trace` is generated with the following columns:
        1. Cycle number
        2. Transaction type ("READ" or "WRITE")
        3. Address in hexadecimal format

Example output line:
    42         WRITE    0x1f3ac

This file is designed to be human-readable and compatible with simulators expecting simple, cycle-accurate transaction traces.
"""

import argparse
import random

def get_max_address(path):
    cfg = {}
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith(";"):
                continue
            if '=' in line:
                key, val = line.split('=')
                key, val = key.strip(), val.strip().rstrip(';')
                cfg[key] = int(val) if val.isdigit() else val
    return int(cfg["ranks"]) * int(cfg["bankgroups"]) * int(cfg["banks_per_group"]) * int(cfg["rows"]) * int(cfg["channels"]) * int(cfg["columns"]) * int(cfg["device_width"]) // 8
def generate_trace(tran_p, read_p, file_name, num_cycles, max_addr):
    with open(file_name + ".trace", 'w') as f:
        for cycle in range(num_cycles):
            if random.random() < tran_p:
                tran_type = "READ" if random.random() < read_p else "WRITE"
                addr = hex(random.randint(0, max_addr))
                f.write(f"{cycle:<10} {tran_type:<6} {addr:>10}\n")


def main():
    parser = argparse.ArgumentParser(description="Random DRAM Trace Generator")
    parser.add_argument('-t', '--tran_p', type=float, default=0.01, help='Transaction probability per cycle (default: 0.01)')
    parser.add_argument('-r', '--read_p', type=float, default=0.5, help='Probability of READ (vs WRITE) (default: 0.5)')
    parser.add_argument('-f', '--file_name', type=str, required=True, help='Output trace file name (without extension)')
    parser.add_argument('-n', '--num_cycles', type=int, default=10000, help='Total number of cycles (default: 10000)')
    parser.add_argument("-c", "--config", type=str, required=True, help="Path to DRAM config file")

    args = parser.parse_args()
    max_addr = get_max_address(args.config)
    generate_trace(args.tran_p, args.read_p, args.file_name, args.num_cycles, max_addr)


if __name__ == '__main__':
    main()