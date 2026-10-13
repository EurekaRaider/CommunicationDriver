# CommunicationDriver

跨平台通信库：C++23 原生异步核心、稳定 C ABI、C++23 和 C#（net48/net10）包装，共用一个实现。

支持 TCP 客户端、固定对端 UDP、串口、SSH、ADB 和 CLI。所有连接通过相同方法使用；Create 保存配置，Open 才连接或启动。`Execute` 发送一次并收集完整回复后检查；`ExecuteUntil` 发送一次并持续匹配。正常检查不匹配返回 `Check.Passed=false`；超时、取消与 IO 故障保留部分结果并明确报错。

项目处于 0.1.0 验收阶段。请以 [批准计划](docs/approved-plan.md)、[API 语义](docs/api.md) 和 [验证报告](docs/validation.md) 为准；平台 CI 与硬件验收分开记录。

```csharp
using (var driver = new Driver()) {
    var connection = driver.CreateCli(new CliOptions {
        ExecutionMode = ExecutionMode.OneShot,
        Shell = ShellKind.Sh
    });
    await connection.OpenAsync();
    var result = await connection.ExecuteAsync("printf READY", Check.Contains("READY"));
    Console.WriteLine(result.Response);
    Console.WriteLine(result.Check.Passed);
    await connection.CloseAsync();
}
```

ADB 改为 `CreateAdb(new AdbOptions { DeviceSerial = "DEVICE_SERIAL_A" })`。SSH 必须设置 Auth 和 HostKey 信任。TCP/UDP/串口的普通 Execute 必须配置可靠的 ReceiveUntil 完成规则，例如 `ReceiveUntil.Bytes(5)` 或 `ReceiveUntil.Delimiter("END")`；默认不猜测静默间隔。只有显式选择 `ReceiveUntil.Idle(...)` 才按静默完成。

```cpp
comm::Driver driver;
comm::CliOptions options;
auto connection = driver.create_cli(options);
connection.open();
auto result = connection.execute("printf READY", comm::Check::contains("READY"));
connection.close();
```

Windows 默认 shell 为 CMD；macOS/Linux 默认为 sh。选定 shell 的命令语法由该 shell 解释。直接程序配置 `ExecutablePath` 与 `Arguments` 后，Execute 的 command 为 stdin 内容。

构建需要 CMake 3.28+、支持所需 C++23 特性的编译器、.NET SDK 10；Unix 需要 OpenSSL 开发文件。依赖版本及 SHA256 已锁定：

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
cmake --install build --prefix install
dotnet build bindings/dotnet/CommunicationDriver -c Release
```

可用 `-DCD_FETCH_DEPENDENCIES=OFF` 使用已安装的依赖（Boost 必须为 1.92.0）。C/C++ 独立消费者位于 `examples/consumer`；C# 包引用和传统 packages.config 示例位于 `examples/dotnet`。NuGet 从平台 CI 产物获取，尚未发布到 nuget.org。net48 仅 Windows；net10 按平台 RID 加载 native DLL/so/dylib。

原生制品名为 `communicationdriver_native.dll`、`libcommunicationdriver_native.so` 或 `libcommunicationdriver_native.dylib`。native 产物包含头文件、所需伴随运行库和 Windows import library；可用 `scripts/test_native_package.py --rid <RID>` 验证独立 C/C++ 链接和运行。所有方法、C ABI 函数及 CMake 目标仍使用 CommunicationDriver 的既定命名。

批次通过 `Batch.RunAsync(items)` 并行执行，默认并发 32；同连接串行，单项失败不取消其他项，报告按输入顺序。连续 await 批次形成步骤 barrier。输入 key 可采用 `UP01/adb`、`UP01/serial` 等标签。

源码 MIT；第三方依赖许可证随 native/NuGet 包附带。测试用密钥、硬件标识和本机产物不会提交。完整 PTY/TUI、TCP 服务端、递归传输、自动重试与可靠 UDP 不在 v1 范围。
