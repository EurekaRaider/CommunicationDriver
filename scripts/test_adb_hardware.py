"""Opt-in physical ADB validation; creates and removes only its own temporary files."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
import uuid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", type=Path, default=shutil.which("adb"))
    parser.add_argument("--build", type=Path, default=Path("build-locked"))
    parser.add_argument("--output", type=Path, default=Path("artifacts/adb-real"))
    parser.add_argument("--seconds", type=int, default=600)
    parser.add_argument(
        "--soak-only",
        action="store_true",
        help="skip functional cases; retain binding and mixed-load checks",
    )
    parser.add_argument(
        "--allow-single-device",
        action="store_true",
        help="run one remaining device; does not validate cross-device isolation",
    )
    parser.add_argument(
        "--offline-device", help="expect this separately bound device to be unavailable"
    )
    parser.add_argument("--command-workers", type=int, default=8)
    parser.add_argument("--transfer-workers", type=int, default=1)
    parser.add_argument("--cancel-workers", type=int, default=1)
    parser.add_argument("--drivers-per-slot", type=int, default=1)
    parser.add_argument(
        "--sampler-python",
        type=Path,
        help="Python with psutil installed; enables child-process tracking",
    )

    args = parser.parse_args()

    if not args.adb or not args.adb.is_file() or args.seconds < 0:
        parser.error("supply --adb and nonnegative --seconds")

    if (
        min(args.command_workers, args.transfer_workers, args.cancel_workers, args.drivers_per_slot)
        < 1
    ):
        parser.error("worker and driver counts must be positive")

    repo = Path(__file__).resolve().parents[1]
    adb, build, output = args.adb.resolve(), args.build.resolve(), args.output.resolve()

    # Evidence contains private serials. Require the repository ignored artifacts directory.
    if not output.is_relative_to(repo / "artifacts"):
        parser.error("--output must be under the repository artifacts directory")

    output.mkdir(parents=True, exist_ok=True)

    executable = build / (
        "cd_adb_hardware_tests.exe" if sys.platform == "win32" else "cd_adb_hardware_tests"
    )
    subprocess.run(
        ["cmake", "--build", str(build), "--target", "cd_adb_hardware_tests", "--parallel", "4"],
        check=True,
    )

    def call(serial, command):
        return subprocess.run(
            [str(adb), "-s", serial, "shell", "-T", command],
            capture_output=True,
            text=True,
            timeout=20,
        )

    devices_text = subprocess.check_output([str(adb), "devices"], text=True)
    serials = [
        line.split()[0] for line in devices_text.splitlines()[1:] if re.search(r"\sdevice$", line)
    ]

    if len(serials) < (1 if args.allow_single_device else 2):
        raise SystemExit("Insufficient online physical Android devices")

    remote = "/data/local/tmp/cd-hardware-" + uuid.uuid4().hex
    metadata = {
        "started_at": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
        "git_head": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=repo, text=True
        ).strip(),
        "adb_version": subprocess.check_output([str(adb), "version"], text=True),
        "devices": [],
        "duration_requested_s": args.seconds,
        "remote_root": remote,
        "binary_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
    }
    metadata["source_sha256"] = {
        str(path.relative_to(repo)): hashlib.sha256(path.read_bytes()).hexdigest()
        for path in (repo / "tests/adb_hardware_tests.cpp", Path(__file__).resolve())
    }
    metadata["per_slot_load"] = {
        name: getattr(args, name)
        for name in ("command_workers", "transfer_workers", "cancel_workers", "drivers_per_slot")
    }

    for n, serial in enumerate(serials):
        model = call(serial, "getprop ro.product.model").stdout.strip()
        version = call(serial, "getprop ro.build.version.release").stdout.strip()
        dmesg = call(serial, "dmesg")
        metadata["devices"].append(
            {
                "label": f"device-{n+1}",
                "model": model,
                "android": version,
                "dmesg_exit": dmesg.returncode,
                "dmesg_denied": "not permitted" in dmesg.stderr,
            }
        )

    (output / "devices.private.json").write_text(json.dumps(serials))
    (output / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")

    sampler_python = str(args.sampler_python.absolute()) if args.sampler_python else sys.executable

    if args.sampler_python:
        subprocess.run([sampler_python, "-c", "import psutil"], check=True)

    command = [
        sampler_python,
        str(repo / "scripts/run_stress.py"),
        "--output",
        str(output / "run.json"),
        "--timeout",
        str(args.seconds + 240),
        "--idle-timeout",
        "120",
        "--interval",
        "5",
        "--library-dir",
        str(build),
    ]

    if args.sampler_python:
        command.append("--process-tree")

    command += [
        "--",
        str(executable),
        str(adb),
        str(output / "files" / uuid.uuid4().hex),
        remote,
        str(args.seconds),
        *serials,
    ]

    if args.soak_only:
        command.append("--soak-only")

    if args.allow_single_device:
        command.append("--allow-single-device")

    if args.offline_device:
        command += ["--offline-device", args.offline_device]

    for name in ("command_workers", "transfer_workers", "cancel_workers", "drivers_per_slot"):
        command += ["--" + name.replace("_", "-"), str(getattr(args, name))]

    if sys.platform == "darwin" and shutil.which("caffeinate"):
        command = ["caffeinate", "-i", "-s", *command]

    cleanup = []

    try:
        completed = subprocess.run(command, cwd=repo)
    finally:
        for n, serial in enumerate(serials):
            result = call(serial, "rm -rf '" + remote + "'; test ! -e '" + remote + "'")
            cleanup.append({"device": n + 1, "temporary_directory_removed": result.returncode == 0})
        (output / "cleanup.json").write_text(json.dumps(cleanup, indent=2) + "\n")

    log_path = output / "run.log"
    log = log_path.read_text() if log_path.exists() else ""
    summary = {
        "exit_code": completed.returncode,
        "devices": metadata["devices"],
        "cleanup": cleanup,
        "events": [
            line
            for line in log.splitlines()
            if line.startswith(
                (
                    "PASS ",
                    "FAIL ",
                    "DMESG ",
                    "DMESG_STREAM ",
                    "SLOT ",
                    "SLOT_SUMMARY ",
                    "LATENCY ",
                    "SUMMARY ",
                )
            )
        ],
    }
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2))

    return completed.returncode or (
        0 if all(item["temporary_directory_removed"] for item in cleanup) else 1
    )


if __name__ == "__main__":
    raise SystemExit(main())
