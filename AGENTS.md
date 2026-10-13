# Repository Guidelines

## Project Structure & Module Organization

`src/` contains the shared C++23 communication core. `include/communicationdriver/` exposes the stable C ABI and C++ wrapper; `bindings/dotnet/CommunicationDriver/` provides C# bindings targeting `net48` and `net10.0`. Tests live in `tests/`; standalone consumers in `examples/`; packaging and integration helpers in `scripts/`. Consult `docs/approved-plan.md`, `docs/api.md`, and `docs/validation.md` for contracts and acceptance boundaries. Generated packages and evidence fixtures belong in ignored `artifacts/`.

## Build, Test, and Development Commands

Require CMake 3.28+, Ninja, a compiler supporting the required C++23 features, and .NET SDK 10. Unix builds also require OpenSSL development files. See `.github/workflows/ci.yml` for platform toolchain setup.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
cmake --install build --prefix install
dotnet build bindings/dotnet/CommunicationDriver -c Release
```

Commands configure, build, test, install native files, and compile managed bindings. Dependencies use pinned versions and SHA256 checks.

## Coding Style & Architecture

Match existing one-space indentation in native and C# sources. Use `cd_` names for C exports, `snake_case` for native functions, and `PascalCase` with `Async` suffixes for C# asynchronous methods. Keep shared behavior in the native core and wrappers thin. Preserve actual asynchronous I/O, handle ownership, and C ABI layout; never pass STL objects or exceptions across the ABI. Keep changes focused and run `git diff --check`.

## Testing Guidelines

CTest runs custom assertion executables, including `native_behavior`, `cpp_lifetime`, and Unix `adb_simulated`. Follow `*_tests.cpp` naming and add regression cases for changed behavior, cancellation, and lifetime boundaries. Run `python3 scripts/test_ssh_local.py --build build` for Unix OpenSSH/SFTP integration; it requires local SSH tools and passwordless sudo. No numeric coverage threshold is configured. Distinguish loopback tests, virtual serial ports, simulated ADB, and real hardware; record unavailable hardware as unverified.

## Commit & Pull Request Guidelines

Use imperative subjects, following history: `Retain process state until pending exit waits complete`. PRs should explain the problem, behavior, relevant issues, and validation commands/results with platform scope. Update API documentation and consumers when contracts change; require passing platform CI before merging.

## Security & Agent Workflow

Never commit credentials, test keys, or device identifiers. Preserve explicit SSH host-key trust. When `.codegraph/` exists, use `codegraph_explore` with this repository's `projectPath`, or `codegraph explore "<symbol or question>"`, before searching or reading code; do not create an index automatically. For upstream library/API/CLI documentation questions, use Context7: resolve with `npx ctx7@latest library`, then fetch with `npx ctx7@latest docs`; run requests outside the default sandbox.
