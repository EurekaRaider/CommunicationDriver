# 验证报告（进行中）

本文件在最终交付前更新。已执行的本机验证与平台 CI、模拟、真实硬件分别记录，不把编译成功称作设备通过。

## 本机已执行

- macOS ARM64，AppleClang 21，Boost 1.92，libssh2 1.11.1，RE2 2025-11-05，.NET SDK 10.0.400。
- C/C++ 示例实际运行；C# net10 消费者运行：同步/异步、文本/字节、32 CLI 连接批次、GC、取消部分结果、提前匹配后安全清理。
- net48/net10 双目标编译：0 warning、0 error；本机不运行 Windows net48。
- native 行为：无换行、TCP 分片、UDP 跨包 READY/UTF8、Unicode scalar count、不匹配立即返回、持久状态、超时与 NeedsRecovery、回调释放、防死锁、Idle 长停顿、缓存上限、32 TCP/UDP/CLI/虚拟串口并发。
- 临时真实 OpenSSH：严格 SHA256 host key、拒绝错误 key、持久状态、单次 stderr 和非零 status、提前匹配清理、SFTP 上传下载与 1MiB 二进制逐字节相等。
- ADB 可执行程序模拟：显式固定 -H/-P/-s、两个绑定连接、持久与单次模式、文件一致性、离线设备错误。此项不是 Android 真机验证。
- 50 条持久 shell 命令在本机测得 8–13ms，无固定每命令静默等待。
- 一小时 32 混合连接已通过：8 TCP、8 UDP、8 持久 CLI、8 虚拟串口，7,815,392 批次、250,092,544 次操作、3,600,000ms；20–55 分钟多次观察 RSS 均为 14,112KiB。机器可读摘要见 [soak-macos-arm64.json](evidence/soak-macos-arm64.json)。
- 首次基线运行发现 UDP 消息元数据累积，不能作为资源通过证据；修复后重新运行上述完整一小时。长测由 `af729dd` 版二进制启动，后续改名、EOF/编码/传输修正以最新 CI 和本机回归验证，不归入旧二进制的长测结论。
- 新增回归覆盖：100 个异步操作在释放 driver/connection/operation 并取消时恰好一次非内联回调；空匹配与未匹配字段区分；UDP 空消息、字节上限和四包 GB18030；编码错误保留有效文本前缀和原始字节；stderr 超时部分结果；EOF 部分回复；持久进程真实退出码及显式恢复；内部完成标记不占业务回复字节额度。
- AddressSanitizer 复现了 Close 删除 process 对象后异步等待仍访问其内部引用的 heap-use-after-free。等待回调现保留该对象到完成；修复后 native/C++/ADB 模拟测试各连续三轮通过 AddressSanitizer，未检测到同类内存错误。一次重跑通过不能替代此内存验证。
- 128MiB SFTP 下载中断报告部分路径，Close 等待本地文件任务，重开后部分文件不继续增长。ADB 模拟覆盖不覆盖独占安装、已有目标保留和目录拒绝；不作为 Android 文件系统能力证明。

## 待验收

六平台 CI、锁定依赖 native/NuGet 包的独立消费者和 Windows packages.config 运行，以终态 CI 结果更新。Windows x86/x64、Linux x64/ARM64、macOS x64 未在本机运行。

至少两台真实 Android 设备与两个真实串口未验证。用户本轮明确回复“本轮暂无硬件，记录未验证”；不以虚拟 PTY、fake adb 或回环网络代替硬件验收。无人为 UP 总数限制，32 为默认批并发与验收规模。
