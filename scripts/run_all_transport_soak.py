"""Run current-version TCP/UDP/CLI/SSH/SFTP and two physical ADB slots, excluding serial."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import signal
import subprocess
import sys
import time
import psutil


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=Path("build-locked"))
    parser.add_argument("--adb", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--seconds", type=int, default=3600)

    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    build, adb, output = args.build.resolve(), args.adb.resolve(), args.output.resolve()

    if args.seconds <= 0 or not output.is_relative_to(repo / "artifacts"):
        parser.error("positive seconds and an output directory under artifacts are required")

    if (output / "manifest.json").exists():
        parser.error("choose a new output directory to preserve earlier evidence")

    output.mkdir(parents=True, exist_ok=True)
    subprocess.run(["cmake", "--build", str(build), "--parallel", "4"], check=True)
    subprocess.run(["ctest", "--test-dir", str(build), "--output-on-failure"], check=True)
    subprocess.run(
        [sys.executable, "scripts/test_ssh_local.py", "--build", str(build), "--user-server"],
        cwd=repo,
        check=True,
    )

    devices = subprocess.check_output([str(adb), "devices"], text=True)
    serials = [
        line.split()[0] for line in devices.splitlines()[1:] if re.search(r"\sdevice$", line)
    ]

    if len(serials) != 2:
        raise RuntimeError("This suite requires exactly two online physical Android devices")

    (output / "devices.private.json").write_text(json.dumps(serials))

    native = build / "libcommunicationdriver_native.dylib"

    if not native.exists():
        native = build / "libcommunicationdriver_native.so"

    def digest(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()

    manifest = {
        "started_at": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
        "platform": platform.platform(),
        "duration_requested_s": args.seconds,
        "git_head": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=repo, text=True
        ).strip(),
        "serial_excluded": True,
        "native_sha256": digest(native),
        "binary_sha256": {
            p.name: digest(p)
            for p in [build / "cd_transport_soak_tests", build / "cd_adb_hardware_tests"]
        },
        "source_sha256": {
            name: digest(repo / name)
            for name in [
                "CMakeLists.txt",
                "cmake/Dependencies.cmake",
                "cmake/PatchLibssh2.cmake",
                *[
                    str(path.relative_to(repo))
                    for path in sorted((repo / "src").rglob("*"))
                    if path.suffix in {".cpp", ".hpp"}
                ],
                "tests/transport_soak_tests.cpp",
                "tests/adb_hardware_tests.cpp",
                "scripts/test_ssh_local.py",
                str(Path(__file__).resolve().relative_to(repo)),
            ]
        },
        "loads": {
            "tcp": {"commands": 32, "bulk": 2, "cancel_timeout": 4, "drivers": 4},
            "udp": {"commands": 32, "bulk": 2, "cancel_timeout": 4, "drivers": 4},
            "cli": {"commands": 32, "streams": 2, "cancel_timeout": 4, "drivers": 4},
            "ssh": {"commands": 32, "streams": 2, "sftp": 4, "cancel_timeout": 4, "drivers": 4},
            "adb_per_slot": {
                "commands": 32,
                "transfers": 4,
                "streams": 3,
                "cancel_reopen": 4,
                "drivers": 4,
            },
        },
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")

    def device(serial, command):
        return subprocess.run(
            [str(adb), "-s", serial, "shell", "-T", command],
            capture_output=True,
            text=True,
            timeout=20,
        )

    roots, jobs, handles = [], {}, []
    awake = (
        subprocess.Popen(["caffeinate", "-i", "-s", "-w", str(os.getpid())])
        if sys.platform == "darwin"
        else None
    )
    failure = None
    load_samples = []

    try:
        for n, serial in enumerate(serials):
            original = device(serial, "id -u").stdout.strip()
            item = {"slot": n + 1, "original_uid": original, "root_requested": False}
            roots.append(item)

            if device(serial, "dmesg >/dev/null").returncode:
                item["root_requested"] = True
                subprocess.run(
                    [str(adb), "-s", serial, "root"],
                    capture_output=True,
                    text=True,
                    check=True,
                    timeout=20,
                )
                subprocess.run([str(adb), "-s", serial, "wait-for-device"], check=True, timeout=25)

            item["test_uid"] = device(serial, "id -u").stdout.strip()
            follow = device(serial, "timeout 1 dmesg -w >/dev/null")

            if follow.returncode != 124:
                raise RuntimeError(f"slot {n+1}: real dmesg follow unavailable")

        (output / "root-state.json").write_text(json.dumps(roots, indent=2) + "\n")

        def sampled(name):
            return [
                sys.executable,
                str(repo / "scripts/run_stress.py"),
                "--output",
                str(output / name / "run.json"),
                "--timeout",
                str(args.seconds + 300),
                "--idle-timeout",
                "120",
                "--interval",
                "5",
                "--process-tree",
                "--library-dir",
                str(build),
                "--",
                str(build / "cd_transport_soak_tests"),
                name,
                str(args.seconds),
                str(output / name / "files"),
                "32",
            ]

        commands = {name: sampled(name) for name in ("tcp", "udp", "cli")}
        commands["ssh"] = [
            sys.executable,
            str(repo / "scripts/test_ssh_local.py"),
            "--build",
            str(build),
            "--user-server",
            "--soak-seconds",
            str(args.seconds),
            "--output",
            str(output / "ssh"),
        ]
        commands["adb"] = [
            sys.executable,
            str(repo / "scripts/test_adb_hardware.py"),
            "--adb",
            str(adb),
            "--build",
            str(build),
            "--output",
            str(output / "adb"),
            "--seconds",
            str(args.seconds),
            "--command-workers",
            "32",
            "--transfer-workers",
            "4",
            "--cancel-workers",
            "4",
            "--drivers-per-slot",
            "4",
            "--sampler-python",
            sys.executable,
        ]

        for name, command in commands.items():
            stream = (output / (name + ".controller.log")).open("w")
            handles.append(stream)

            process = subprocess.Popen(
                command, cwd=repo, stdout=stream, stderr=subprocess.STDOUT, start_new_session=True
            )
            jobs[name] = {
                "process": process,
                "root": psutil.Process(process.pid),
                "observed": set(),
                "exit_code": None,
            }

        started = time.monotonic()
        next_report = 0

        while any(job["process"].poll() is None for job in jobs.values()):
            elapsed = time.monotonic() - started

            if digest(native) != manifest["native_sha256"]:
                raise RuntimeError("native library changed while tests were running")

            status = {}

            for name, job in jobs.items():
                process = job["process"]
                code = process.poll()
                job["exit_code"] = code

                try:
                    job["observed"].update(job["root"].children(recursive=True))
                except psutil.NoSuchProcess:
                    pass

                job["observed"] = {p for p in job["observed"] if p.is_running()}

                if name == "adb":
                    slots = [
                        {"slot": n + 1, "adb_processes": 0, "transfers": 0, "logcat": 0, "dmesg": 0}
                        for n in range(len(serials))
                    ]
                    for child in job["observed"]:
                        try:
                            child_args = child.cmdline()
                        except (psutil.NoSuchProcess, psutil.AccessDenied):
                            continue

                        if "-s" not in child_args:
                            continue

                        index = child_args.index("-s") + 1

                        if index >= len(child_args) or child_args[index] not in serials:
                            continue

                        slot = slots[serials.index(child_args[index])]
                        slot["adb_processes"] += 1

                        if "push" in child_args or "pull" in child_args:
                            slot["transfers"] += 1

                        if child_args[-1].startswith("logcat "):
                            slot["logcat"] += 1

                        if child_args[-1] == "dmesg -w":
                            slot["dmesg"] += 1
                    load_samples.append({"elapsed_s": round(elapsed, 3), "slots": slots})

                log_path = output / name / "run.log"
                log = log_path.read_text(errors="replace") if log_path.exists() else ""
                lines = log.splitlines()
                status[name] = {"exit_code": code, "progress": lines[-3:]}

                if code not in (None, 0) or any(
                    line.startswith(("FAIL ", "CLEANUP_FAIL ")) for line in lines
                ):
                    # Let a failing workload save its result and close resources before aborting peers.
                    if code is None:
                        try:
                            process.wait(timeout=30)
                        except subprocess.TimeoutExpired:
                            pass
                    raise RuntimeError(name + " workload failed; inspect preserved evidence")

            (output / "progress.json").write_text(
                json.dumps({"elapsed_s": round(elapsed, 3), "status": status}, indent=2) + "\n"
            )

            if elapsed >= next_report:
                print(json.dumps({"elapsed_s": round(elapsed, 3), "status": status}), flush=True)
                next_report = elapsed + 60
                (output / "slot-process-samples.json").write_text(
                    json.dumps(load_samples, indent=2) + "\n"
                )

            if elapsed > args.seconds + 360:
                raise RuntimeError("suite watchdog exceeded")

            time.sleep(5)

        for name, job in jobs.items():
            job["exit_code"] = job["process"].returncode

            if job["exit_code"]:
                raise RuntimeError(name + " controller exited with failure")

            result = json.loads((output / name / "run.json").read_text())

            if (
                result["exit_code"]
                or result["timed_out"]
                or result["idle_timed_out"]
                or result["surviving_observed_children"]
            ):
                raise RuntimeError(name + " sampler rejected workload")
            if result["artifact_sha256"].get(str(native)) != manifest["native_sha256"]:
                raise RuntimeError(name + " ran with a different native library")
    except BaseException as error:
        failure = str(error)
    finally:
        survivors = []
        (output / "slot-process-samples.json").write_text(json.dumps(load_samples, indent=2) + "\n")

        for name, job in jobs.items():
            process = job["process"]

            if process.poll() is None:
                try:
                    job["observed"].update(job["root"].children(recursive=True))
                except psutil.NoSuchProcess:
                    pass
                try:
                    os.killpg(process.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    try:
                        os.killpg(process.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                    process.wait()

            _, alive = psutil.wait_procs(list(job["observed"]), timeout=5)

            for child in alive:
                try:
                    child_name = child.name()
                except psutil.NoSuchProcess:
                    continue
                survivors.append({"workload": name, "pid": child.pid, "name": child_name})
                try:
                    child.kill()
                except psutil.NoSuchProcess:
                    pass

            psutil.wait_procs(alive, timeout=5)

        (output / "controller-survivors.json").write_text(json.dumps(survivors, indent=2) + "\n")

        if survivors and not failure:
            failure = "observed test controller descendants survived"

        # Cleanup only UUID directories emitted by the physical-device runner.
        adb_meta = output / "adb" / "metadata.json"

        if adb_meta.exists():
            try:
                remote = json.loads(adb_meta.read_text())["remote_root"]
                if not re.fullmatch(r"/data/local/tmp/cd-hardware-[a-f0-9]+", remote):
                    raise RuntimeError("invalid ADB fixture path; refusing cleanup")
                for serial in serials:
                    if device(
                        serial, "rm -rf '" + remote + "'; test ! -e '" + remote + "'"
                    ).returncode:
                        raise RuntimeError("ADB temporary directory cleanup failed")
            except Exception as error:
                failure = failure or str(error)

        for item in roots:
            serial = serials[item["slot"] - 1]
            try:
                if item["root_requested"] and item["original_uid"] == "2000":
                    subprocess.run(
                        [str(adb), "-s", serial, "unroot"],
                        capture_output=True,
                        text=True,
                        check=True,
                        timeout=20,
                    )
                    subprocess.run(
                        [str(adb), "-s", serial, "wait-for-device"], check=True, timeout=25
                    )
                item["restored_uid"] = device(serial, "id -u").stdout.strip()
                if item["restored_uid"] != item["original_uid"]:
                    raise RuntimeError("original ADB uid not restored")
            except Exception as error:
                item["restore_error"] = str(error)
                failure = failure or str(error)

        (output / "root-state.json").write_text(json.dumps(roots, indent=2) + "\n")

        for stream in handles:
            stream.close()

        if awake:
            awake.terminate()
            awake.wait()

    summary = {
        "passed": failure is None,
        "failure": failure,
        "exit_codes": {name: job["process"].returncode for name, job in jobs.items()},
        "native_sha256": manifest["native_sha256"],
    }
    (output / "suite-result.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary), flush=True)

    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
