---
name: communicationdriver
description: Write or fix C, C++23, and C# consumers of CommunicationDriver for TCP, UDP, serial, SSH/SFTP, ADB, and local CLI. Use for connection setup, response framing, Execute/ExecuteUntil, async cancellation, file transfer, batches, and native/NuGet integration. Not a guide to changing the library internals.
---

# CommunicationDriver

Use the shared native core through its public API. All six factories return
the same Connection type; backend differences belong in creation options,
response boundaries, and capabilities.

## Start here

1. Identify the consumer language, installed library version, OS/process
   architecture, backend, and protocol's actual response boundary. Inspect
   existing dependency configuration before adding a dependency.
2. Read only the relevant reference:
   - [C# / .NET](references/csharp.md): Task, CancellationToken, exceptions, batch.
   - [C++23](references/cpp.md): RAII, Operation, stop_token, batch.
   - [C99 ABI](references/c.md): initialization, status, ownership, callbacks.
   - [Build and packaging](references/integration.md): CMake, NuGet, native loading.
3. Configure → create → open → operate/check → close → release. Create copies
   and validates configuration; it does not connect. Keep the Driver alive for
   the intended session. A normally closed connection can reopen; a closed
   Driver cannot create or reopen connections.
4. Compile against the consumer's actual headers/assemblies. Run the CLI smoke
   example without hardware, then exercise the selected backend against the
   user's service or an isolated fixture where available.

These references travel with the library and work without a source checkout
or agent-specific tools. If a matching checkout is available, authoritative
contracts are `include/communicationdriver/communicationdriver.h`,
`include/communicationdriver/communicationdriver.hpp`,
`bindings/dotnet/CommunicationDriver/Contracts.cs`, and `docs/api.md`.
Prefer that version's declarations over examples from a different release.

## Choose completion before writing execution code

| Need | API / rule |
| --- | --- |
| Send once, collect a complete response, then validate | Execute + optional Check |
| Return as soon as output matches | ExecuteUntil + Contains or Regex; Equals is invalid |
| TCP/serial or a response spanning UDP datagrams | Explicit protocol byte/character length, delimiter, or pattern |
| One UDP datagram is the entire response | ReceiveUntil.Message |
| Ordinary shell command | CLI/SSH/ADB managed command completion |
| Custom interactive program/session | Explicit protocol boundary; no PTY/TUI emulation |
| Low-level available data | Read / Available; this does not establish a complete response |

Never guess a response length or delimiter. If framing is unspecified, ask for
it while completing independent setup. Do not add fixed 50/100 ms sleeps or
silently select Idle. Idle is an explicit silence rule that can finish during
an application pause; raw-stream Execute with Idle enters NeedsRecovery.
Timeout limits waiting; it is not a successful response boundary.

Execute does not append a newline to raw-stream commands. Include protocol
terminators in the input, or use WriteLine for low-level writes. Delimiter
strips its delimiter by default; request inclusion if the check needs it.
Text defaults to UTF-8, optionally ASCII/GB18030 on the wire; C/C++ text values
remain UTF-8. Bytes bypass text encoding. Character counts are Unicode scalar
values, not UTF-8 bytes or UTF-16 code units.

## Results and recovery

- A normal check mismatch is a successful operation with `Check.Passed=false`
  (C++ `result.check->passed`), not an exception. With no check, the check result
  may be absent. Inspect ExitCode separately: operation success and
  Batch.Succeeded do not imply exit code zero.
- Checks are case-sensitive, do not trim or remove ANSI sequences, and use RE2
  syntax for Regex/Pattern. Lookaround and backreferences are unsupported.
- ExecuteUntil sends once. Early matching can return without ExitCode. Managed
  commands and reliably framed streams drain remaining output before the next
  queued transaction; do not read concurrently to steal that output. A raw
  stream without a reliable cleanup boundary enters NeedsRecovery after matching.
- Timeout/cancellation includes time in the connection queue. Preserve partial
  text, bytes, and reported file paths. Cancellation is a request; await/get
  completion before assuming the operation has stopped.
- NeedsRecovery requires close then open before continuing. Do not silently
  resend or reconnect: the command may already have taken effect. Closing does
  not roll back remote effects or guarantee remote process exit.

## Backend and concurrency rules

- TCP is a client. UDP has a fixed peer; Open does not prove remote availability.
  UDP does not repair loss, duplication, or reordering.
- Serial needs the real port and line settings, plus protocol framing.
- SSH requires authentication and explicit known_hosts or SHA256 host-key trust.
  Never invent a fingerprint or add trust-all. Read credentials from the
  caller's configuration; do not hardcode or print them.
- ADB requires caller-supplied Platform Tools and a fixed DeviceSerial. All
  operations stay on that device; closing does not kill the ADB server.
- CLI Persistent preserves shell state; OneShot isolates commands. With a direct
  ExecutablePath and Arguments, Execute's command is program stdin, not a shell
  command line. The library closes stdin for a OneShot program.
- SessionCommand is for SSH/ADB custom persistent sessions and cannot be combined
  with OneShot. Separate stderr applies only to CLI/SSH/ADB.
- Upload/Download are single-file operations on SSH/ADB; check capabilities for
  optional operations. Overwrite defaults to true. Failure may leave a partial
  or staged file reported in ErrorInfo.PartialFile; cleanup is a caller decision.
- Each connection serializes operations. Use separate connections for parallel
  commands. Batch takes already-open connections from one Driver and unique
  caller keys; it does not create connections. Default concurrency is 32, not a
  connection-count limit. Results retain input order. Await each batch before
  its dependent stage. Cancellation preserves completed items and reports
  unadmitted items as NotStarted.

Stay on the public API; do not invent backend-specific everyday methods such
as SendTo/ReceiveFrom, Query, SendExpect, or CreateMultiUp. Report compilation,
loopback/simulation, CI, and real-device results according to what was exercised.
