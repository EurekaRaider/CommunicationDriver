# 验证报告（进行中）

本文件在最终交付前更新。已执行的本机验证与平台 CI、模拟、真实硬件分别记录，不把编译成功称作设备通过。

## 本机已执行

- macOS ARM64，AppleClang 21，Boost 1.92，libssh2 1.11.1，RE2 2025-11-05，.NET SDK 10.0.400。
- C/C++ 示例实际运行；C# net10 消费者运行：同步/异步、文本/字节、32 CLI 连接批次、GC、取消部分结果、提前匹配后安全清理。
- net48/net10 双目标编译：0 warning、0 error；本机不运行 Windows net48。
- native 行为：无换行、TCP 分片、UDP 跨包 READY/UTF8、Unicode scalar count、不匹配立即返回、持久状态、超时与 NeedsRecovery、回调释放、防死锁、Idle 长停顿、缓存上限、32 TCP/UDP/CLI/虚拟串口并发。
- 临时真实 OpenSSH：严格 SHA256 host key、拒绝错误 key、持久状态、单次 stderr 和非零 status、提前匹配清理、SFTP 上传下载与 1MiB 二进制逐字节相等。
- ADB 可执行程序模拟：显式固定 -H/-P/-s、两个绑定连接、持久与单次模式、文件一致性、离线设备错误。此项不是 Android 真机验证。
- 50 条持久 shell 命令测得 8–13ms，无固定每命令静默等待。一小时 32 混合连接测试正在运行；完成前不标为通过。

## 待验收

六平台 CI、锁定依赖 native/NuGet 包的独立消费者和 Windows packages.config 运行，以终态 CI 结果更新。Windows x86/x64、Linux x64/ARM64、macOS x64 未在本机运行。

至少两台真实 Android 设备与两个真实串口当前不可访问，硬件验收未完成。虚拟 PTY、fake adb 和回环网络只证明相应代码行为。无人为 UP 总数限制，32 为默认批并发与验收规模。
