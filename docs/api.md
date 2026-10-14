# API 与运行语义

公开 C API 见 `include/communicationdriver/communicationdriver.h`；C++ 包装见同目录 `.hpp`；C# 合约见 `bindings/dotnet/CommunicationDriver/Contracts.cs`。所有 IO 都有同步与异步版本，异步由原生事件驱动。默认 UTF8，可选 ASCII/GB18030；C/C++ 文本始终以 UTF8 表示，选项编码用于传输字节；C# 使用 Unicode string。

Create 复制并校验配置，不连接。Open 建立本地资源/会话；UDP Open 不代表远端在线。Close 可中断未完成事务，CloseAsync 可在完成回调中使用。关闭后可以 Open 恢复，Driver Close 后不能创建或重新打开连接。

Read 等待可用数据，不宣称业务完整；ReadExactlyString 按 Unicode scalar value 计数；ReadUntil 默认剥离 delimiter；ReadLine 剥配置行结束符。WriteString 不加换行，WriteLine 才加。字节接口保持原始字节。独立 stderr 只适用于 CLI/SSH/ADB；其他后端会在 IO 前拒绝该选项。

Execute 需要可靠完成依据：托管命令完成、明确字节/字符长度、delimiter、pattern 或 UDP message。Available 只为低级读取；无配置的任意流无法保证完整分片回复并立即判不匹配。Idle 是显式选择，长于静默间隔的业务停顿会提前完成。原始流 Execute 以 Idle 完成后进入 NeedsRecovery，要求 Close/Open，避免把迟到数据用于下一命令；低级 Read 的显式 Idle 仍可使用。

ExecuteUntil 只接受 Contains/Regex。匹配即返回 CompletionReason.Matched，可能没有 ExitCode；托管命令的剩余输出由库收集清理，下一事务排队等待。原始流配置了可靠完成规则时同样等待该边界清理；没有可靠边界时匹配后进入 NeedsRecovery，要求关闭重开。没有隐式重发、重连或远程副作用回滚。

超时/取消包括等待同连接队列的时间；有不明回复归属时进入 NeedsRecovery。结果包含已接收的完整部分字符与原始部分字节。正常检查失败没有异常。正则使用 RE2，大小写敏感、不 trim、不移除 ANSI；反向引用和 lookaround 等 RE2 不支持语法属于 InvalidArgument，发送前拒绝。

超时与 Idle 间隔必须能安全转换到原生 steady clock，超出其安全范围会在提交前返回 InvalidArgument。`CD_INFINITE` 仅用于 C ABI 的 observer wait；普通 IO 不接受无限超时。多个并发 CloseAsync 各自完成，并在本地文件任务结束后才允许后续 Open 开始。

OneShot 命令输出独立于低级持久 IO 通道。CLI 直接程序单次执行由库关闭 stdin，等待退出并排空 stdout/stderr。Persistent shell 保存 cd/env，生成内部标记并剥离。SessionCommand 用于 SSH/ADB 自定义持久会话，与 OneShot 同时设置会在创建时拒绝；自定义交互程序须使用明确 ReceiveUntil。没有 PTY/TUI 模拟；命令在后台自行派生进程可能在标记后继续产生无归属输出，会拒绝后续执行并要求恢复。

ADB 固定 DeviceSerial，所有命令带相同 `-s`；ServerHost/ServerPort 是主机 server 地址。Open 用 get-state 验证绑定设备，关闭连接不 kill server。SSH 严格已知 host 或 SHA256 校验，不自动信任；私钥/密码不记录在错误文本或公开日志。关闭客户端不保证远程进程退出或副作用撤销。

文件传输是单文件操作。Overwrite 默认为 true；中断/失败可能留下部分文件，ErrorInfo.PartialFile 报告可能的目标或暂存路径，无法确认进度保持 null。SSH 使用 SFTP EXCL 和本地独占创建保护 Overwrite=false；ADB 使用目标同目录的随机暂存文件和独占硬链接安装，不替换已有文件，文件系统不支持时明确报错并保留暂存路径。调用者应按报告决定是否移除部分文件。中断会停止后续传输，Close 等待本地文件任务完成；不能保证撤销远端已确认写入。可用校验和或逐字节比较验证传输内容。

Batch 默认 MaxConcurrency=32，不限制总连接数。连接须已打开且同 Driver，key 唯一。同连接串行；单项 timeout 从批调度准入开始，含连接队列等待；批队列等待不计入。取消返回完整索引的部分报告，未准入项为 NotStarted，已经完成项保留。Succeeded 由操作成功和检查通过共同决定，ExitCode 单独展示。

C 结构先调用 *_options_init；输出结构设置 struct_size。字符串和字节 view 的长度为实际字节，不要求 NUL 结尾。异步提交在返回前复制输入，接受后恰好一次回调；提交失败不回调，回调不会内联到提交栈。回调 result 借用，retain 后可保留；同步和 wait 的 result 由调用者 release。五种 handle 均 retain/release。cancel 是请求，不等于完成。observer wait 超时不会取消。自身完成上下文中的同步 wait/close 返回 InvalidContext，使用异步路径。

C++ Operation 是可移动 RAII 句柄；get 只能一次，wait_for 只观察，丢弃不取消，stop_token 可请求原生取消。CommunicationError 携带 ErrorInfo。C# 使用 SafeHandle 和异步 native 回调，Task continuation 异步调度；CommException/CommOperationCanceledException 携带部分结果。

C# Driver/Connection 的 Dispose 可重复或并发调用，Connection 也可在所属 Driver 已 Dispose 后释放。释放竞态产生的 ObjectDisposedException 仅在相关句柄确已关闭时忽略，其他关闭错误仍传播；已释放对象上的普通 IO 调用仍会失败。

NuGet PackageReference 支持 net48/net10，传统 packages.config 自动导入 build targets。Windows 默认按 PlatformTarget 选择 win-x86/win-x64；Unix 可设置 RuntimeIdentifier 或 CommunicationDriverRid。使用不匹配位数的 native 库会由平台加载器拒绝，不能通过改变命令模式规避。
