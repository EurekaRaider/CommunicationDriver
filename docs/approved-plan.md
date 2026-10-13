# CommunicationDriver 批准计划（2026-10-13）

本文件冻结用户批准的设计，作为实现和验收依据。接口不可恢复已否决命名，未验证平台和硬件不可标为通过。

## 1. 范围与架构

TCP 客户端、固定对端 UDP、SSH、ADB、串口、跨平台 CLI（terminal/shell 与 stdin/stdout 程序）；C、C++23、C# net48/net10.0 共用 C++23 native core 和稳定 C ABI。同步和真正异步；字节/字符串 IO；发送、接收、检查一体；多 UP 并行；SSH/ADB 单文件传输。

目标 Windows x86/x64、Linux x64/ARM64、macOS x64/ARM64；net48 仅 Windows。验收 32 混合连接，无人为 UP 总数限制。v1 不包含 TCP 服务端、完整 PTY/TUI、递归目录传输、自动重试命令或可靠 UDP。

依赖 Boost.Asio、Boost.Process、libssh2、官方外部 adb、RE2；先核对 API、工具链与许可证并锁定版本。CMake 强制 C++23 和特性检测，不降级；C ABI 不传 STL/异常。C# SafeHandle、Task、CancellationToken，不能 Task.Run 包装阻塞 IO；C++ 不逐操作 std::async。

## 2. 高级执行

只有 Execute/ExecuteUntil 及 Async。Execute 发送一次，收集完整回复，检查一次，正常不匹配立即 Check.Passed=false。ExecuteUntil 发送一次，持续累积 Contains/Regex 匹配，匹配即返回；超时、断开、取消、EOF、错误分别报告，不隐式重发。匹配可早于命令结束，完成原因如实记录，由库清理剩余输出；下一命令等待安全清理。超时是操作错误，保留部分回复和确认进度。

字符串/字节重载；C 使用 _string/_bytes。不公开 Query*、ExecuteCheck、QueryCheck、SendExpect*、WaitForExit、Terminate、CreateMultiUp、SendTo、ReceiveFrom。各协议返回相同 Connection。Capabilities/NotSupported 表达真实支持。CloseInput 为 EOF/半关闭，单次进程由库内部处理。

## 3. 完成规则与性能

- ADB：命令完成且输出收集完成。SSH：通道完成且输出收集完成。CLI：退出且 stdout/stderr 排空。
- 托管持久 shell：每条命令内部完成标记，检测并剥离，不泄漏。
- 自定义交互程序：配置协议完成规则。TCP/UDP/串口：可靠长度/结束标记/模式，可作为连接默认。
- Idle 仅显式选择，禁止全局 50/100ms 静默兜底和每命令等待总超时。
- 普通 Execute 无可靠完成规则必须发送前配置错误；Read/Write/ExecuteUntil 仍可用。
- 无完成信息的任意字节流不能同时保证完整分片回复、立即判不匹配和无需配置。
- 原生事件/异步 IO/计时器，不固定 Sleep 轮询。复用持久会话，不反复认证。
- 同连接事务串行，无关 Read 不抢回复；不同连接并行，不永久每 UP 一个阻塞线程。
- 不清空不明旧数据，不归属迟到回复给下一命令；归属不明确的超时/取消进入 NeedsRecovery，关闭重开恢复，不自动重试。
- 关闭 SSH/ADB 客户端不保证远程进程终止或副作用回滚。缓存上限超出 LimitExceeded，不静默截断。

## 4. 创建与配置

Driver: CreateTcp/Udp/Serial/Ssh/Adb/Cli、Close/CloseAsync/Dispose。Create 校验并保存，Open 才连接/启动。

TCP Host/Port；UDP RemoteHost/RemotePort、可选 LocalHost/LocalPort（固定对端）；Serial PortName/BaudRate/DataBits/Parity/StopBits/FlowControl；SSH Host/Port/UserName/Auth/HostKey/Shell/ExecutionMode/SessionCommand；ADB DeviceSerial 必填不可变、ExecutablePath/ServerHost/ServerPort/ExecutionMode/SessionCommand；CLI Shell/ExecutablePath/Arguments/WorkingDirectory/Environment/ExecutionMode。

默认 UTF8（另 ASCII/GB18030）、NewLine="\n"、可选 ReceiveUntil、ConnectTimeout=10s、IoTimeout=5s、ExecutionTimeout=30s、CloseTimeout=10s、MaxResponseBytes=1MiB。

