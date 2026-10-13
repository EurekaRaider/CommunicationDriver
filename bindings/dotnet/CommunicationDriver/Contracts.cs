using System;
using System.Collections.Generic;
using System.Threading;
namespace CommunicationDriver {
public enum ErrorCode { OK,InvalidArgument,NotSupported,NotOpen,Closed,Timeout,Cancelled,ConnectionFailed,Disconnected,EndOfStream,AuthenticationFailed,HostKeyRejected,DeviceUnavailable,PermissionDenied,IOError,ProtocolError,EncodingError,LimitExceeded,Busy,NeedsRecovery,InvalidContext,InternalError }
public enum ConnectionState {Created,Opening,Open,Closing,Closed,Faulted,NeedsRecovery}
[Flags] public enum Capabilities:ulong {Read=1,Write=2,Execute=4,ExecuteUntil=8,CloseInput=16,StandardError=32,Upload=64,Download=128,Message=256}
public enum EncodingKind {UTF8=1,ASCII=2,GB18030=3}
public enum ExecutionMode {Persistent,OneShot}
public enum ShellKind {Default,Cmd,WindowsPowerShell,Pwsh,Sh,Bash,Zsh}
public enum StreamKind {StandardOutput,StandardError}
public enum CompletionReason {None,CommandCompleted,ProtocolBoundary,Idle,Matched}
public enum Parity {None,Odd,Even}
public enum StopBits {One,OnePointFive,Two}
public enum FlowControl {None,Software,Hardware}
public enum BatchItemState {Completed,Failed,TimedOut,Cancelled,NotStarted}
public sealed class Check {
 internal int Kind;internal string Text;internal byte[] Bytes;
 private Check(int kind,string text,byte[] bytes){Kind=kind;Text=text;Bytes=bytes==null?null:(byte[])bytes.Clone();}
 public static Check Contains(string value){return new Check(1,value??throw new ArgumentNullException(nameof(value)),null);}
 public static Check Contains(byte[] value){return new Check(1,null,value??throw new ArgumentNullException(nameof(value)));}
 public static Check Equals(string value){return new Check(2,value??throw new ArgumentNullException(nameof(value)),null);}
 public static Check Equals(byte[] value){return new Check(2,null,value??throw new ArgumentNullException(nameof(value)));}
 public static Check Regex(string pattern){return new Check(3,pattern??throw new ArgumentNullException(nameof(pattern)),null);}
}
public sealed class ReceiveUntil {
 internal int Kind;internal ulong Count,IdleMs;internal string Text;internal byte[] Data;internal bool Include;
 private ReceiveUntil(int kind){Kind=kind;}
 public static ReceiveUntil Bytes(ulong count){if(count==0)throw new ArgumentOutOfRangeException(nameof(count));return new ReceiveUntil(1){Count=count};}
 public static ReceiveUntil Characters(ulong count){if(count==0)throw new ArgumentOutOfRangeException(nameof(count));return new ReceiveUntil(2){Count=count};}
 public static ReceiveUntil Delimiter(string delimiter,bool includeDelimiter=false){if(string.IsNullOrEmpty(delimiter))throw new ArgumentException("Delimiter required",nameof(delimiter));return new ReceiveUntil(3){Text=delimiter,Include=includeDelimiter};}
 public static ReceiveUntil Delimiter(byte[] delimiter,bool includeDelimiter=false){if(delimiter==null||delimiter.Length==0)throw new ArgumentException("Delimiter required",nameof(delimiter));return new ReceiveUntil(3){Data=(byte[])delimiter.Clone(),Include=includeDelimiter};}
 public static ReceiveUntil Line(string newLine="\n"){return Delimiter(newLine);}
 public static ReceiveUntil Pattern(string regex){return new ReceiveUntil(4){Text=regex??throw new ArgumentNullException(nameof(regex))};}
 public static ReceiveUntil Idle(TimeSpan interval){return new ReceiveUntil(5){IdleMs=Interop.Milliseconds(interval)};}
 public static ReceiveUntil Message(){return new ReceiveUntil(6);}
 public static ReceiveUntil Available(){return new ReceiveUntil(7);}
}
public abstract class ConnectionOptions {
 public EncodingKind Encoding {get;set;}=EncodingKind.UTF8;
 public string NewLine {get;set;}="\n";
 public ReceiveUntil ReceiveUntil {get;set;}
 public TimeSpan ConnectTimeout {get;set;}=TimeSpan.FromSeconds(10);
 public TimeSpan IoTimeout {get;set;}=TimeSpan.FromSeconds(5);
 public TimeSpan ExecutionTimeout {get;set;}=TimeSpan.FromSeconds(30);
 public TimeSpan CloseTimeout {get;set;}=TimeSpan.FromSeconds(10);
 public ulong MaxResponseBytes {get;set;}=1048576;
}
public sealed class TcpOptions:ConnectionOptions {public string Host {get;set;}public ushort Port {get;set;}}
public sealed class UdpOptions:ConnectionOptions {public string RemoteHost {get;set;}public ushort RemotePort {get;set;}public string LocalHost {get;set;}public ushort LocalPort {get;set;}}
public sealed class SerialOptions:ConnectionOptions {public string PortName {get;set;}public uint BaudRate {get;set;}=115200;public uint DataBits {get;set;}=8;public Parity Parity {get;set;}public StopBits StopBits {get;set;}public FlowControl FlowControl {get;set;}}
public sealed class SshAuth {internal int Kind;internal string Value,Passphrase;private SshAuth(){}public static SshAuth Password(string password){return new SshAuth {Kind=0,Value=password??throw new ArgumentNullException(nameof(password))};}public static SshAuth PrivateKey(string path,string passphrase=null){return new SshAuth {Kind=1,Value=path??throw new ArgumentNullException(nameof(path)),Passphrase=passphrase};}}
public sealed class HostKey {internal int Kind;internal string Value;private HostKey(){}public static HostKey KnownHosts(string path){return new HostKey {Value=path??throw new ArgumentNullException(nameof(path))};}public static HostKey Sha256(string fingerprint){return new HostKey {Kind=1,Value=fingerprint??throw new ArgumentNullException(nameof(fingerprint))};}}
public sealed class SshOptions:ConnectionOptions {public string Host {get;set;}public ushort Port {get;set;}=22;public string UserName {get;set;}public SshAuth Auth {get;set;}public HostKey HostKey {get;set;}public ShellKind Shell {get;set;}public ExecutionMode ExecutionMode {get;set;}public string SessionCommand {get;set;}}
public sealed class AdbOptions:ConnectionOptions {public string DeviceSerial {get;set;}public string ExecutablePath {get;set;}public string ServerHost {get;set;}="127.0.0.1";public ushort ServerPort {get;set;}=5037;public ExecutionMode ExecutionMode {get;set;}public string SessionCommand {get;set;}}
public sealed class CliOptions:ConnectionOptions {public ShellKind Shell {get;set;}public string ExecutablePath {get;set;}public IList<string> Arguments {get;set;}=new List<string>();public string WorkingDirectory {get;set;}public IDictionary<string,string> Environment {get;set;}=new Dictionary<string,string>();public ExecutionMode ExecutionMode {get;set;}}
public sealed class CallOptions {public TimeSpan? Timeout {get;set;}public EncodingKind? Encoding {get;set;}public string NewLine {get;set;}public ulong MaxResponseBytes {get;set;}public ReceiveUntil ReceiveUntil {get;set;}public StreamKind ReadStream {get;set;}public StreamKind CheckStream {get;set;}}
public sealed class TransferOptions {public TimeSpan? Timeout {get;set;}public bool Overwrite {get;set;}=true;}
public sealed class CheckResult {public bool Passed {get;internal set;}public string MatchedText {get;internal set;}public byte[] MatchedBytes {get;internal set;}public IReadOnlyList<string> Groups {get;internal set;}public string FailureReason {get;internal set;}}
public sealed class ExecutionResult<T> {public T Response {get;internal set;}public T StandardError {get;internal set;}public CheckResult Check {get;internal set;}public int? ExitCode {get;internal set;}public CompletionReason CompletionReason {get;internal set;}public TimeSpan Elapsed {get;internal set;}}
public sealed class TransferResult {public string Source {get;internal set;}public string Destination {get;internal set;}public ulong? BytesTransferred {get;internal set;}public TimeSpan Elapsed {get;internal set;}}
public sealed class ErrorInfo {public ErrorCode Code {get;internal set;}public string Message {get;internal set;}public string Backend {get;internal set;}public long NativeCode {get;internal set;}public string PartialText {get;internal set;}public byte[] PartialBytes {get;internal set;}public ulong? ConfirmedProgress {get;internal set;}public string PartialFile {get;internal set;}}
public class CommException:Exception {public ErrorInfo Error {get;}public object PartialResult {get;}internal CommException(ErrorInfo error,object partial):base(error.Message){Error=error;PartialResult=partial;}}
public sealed class CommOperationCanceledException:OperationCanceledException {public ErrorInfo Error {get;}public object PartialResult {get;}internal CommOperationCanceledException(ErrorInfo error,object partial):base(error.Message){Error=error;PartialResult=partial;}}
public sealed class BatchOptions {public uint MaxConcurrency {get;set;}=32;}
public sealed class BatchItem {
 internal int Kind;internal string Command,Source,Destination;internal byte[] Bytes;internal Check Check;internal CallOptions Options;internal TransferOptions TransferOptions;
 public string Key {get;private set;}public Connection Connection {get;private set;}
 private BatchItem(string key,Connection connection,int kind){Key=key??throw new ArgumentNullException(nameof(key));Connection=connection??throw new ArgumentNullException(nameof(connection));Kind=kind;}
 public static BatchItem Execute(string key,Connection connection,string command,Check check=null,CallOptions options=null){return new BatchItem(key,connection,0){Command=command??throw new ArgumentNullException(nameof(command)),Check=check,Options=options};}
 public static BatchItem Execute(string key,Connection connection,byte[] command,Check check=null,CallOptions options=null){return new BatchItem(key,connection,1){Bytes=(byte[])(command??throw new ArgumentNullException(nameof(command))).Clone(),Check=check,Options=options};}
 public static BatchItem ExecuteUntil(string key,Connection connection,string command,Check match,CallOptions options=null){var item=Execute(key,connection,command,match??throw new ArgumentNullException(nameof(match)),options);item.Kind=2;return item;}
 public static BatchItem ExecuteUntil(string key,Connection connection,byte[] command,Check match,CallOptions options=null){var item=Execute(key,connection,command,match??throw new ArgumentNullException(nameof(match)),options);item.Kind=3;return item;}
 public static BatchItem Upload(string key,Connection connection,string localPath,string remotePath,TransferOptions options=null){return new BatchItem(key,connection,4){Source=localPath,Destination=remotePath,TransferOptions=options};}
 public static BatchItem Download(string key,Connection connection,string remotePath,string localPath,TransferOptions options=null){return new BatchItem(key,connection,5){Source=remotePath,Destination=localPath,TransferOptions=options};}
}
public sealed class BatchItemResult {public string Key {get;internal set;}public BatchItemState State {get;internal set;}public bool Succeeded {get;internal set;}public ExecutionResult<string> TextExecution {get;internal set;}public ExecutionResult<byte[]> ByteExecution {get;internal set;}public TransferResult Transfer {get;internal set;}public ErrorInfo Error {get;internal set;}public TimeSpan Elapsed {get;internal set;}}
public sealed class BatchResult {public IReadOnlyList<BatchItemResult> Items {get;internal set;}public bool AllSucceeded {get;internal set;}public bool WasCancelled {get;internal set;}public TimeSpan Elapsed {get;internal set;}}
}
