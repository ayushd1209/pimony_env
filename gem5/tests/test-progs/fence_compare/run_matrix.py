"""Run synthetic ELFs in disposable containers against the exact pim-isa tree."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

HERE = Path(__file__).resolve().parent
WORKTREE = Path(os.environ.get("PIM_ISA_WORKTREE", str(HERE / "pim-isa-worktree"))).resolve()


def sha256(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("results", type=Path)
    parser.add_argument("--cpus", nargs="+", default=["o3", "minor"])
    parser.add_argument("--lines", nargs="+", type=int, default=[1, 16, 64])
    args = parser.parse_args()
    results = args.results.resolve()
    mount_results = Path("/bench") / results.relative_to(HERE)
    binary = WORKTREE / "gem5/build/RISCV/gem5.opt"
    config = WORKTREE / "gem5/configs/pimony/fs_bert.py"
    reference = json.loads((HERE / "validated/provenance.json").read_text())
    commit = subprocess.check_output(
        ["git", "-C", str(WORKTREE), "rev-parse", "HEAD"], text=True).strip()
    source_status = subprocess.check_output(
        ["git", "-C", str(WORKTREE), "status", "--short", "--untracked-files=no",
         "--ignore-submodules=all"], text=True)
    if commit != reference["commit"] or source_status:
        raise SystemExit("Use a clean isolated checkout at " + reference["commit"])
    if sha256(config) != reference["config_sha256"]:
        raise SystemExit("Isolated fs_bert.py differs from the measured configuration")
    metadata = {
        "branch": "pim-isa (detached isolated worktree)",
        "commit": commit,
        "worktree": str(WORKTREE),
        "source_status": source_status,
        "gem5_sha256": sha256(binary),
        "libdramsim3_sha256": sha256(WORKTREE / "gem5/ext/dramsim3/PIMony/libdramsim3.so"),
        "config_sha256": sha256(config),
        "elf_sha256": {p.name: sha256(p) for p in sorted((results / "bin").glob("fence_cmp*"))
                       if p.suffix != ".disasm"},
        "container_image": "gem5_env:latest",
        "container_image_id": subprocess.check_output(["docker", "image", "inspect", "gem5_env:latest", "--format", "{{.Id}}"], text=True).strip(),
        "cpus": args.cpus, "lines": args.lines,
        "measurement": "reset -> clean sequence -> common release-AMO completion guard on warmed scratch line -> dump/reset",
        "handoff_address": "0x82000000",
        "handoff_kind": "release_amo",
        "wall_start_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
    }
    (results / "provenance.json").write_text(json.dumps(metadata, indent=2) + "\n")
    (results / "source/fs_bert.py").write_bytes(config.read_bytes())
    for cpu in args.cpus:
        for n in args.lines:
            for mode in range(3):
                case = f"{cpu}_m{mode}_n{n}"
                out = results / case
                out.mkdir()
                mounted_out = mount_results / case
                name = f"fence-compare-{os.getpid()}-{case}"
                cmd = ["docker", "run", "--rm", "--network", "none", "--name", name,
                       "--user", f"{os.getuid()}:{os.getgid()}",
                       "--mount", f"type=bind,src={WORKTREE},dst=/home/pimony",
                       "--mount", f"type=bind,src={HERE},dst=/bench",
                       "--workdir", "/home/pimony/gem5", "gem5_env:latest", "env",
                       "LD_LIBRARY_PATH=/home/pimony/gem5/ext/dramsim3/PIMony",
                       "/home/pimony/gem5/build/RISCV/gem5.opt",
                       f"--outdir={mounted_out}",
                       "--debug-flags=Exec,Cache,CoherentXBar,PseudoInst,DRAMsim3",
                       f"--debug-file={mounted_out}/debug.log",
                       "/bench/run_config.py", "/home/pimony/gem5/configs/pimony/fs_bert.py",
                       cpu, str(mount_results / "bin" / f"fence_cmp_m{mode}_n{n}")]
                (out / "command.json").write_text(json.dumps(cmd, indent=2) + "\n")
                print(f"Running {case}", flush=True)
                with (out / "run.log").open("w") as log:
                    try:
                        result = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, timeout=120)
                        code = result.returncode
                    except subprocess.TimeoutExpired:
                        subprocess.run(["docker", "stop", "--time", "1", name], stdout=log, stderr=log)
                        code = 124
                (out / "exit_code.txt").write_text(f"{code}\n")
                print(f"Finished {case}: exit={code}", flush=True)


if __name__ == "__main__":
    main()