SSH SshAuth.Password(password)/PrivateKey(path,passphrase=null)，HostKey.KnownHosts(path)/Sha256(fingerprint)，严格校验。ADB 每个设备命令显式 -s SERIAL；server 默认 127.0.0.1:5037，是主机 server 而非设备标识，不选择第一台设备，不 kill 共享 server。

## 5. 执行模式

Persistent 默认并保留 cd/env；OneShot 隔离命令/进程。CLI 未设 ExecutablePath 由 Shell 解释命令；设置 ExecutablePath+Arguments 时 command 是 stdin。直接程序 OneShot 启动、写入、关闭 stdin、排空、等退出；Persistent 复用并配置规则。低级 IO 为持久 stdio，可惰性创建，OneShot 输出不混入持久缓冲。

ShellKind Default/Cmd/WindowsPowerShell/Pwsh/Sh/Bash/Zsh。SSH/ADB SessionCommand 自定义会话，否则托管 shell。模式和原始 IO 必须明确可验证，不静默换模式。

## 6. Connection 方法

全部有同步和 Async：Open/Close、ReadBytes/String、ReadExactlyBytes/String、ReadUntilBytes/String、ReadLine、WriteBytes/String/Line、Execute(string/byte[])、ExecuteUntil(string/byte[],必填 match)、CloseInput、Upload/Download。属性 State/Capabilities；Dispose。

Execute(command,check=null,options=null,cancellationToken=default)，ExecuteUntil 的 match 必填。Read 等可用数据不等于业务完成；ReadExactlyString 长度为 Unicode scalar；ReadUntil 默认剥 delimiter（可包含）；ReadLine 剥配置换行；WriteString 不加换行，WriteLine 才加。原始传输 Execute 原样发送，托管 shell 负责封装。跨块增量解码不能拆坏 UTF8。UDP/串口 CloseInput NotSupported。

## 7. ReceiveUntil

Bytes(count)、Characters(count)、Delimiter(byte[]/string,includeDelimiter=false)、Line(newLine="\n")、Pattern(regex)、Idle(interval)、Message()、Available()。旧 ResponseFrame 废弃。Message 为原生消息，不支持明确报错；Available 只为低级可用块，不能普通 Execute 完成。完成规则与内容匹配不混合。

## 8. 检查、选项、结果

Check.Contains(string/bytes)、Equals(string/bytes)、Regex(string)。ResponseChecker.IsMatch(string/bytes,Check)。默认大小写敏感、不 trim、不剥 ANSI；无效 regex 发送前报错；字节原样，文本按 encoding。ExecuteUntil 禁止 Equals。内部标记剥离。

CallOptions Timeout/Encoding/NewLine/MaxResponseBytes/ReceiveUntil/ReadStream/CheckStream（stdout 默认）；无独立 stderr 后端 IO 前拒绝相应选项。

ExecutionResult<T>: Response、StandardError（适用）、Check（可空）、ExitCode（真实或可空）、CompletionReason（命令结束/协议边界/静默/匹配）、Elapsed。CheckResult Passed/MatchedText/MatchedBytes/Groups/FailureReason，仅适用字段有值。TransferResult Source/Destination/BytesTransferred（未知可空）/Elapsed；TransferOptions Timeout/Overwrite=true。中断可能部分文件，报告路径，不编造字节数。

## 9. 错误与生命周期

正常不匹配不异常。C status/ErrorInfo；C++ CommunicationError；C# CommException，取消为带部分结果的 OperationCanceledException 派生类。

ErrorInfo code/message/backend/native code/partial bytes,text/confirmed progress。
InvalidArgument、NotSupported、NotOpen、Closed、Timeout、Cancelled、ConnectionFailed、Disconnected、EndOfStream、AuthenticationFailed、HostKeyRejected、DeviceUnavailable、PermissionDenied、IOError、ProtocolError、EncodingError、LimitExceeded、Busy、NeedsRecovery、InvalidContext、InternalError。

State Created/Opening/Open/Closing/Closed/Faulted/NeedsRecovery。关闭重开恢复，不暗示远程回滚。自身回调同步等待/关闭检测 InvalidContext，异步可用。Driver close 正确处理未结束操作、资源、回调寿命。

## 10. UDP

固定对端，一次 Write 一包，过大明确错误，不拆包。内部保留边界，字符串和规则/匹配跨包累积（REA+DY、跨包 UTF8）。按接收顺序，不补偿丢包/乱序/重复。Open 只代表本地就绪。

## 11. Batch

