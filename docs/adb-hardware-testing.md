# Physical ADB validation

These opt-in tests use CommunicationDriver's C++ and C# public interfaces against at least two online Android devices. They are separate from `adb_simulated` and are never registered as automatic CTest cases. Keep both devices attached, authorized and awake until completion.

## Native functional and mixed-load tests

Build the repository normally, then supply the official Android Platform Tools `adb` executable:

```sh
python3 scripts/test_adb_hardware.py --adb /absolute/path/to/adb --build build-locked --seconds 600 --output artifacts/adb-real/local
```

For high load on every physical slot, use:

```sh
python3 scripts/test_adb_hardware.py --adb /absolute/path/to/adb --build build-locked --seconds 600 --command-workers 32 --transfer-workers 4 --cancel-workers 4 --drivers-per-slot 4 --output artifacts/adb-real/high-slots
```

Each slot then has its own four Driver instances, 32 command workers (24 Persistent and eight OneShot), four transfer workers, logcat/dmesg/continuous readers and four cancel/reopen workers. Slot progress, final counts and short-command-pair latency are reported separately. Each transfer worker owns distinct filenames, and logcat markers include a fresh run identifier.

The default is ten minutes of mixed load after functional checks. `--seconds 0` runs functional checks only. The script enumerates online devices and binds every connection to a specific serial. It uses only read-only diagnostics, a dedicated test log tag and unique `/data/local/tmp/cd-hardware-*` directories. Remote test directories are removed on normal completion or failure; a lost device connection may prevent cleanup and is reported. Existing application files, logs and system configuration are not intentionally modified.

For process-tree resource sampling, use a Python environment with `psutil==7.0.0` installed and pass `--sampler-python /absolute/path/to/venv/bin/python`. Without it, the existing sampler records basic platform resources. On macOS the wrapper uses `caffeinate -i -s` during execution. Explicitly closing the lid can still interrupt the test.

| Area | Assertions |
| --- | --- |
| Short commands | Persistent and OneShot execution; version properties; `id`, `uname`, `pwd`, `ls`, `df`, `pm`, `dumpsys`, pipelines, no newline and empty output |
| Output and results | UTF8, binary NUL bytes, independent stdout/stderr, nonzero exit codes, normal failed checks, early matching and tail ownership |
| Ordering and binding | 32 queued commands, distinct files at the same path on each device, cross-device Batch results |
| Push/pull | 0 B, 1 B, 4,095 B, 64 KiB, 1 MiB and 16 MiB; byte equality and reported sizes; spaces, Chinese and quotes in paths |
| File failures | Exclusive installation, refusing replacement, directories and missing files |
| Lifecycle | Timeout partial output, cancellation, response limits, sixteen concurrent closes and explicit reopen |
| Mixed load per device | Eight command workers (six Persistent, two OneShot), one push/pull worker, one real filtered logcat reader and producer, one continuous output reader, one dmesg reader (or permission-error worker), one cancel/reopen worker |

A connection serializes its own transactions. Keep a long-running stream on a separate connection from short commands and transfers. Multiple connections still bind to the same device. The mixed load checks output identity and file content; it does not only count successful process launches.

The dmesg worker probes existing permissions. If reading is permitted, it opens a real `dmesg -w` session, consumes kernel-log bytes and cancels any outstanding read at the end. Otherwise it checks independent stderr and exit status while the other workers continue. The test never changes device privileges itself. Use `--soak-only` to repeat mixed load without repeating functional cases, for example after a separately authorized device-permission change. A separate continuous shell output stream does not substitute for successful dmesg acceptance.

## Managed interface and active transfer cancellation

```sh
dotnet build tests/dotnet/AdbHardware -c Release
```

Run `AdbHardware.dll` with arguments `<adb> <local-artifacts-root> <serial> <serial>...`, and make the native library available using `DYLD_LIBRARY_PATH` on macOS, `LD_LIBRARY_PATH` on Linux or `PATH` on Windows. Device serials belong in local ignored evidence only.

The managed test runs both devices concurrently. It covers package/software versions, binary output, early matching, 64 queued operations, real logcat with a concurrent short command, SHA256 file verification, command cancellation and concurrent close/reopen. For both push and pull, it waits until a destination contains data before requesting cancellation. It checks the reported partial path, explicit recovery, and that a partial download stops changing after Close. Each device uses a unique remote directory and removes it in `finally`.

## Evidence and scope

The wrapper writes metadata, raw logs, five-second resource samples, a summary and cleanup results under the selected ignored `artifacts/` directory. Raw records contain private device serials and local paths; do not commit or publish them. The public summary belongs under `docs/evidence/` and must omit serials and raw device logs.

The test program never performs root/unroot, remount, reboot, install/uninstall, factory reset, log clearing, deliberate USB disconnection or server restart. For the 2026-10-17 kernel-log follow-up, the user separately authorized `adb root`; both devices were tested with their existing userdebug build, then their original ADB uid was restored. Refer to the dated evidence for that action. Those state-changing and transport-loss scenarios need separate acceptance work. Results apply to the actual host, devices and duration recorded; they do not establish other Android versions, Windows/Linux physical USB behavior or overnight stability.

The `concurrent_process_launch` CTest is hardware-independent: eight independent Drivers repeatedly start OneShot CLI children. It reproduces the multi-runtime pre-exec deadlock found during the managed hardware follow-up. The POSIX library build disables Boost.Process fork notifications because these children immediately exec and never run the inherited Asio contexts. See the [Asio thread-safety constraint](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference/io_context/notify_fork.html) and the pinned Boost.Process default launcher implementation for the supported compile-time switch.
