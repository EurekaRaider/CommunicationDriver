# C# / .NET consumers

Namespace: `CommunicationDriver`; targets: `net48` and `net10.0`. Use Async
methods directly; native async I/O makes Task.Run wrappers unnecessary.
IDisposable is supported; IAsyncDisposable is not part of this API.

## Runnable CLI smoke example

Put this in Program.cs of an executable project referencing the library.
`async Task Main` works in net48 with a suitable C# compiler; net48 executes
on Windows only. `echo READY` works in the default CMD/sh shells.

```csharp
using System;
using System.Threading;
using System.Threading.Tasks;
using CommunicationDriver;

class Program {
 static async Task<int> Main() {
  using(var driver = new Driver())
  using(var connection = driver.CreateCli(new CliOptions {
   ExecutionMode = ExecutionMode.OneShot
  })) {
   await connection.OpenAsync();
   var result = await connection.ExecuteAsync("echo READY", Check.Contains("READY"));
   Console.Write(result.Response);
   await connection.CloseAsync();
   return result.Check != null && result.Check.Passed && result.ExitCode == 0 ? 0 : 1;
  }
 }
}
```

## Factory options

All factories are on Driver and return Connection. Common options: Encoding,
NewLine, ReceiveUntil, ConnectTimeout, IoTimeout, ExecutionTimeout, CloseTimeout,
MaxResponseBytes. Backend fields:

| Factory / options | Fields |
| --- | --- |
| CreateTcp(TcpOptions) | Host, Port |
| CreateUdp(UdpOptions) | RemoteHost, RemotePort, LocalHost, LocalPort |
| CreateSerial(SerialOptions) | PortName, BaudRate, DataBits, Parity, StopBits, FlowControl |
| CreateSsh(SshOptions) | Host, Port, UserName, Auth, HostKey, Shell, ExecutionMode, SessionCommand |
| CreateAdb(AdbOptions) | DeviceSerial, ExecutablePath, ServerHost, ServerPort, ExecutionMode, SessionCommand |
| CreateCli(CliOptions) | Shell, ExecutablePath, Arguments, WorkingDirectory, Environment, ExecutionMode |

SSH uses `SshAuth.PrivateKey(path, passphrase)` or `SshAuth.Password(password)`
and `HostKey.KnownHosts(path)` or `HostKey.Sha256(fingerprint)`. Inputs come
from the caller's real configuration and trust decision.

## Framed TCP and byte operations

Helpers below can live inside Program. TCP assumes commands terminated by LF
and replies terminated by END; replace both with the actual protocol. The
binary helper takes the actual expected reply length, independent of request
length. Delimiter is stripped unless inclusion is requested.

```csharp
static async Task<ExecutionResult<string>> QueryTcp(
 Driver driver, string host, ushort port, CancellationToken token) {
 using(var connection = driver.CreateTcp(new TcpOptions {
  Host = host, Port = port, ReceiveUntil = ReceiveUntil.Delimiter("END")
 })) {
  await connection.OpenAsync(cancellationToken: token);
  var result = await connection.ExecuteAsync("STATUS\n", Check.Contains("READY"),
   new CallOptions { Timeout = TimeSpan.FromSeconds(5) }, token);
  await connection.CloseAsync(); // Do not reuse a cancelled operation token for cleanup.
  return result;
 }
}

static Task<ExecutionResult<byte[]>> QueryBytes(Connection connection,
 byte[] request, ulong replyBytes, CancellationToken token) {
 return connection.ExecuteAsync(request, options: new CallOptions {
  ReceiveUntil = ReceiveUntil.Bytes(replyBytes), Timeout = TimeSpan.FromSeconds(5)
 }, cancellationToken: token);
}

static Task<ExecutionResult<string>> WaitForReady(Connection connection,
 string command, CancellationToken token) {
 return connection.ExecuteUntilAsync(command, Check.Contains("READY"),
  cancellationToken: token);
}
```

Per-call options override defaults. ReceiveUntil choices: Bytes(ulong),
Characters(ulong), Delimiter(string/byte[], includeDelimiter), Line(newLine),
Pattern(regex), Idle(TimeSpan), Message(), Available(). The early-match helper
assumes managed command completion or a configured reliable cleanup boundary.
Awaiting its Task does not mean the remote command has exited.

## Batch and transfer helpers

Connections must already be open and owned by one Driver. Pass commands that
follow each connection's protocol. Await the batch before its dependent stage.

```csharp
static Task<BatchResult> RunStage(Connection first, string firstCommand,
 Connection second, string secondCommand, CancellationToken token) {
 return Batch.RunAsync(new[] {
  BatchItem.Execute("first/status", first, firstCommand, Check.Contains("READY")),
  BatchItem.Execute("second/status", second, secondCommand, Check.Contains("READY"))
 }, new BatchOptions { MaxConcurrency = 32 }, token);
}

static async Task<TransferResult> UploadFile(Connection connection,
 string localPath, string remotePath, CancellationToken token) {
 if((connection.Capabilities & Capabilities.Upload) == 0)
  throw new NotSupportedException("Connection cannot upload");
 return await connection.UploadAsync(localPath, remotePath,
  new TransferOptions { Overwrite = false, Timeout = TimeSpan.FromSeconds(30) }, token);
}
```

Inspect report.Items: Key, State, Succeeded, Error, TextExecution, ByteExecution,
Transfer. AllSucceeded includes checks, not exit-code policy. DownloadAsync
uses `(remotePath, localPath, options, cancellationToken)`.

## Errors, cancellation, and low-level I/O

Pass CancellationToken as `cancellationToken` or the final positional argument.
Catch CommOperationCanceledException before a general OperationCanceledException;
it carries Error and PartialResult. Other operation errors use CommException
with those properties. Error fields: Code, Message, Backend, NativeCode,
PartialText, PartialBytes, ConfirmedProgress, PartialFile. Preserve diagnostics
without logging secrets.

ConnectionState.NeedsRecovery requires CloseAsync then OpenAsync before another
transaction. Do not replay the failed command by default. Use a noncancelled
token for cleanup. Dispose releases resources when an operation throws.
ExecutionResult has Response, StandardError, Check (possibly null), ExitCode
(nullable), CompletionReason, Elapsed. Check mismatch returns Passed=false.

Low-level methods have synchronous partners: ReadBytesAsync, ReadStringAsync,
ReadLineAsync, ReadExactlyBytesAsync(count), ReadExactlyStringAsync(count),
ReadUntilBytesAsync(rule), ReadUntilStringAsync(rule), WriteBytesAsync,
WriteStringAsync, WriteLineAsync, CloseInputAsync. CallOptions.ReadStream and
CheckStream select StandardOutput/StandardError where supported. Do not race
low-level reads against Execute on the same response.
