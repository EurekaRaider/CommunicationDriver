# C++23 consumers

Include `<communicationdriver/communicationdriver.hpp>`; namespace `comm`.
Link the native library; the wrapper is header-only. See [integration.md](integration.md).

## Runnable CLI smoke example

```cpp
#include <communicationdriver/communicationdriver.hpp>
#include <iostream>

int main() {
 try {
  comm::Driver driver;
  comm::CliOptions options;
  options.execution_mode = comm::ExecutionMode::OneShot;
  auto connection = driver.create_cli(options);
  connection.open();
  auto pending = connection.execute_async("echo READY", comm::Check::contains("READY"));
  auto result = pending.get();
  std::cout << result.response;
  connection.close();
  return result.check && result.check->passed && result.exit_code == 0 ? 0 : 1;
 } catch(const comm::CommunicationError& error) {
  std::cerr << error.what() << '\n';
  return 2;
 }
}
```

## Factory and option names

Driver has create_tcp(TcpOptions), create_udp(UdpOptions),
create_serial(SerialOptions), create_ssh(SshOptions), create_adb(AdbOptions),
create_cli(CliOptions). Every factory returns Connection. Common options:
encoding, new_line, receive_until, connect_timeout, io_timeout,
execution_timeout, close_timeout, max_response_bytes. Durations use
`comm::Duration` (`std::chrono::milliseconds`).

| Options | Backend fields |
| --- | --- |
| TcpOptions | host, port |
| UdpOptions | remote_host, remote_port, local_host, local_port |
| SerialOptions | port_name, baud_rate, data_bits, parity, stop_bits, flow_control |
| SshOptions | host, port, user_name, auth, host_key, shell, execution_mode, session_command |
| AdbOptions | device_serial, executable_path, server_host, server_port, execution_mode, session_command |
| CliOptions | shell, executable_path, arguments (vector), working_directory, environment (map), execution_mode |

SSH uses `comm::SshAuth::private_key(path, passphrase)` or `password(secret)`;
trust uses `comm::HostKey::known_hosts(path)` or `sha256(fingerprint)`.
Use caller-provided values. ExecutionMode: Persistent/OneShot. ShellKind:
Default, Cmd, WindowsPowerShell, Pwsh, Sh, Bash, Zsh.

## Framed TCP and byte operations

The first helper assumes LF commands and END replies. The binary helper takes
the actual reply byte count from the caller. Replace framing with the real
protocol; do not infer it from arbitrary data.

```cpp
comm::ExecutionResult<std::string> query_tcp(comm::Driver& driver,
 std::string host, uint16_t port, std::stop_token stop = {}) {
 comm::TcpOptions options;
 options.host = std::move(host);
 options.port = port;
 options.receive_until = comm::ReceiveUntil::delimiter("END");
 auto connection = driver.create_tcp(options);
 connection.open({}, stop);
 comm::CallOptions call;
 call.timeout = comm::Duration(5000);
 auto result = connection.execute("STATUS\n", comm::Check::contains("READY"), call, stop);
 connection.close();
 return result;
}

comm::ExecutionResult<comm::Bytes> query_bytes(const comm::Connection& connection,
 std::span<const std::byte> request, uint64_t reply_bytes, std::stop_token stop = {}) {
 comm::CallOptions call;
 call.receive_until = comm::ReceiveUntil::bytes(reply_bytes);
 call.timeout = comm::Duration(5000);
 return connection.execute(request, {}, call, stop);
}
```

ReceiveUntil factories: bytes(n), characters(n), delimiter(string/span,
include=false), line(new_line), pattern(regex), idle(Duration), message(),
available(). Checks: Check::contains, equals, regex. Text overloads use
string_view; binary overloads use span<const byte>. Bytes is vector<byte>.
Keep consumer code off internal `comm::detail` helpers.

## Async, cancellation, and matching

`execute_async(command, optional_check, CallOptions, stop_token)` returns
Operation<ExecutionResult<T>>; other I/O methods have `_async` partners.
Operation is movable, not copyable. get() consumes its result once;
wait_for(Duration) only observes, and false does not cancel. cancel() requests
cancellation; use get() afterwards to observe completion. Dropping the handle
does not cancel. std::stop_source supplies a stop_token to the final parameter.

`connection.execute_until(command, comm::Check::contains("READY"), call, stop)`
returns early on matching. Only contains/regex are accepted; raw streams need
a reliable cleanup boundary for safe reuse.

ExecutionResult fields: response, standard_error, optional check, optional
exit_code, completion_reason, elapsed. Check failure is a result. Catch
CommunicationError and inspect its public info: code (CD_*), message, backend,
native_code, partial_text, partial_bytes, partial_file, optional confirmed_progress.
Cancellation also throws CommunicationError with info.code == CD_CANCELLED.
If state() is State::NeedsRecovery, close()/open() before continuing; no default
command replay. Close without a cancelled token. Driver/Connection use RAII;
explicit close is useful for orderly shutdown and recovery.

## Batch and transfer helpers

Inputs are already-open connections from one Driver. Supply protocol-appropriate
commands and unique keys. Batch::run is synchronous; run_async returns
Operation<BatchResult>. Get each batch result before its dependent stage.

```cpp
comm::BatchResult run_stage(comm::Connection first, std::string first_command,
 comm::Connection second, std::string second_command) {
 std::vector<comm::BatchItem> items{
  comm::BatchItem::execute("first/status", first, first_command, comm::Check::contains("READY")),
  comm::BatchItem::execute("second/status", second, second_command, comm::Check::contains("READY"))
 };
 comm::BatchOptions options;
 options.max_concurrency = 32;
 return comm::Batch::run(items, options);
}

comm::TransferResult upload_file(const comm::Connection& connection,
 std::string_view local_path, std::string_view remote_path) {
 if(!(connection.capabilities() & CD_CAP_UPLOAD))
  throw std::invalid_argument("Connection cannot upload");
 comm::TransferOptions options;
 options.overwrite = false;
 options.timeout = comm::Duration(30000);
 return connection.upload(local_path, remote_path, options);
}
```

Inspect BatchResult.items and each key/state/succeeded/error; inspect process
exit_code separately. download(remote_path, local_path, options, stop) reverses
the direction. File failures can leave partial files in info.partial_file.

Low-level methods: read_bytes, read_string, read_line, read_exactly_bytes(n),
read_exactly_string(n), read_until_bytes(rule), read_until_string(rule),
write_bytes, write_string, write_line, close_input; all have `_async` partners.
CallOptions.read_stream/check_stream use Stream::StandardOutput/StandardError
on supporting backends. Do not mix reads with an Execute transaction that owns
the same response.