用字典/对象映射 UP 到连接；一个 UP 多连接。Batch.Run/RunAsync(items,options,cancellationToken)；BatchItem.Execute(string/bytes)、ExecuteUntil(string/bytes,match)、Upload/Download。

MaxConcurrency=32，连接已打开且同 Driver，key 唯一。不同连接并行，同连接串行；单失败不取消其他；输入顺序返回；取消保留部分报告。单 timeout 自调度准入开始，包含连接队列等待；批调度队列等待不计入。多个 await 批次为 barrier。

BatchResult Items/AllSucceeded/WasCancelled/Elapsed；BatchItemResult Key/State/Succeeded/TextExecution/ByteExecution/Transfer/Error/Elapsed；Completed/Failed/TimedOut/Cancelled/NotStarted。Succeeded 为无操作错误且 check 通过；ExitCode 单独，不与检查等同。

## 12. C++23

snake_case；UTF8 string/string_view、span<const byte>/vector<byte>、stop_token、RAII。Operation<T> is_completed/wait/wait_for/get（一次）/cancel；wait_for 超时不取消；丢句柄不取消。

## 13. C ABI

Opaque cd_driver_t/cd_connection_t/cd_operation_t/cd_result_t/cd_cancel_source_t，都 retain/release。固定宽度，公开结构 struct_size，字符串/字节 ptr+length，CD_SV。

cd_driver_create；cd_create_tcp/udp/serial/ssh/adb/cli。所有 IO 与对应 _async：cd_open/close/read_bytes/read_string/read_exactly_bytes/read_exactly_string/read_until_bytes/read_until_string/read_line/write_bytes/write_string/write_line/execute_string/execute_bytes/execute_until_string/execute_until_bytes/close_input/upload/download。

cd_driver_close/close_async；cd_connection_get_state/capabilities；cd_operation_is_completed/wait/cancel；cd_cancel_source_create/request/is_requested；cd_batch_run/run_async；cd_response_checker_is_match_string/bytes；cd_status_name。

cd_result_get_status/kind/error/text/bytes/check/execution_info/transfer/batch_info/batch_item。*_options_init：tcp/udp/serial/ssh/adb/cli/call/transfer/batch。check_contains_string/bytes、equals_string/bytes、regex；receive_until_bytes/characters/delimiter_string/bytes/line/pattern/idle/message/available。

异步提交复制参数数据；提交失败不回调；接受后恰好一次且不内联。回调 result 借用，retain 后可保留。cancel request 不等于完成。无全局 last_error。同步 result 调用者 release。

## 14. C# 体验

using Driver，CreateAdb(new AdbOptions {DeviceSerial="DEVICE_SERIAL_A",ExecutionMode=OneShot})，await OpenAsync/ExecuteAsync("getprop sys.boot_completed",Check.Contains("1"))/CloseAsync；读取 Response/Check.Passed。同步换对应方法。六种创建后接口一致。字符串/byte[] 都实际实现。

## 15. 实施与验收

阶段：冻结三语言合约/示例/依赖；native async/缓冲/编码/超时取消和 TCP/UDP/串口；CLI/SSH/ADB；Check/Execute/Until/Batch/32UP；传输和所有权；native/NuGet/独立消费者/文档/平台 CI。

必要行为：无换行；跨块/包 READY 与 UTF8；完整回复不匹配立即；BUSY 后 READY；显式 Idle 长停顿限制；可靠信号无固定等待及性能/资源；同连接顺序、取消、迟到输出隔离；32 混合连接持续 1 小时（环境缺失准确记录）；2 台真实 ADB+2 真实串口；net48/net10 独立消费者和 packages.config/PackageReference；C 回调释放竞争/C++RAII/C#GC；文件校验、中断、部分文件。缺平台/硬件继续其他工作，不把 stub、编译或模拟说成功。

## 16. 调研依据

上游参考：openatx/adbutils _device_base.py、SSH.NET SshCommand.cs/ShellStream.cs、netmiko base_connection.py、pexpect expect.py/pxssh.py、node-serialport inter-byte-timeout parser；ADB adb.1.md（-s 设备，-H/-P server，-T 无 PTY，不用 -x）；RFC8085；CMake CXX_STANDARD；Microsoft target frameworks 与 native interop best practices。使用第三方 API 仍核对当前文档。

开发遵守用户 AGENTS：仅存在 .codegraph 时优先 CodeGraph；简单、精确变更；外部 API 查询用沙箱外 ctx7 library 然后 docs，每问题至多 3 命令，不查询敏感信息。
