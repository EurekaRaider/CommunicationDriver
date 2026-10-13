# 验证报告（0.1.0）

软件验收与真实硬件分别记录。以下 PASS 仅对应明确列出的测试范围，不把编译或模拟称作设备通过。

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

## 2026-10-13 性能与资源调查（本机工作区修复）

本轮在 macOS 27.0.1 ARM64 上调查当前工作区代码，发现并修复了三个问题。下列结果对应未提交的工作区修复，不归入上方旧版一小时长测或下方既有平台 CI。源码与原生库 SHA256、资源采样摘要见 [performance-audit-macos-arm64.json](evidence/performance-audit-macos-arm64.json)；原始日志和采样保存在本机忽略目录 `artifacts/performance-audit/`。

- **关闭竞态与句柄滞留**：同连接同时提交多个 CloseAsync，会覆盖先前的 active 关闭请求，令已接受的操作永久等待，保留回调/操作句柄。受控测试先阻塞 IO 回调，再提交 128 个关闭请求，修复前稳定触发 5 秒 watchdog。修复后每个关闭请求独立完成；本地文件任务仍是后续 Open 的 barrier，关闭超时/取消也只完成其自己的结果。
- **超时整数溢出**：`INT64_MAX` 毫秒通过旧校验，在转换为 steady-clock 刻度时触发 UBSan signed integer overflow。现在默认超时、调用/传输/批次超时、Idle 和 observer wait 都校验原生时钟的安全范围；`CD_INFINITE` 仍仅适用于 observer wait。回归验证不安全值在接受/IO 前拒绝。
- **取消监听列表扫描退化**：已完成但调用者仍保留的 Operation 不会过期，旧 subscribe 每次扫描全部历史完成项。现在清理过期及已完成项。保留 32,000 个操作并复用同一个取消源，实测从 1,339ms 降至 534ms；4k/8k/16k/32k 修复后分别为 67/133/266/534ms。该指标覆盖取消订阅与结果生命周期，不是网络往返延迟，也不设置跨机器固定时间门槛。

| 验证 | 本轮结果 |
| --- | --- |
| Release CTest | native_behavior / cpp_lifetime / native_stress / adb_simulated 全部通过 |
| ASan + UBSan | 上述四项连续三轮通过，未报告内存错误或未定义行为 |
| ThreadSanitizer | 上述四项连续三轮通过，未报告数据竞争 |
| native 并发取消 | 8 个提交线程、4,000 次操作，提前 release，回调总数和完成结果正确 |
| 生命周期压力 | 10,000 个驱动、20,000 次 CLI 进程启动；稳态 RSS 12,912–12,960KiB、线程 4、FD 8–16，末次 FD 8 |
| macOS leaks | 1,000 个驱动生命周期及并发压力后，报告 0 leaks / 0 leaked bytes |
| 32 混合连接五分钟 | 905,561 批次 / 28,977,952 次操作；8 TCP、8 UDP、8 CLI、8 虚拟串口；稳态 RSS 10,352–10,480KiB、线程固定 14、FD 固定 109 |
| C# GC/取消/并发关闭 | 4,004,000 次操作、128,128 次并发关闭；2,000 个已释放包装对象全部被 GC 回收；稳态 RSS 68,816–69,136KiB，末次 FD 41 |
| 真实回环 OpenSSH/SFTP | Release 30 轮，ASan/UBSan 3 轮，TSan 3 轮；包含严格 host key、1MiB 文件相等、128MiB 下载中断及 Close 后不再写入 |
| C# 编译与消费者 | net10 压力测试编译 0 warning / 0 error；既有消费者实际运行通过 |

资源稳态统计排除启动后前 10 秒。C# 最终 GC heap 包含测试保留的 2,000 个 WeakReference 和调度器状态，判断依据同时包括包装对象可回收与稳定 RSS，不把 heap 必须回到完全相同字节数作为验收标准。两次五分钟长测均通过，表中仅列最后一次完整修复版本。高吞吐长测约占用 1.9 个 CPU 核，包含测试端回环服务线程。

ASan/TSan 构建使用 `CD_FETCH_DEPENDENCIES=OFF` 的本机依赖，对工程源码与测试插桩，第三方库内部并未完整插桩。Release 使用 `build-locked` 的锁定依赖。以上仅为本机所测范围内的结论，不能证明所有负载或平台均不存在泄漏、死锁或数据竞争。本轮没有运行六平台 CI；真实 Android/串口设备仍未验证。

### 复跑

```sh
cmake --build build-locked --parallel 4
ctest --test-dir build-locked --output-on-failure
python3 scripts/run_stress.py --output artifacts/performance-audit/mixed-soak.json --timeout 330 -- build-locked/cd_tests --soak-seconds 300
python3 scripts/run_stress.py --output artifacts/performance-audit/lifecycle-churn.json --timeout 120 --interval 1 -- build-locked/cd_stress_tests 10000
build-locked/cd_stress_tests --retained 32000
python3 scripts/test_ssh_local.py --build build-locked --user-server --repeat 30
dotnet build tests/dotnet/Stress -c Release
DYLD_LIBRARY_PATH="$PWD/build-locked" dotnet tests/dotnet/Stress/bin/Release/net10.0/Stress.dll 1000
MallocStackLogging=1 leaks --atExit -- build-locked/cd_stress_tests 1000
```

macOS 采样需要允许 `ps`/`lsof`，回环服务需要允许本机监听；在限制这些操作的沙箱外运行。Linux managed 消费者使用 `LD_LIBRARY_PATH`，Windows 将对应原生 DLL 目录加入 PATH。`--user-server` 仅用于支持当前用户运行 sshd 的 Unix 环境；原有免密 sudo 路径仍可使用。`native_stress` 已加入 CTest，后续平台 CI 会自动运行。

Sanitizer 构建在本机可使用：

```sh
cmake -S . -B build-sanitize -G Ninja -DCD_FETCH_DEPENDENCIES=OFF -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS=-fsanitize=address,undefined -DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -DCMAKE_SHARED_LINKER_FLAGS=-fsanitize=address,undefined -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined
cmake --build build-sanitize --parallel 4
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build-sanitize --output-on-failure --repeat until-fail:3
cmake -S . -B build-tsan -G Ninja -DCD_FETCH_DEPENDENCIES=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_FLAGS=-fsanitize=thread -DCMAKE_CXX_FLAGS=-fsanitize=thread -DCMAKE_SHARED_LINKER_FLAGS=-fsanitize=thread -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread
cmake --build build-tsan --parallel 4
TSAN_OPTIONS=halt_on_error=1 ctest --test-dir build-tsan --output-on-failure --repeat until-fail:3
```

## 既有平台与制品验收

[最终软件提交 `3da0d8c`](https://github.com/EurekaRaider/CommunicationDriver/commit/3da0d8cab143ae1ea1d2c3e2f0a6e4e7b2150177) 的 [Platform CI #38397256682](https://github.com/EurekaRaider/CommunicationDriver/actions/runs/38397256682) 六平台任务与合并 NuGet 任务均已终态成功。本机仅 macOS ARM64；其他平台结论来自对应真实 CI runner 上的实际程序执行。

| RID | 系统与进程 | Native / C / C++ 消费者 | net10 | net48 / packages.config | 真实 OpenSSH/SFTP |
| --- | --- | --- | --- | --- | --- |
| linux-x64 | Ubuntu 24.04 x64 | PASS | PASS | 不适用 | PASS |
| linux-arm64 | Ubuntu 24.04 ARM64 | PASS | PASS | 不适用 | PASS |
| osx-arm64 | macOS 15 ARM64 | PASS | PASS | 不适用 | PASS |
| osx-x64 | macOS 15 Intel | PASS | PASS | 不适用 | PASS |
| win-x64 | Windows Server 2025，64 位进程 | PASS | PASS | PASS / PASS | 未联调 |
| win-x86 | Windows Server 2025 x64 runner，32 位进程 | PASS | PASS | PASS / PASS | 未联调 |

Native 包独立 C/C++ 客户端直接使用包内头文件和运行库，Windows 同时验证 import library。PackageReference 与 packages.config 消费者实际执行同步/异步、文本/字节、32 连接批次和 GC；Windows 进程位数有运行时断言。NuGet `CommunicationDriver.0.1.0.nupkg` 合并六个 RID、net48/net10 和第三方许可证，未发布到 nuget.org。

编译器为 Linux GCC 14、Windows MSVC、CI macOS LLVM 21；保持 C++23 特性探测。一次 Intel SIGCHLD 重跑成功不能证明无竞争，后续 ASan 复现并修复等待对象的 use-after-free 后，最新完整六平台 CI 再次通过。编译器扩展/依赖警告不等于零警告构建；C# 双目标编译为零 warning/error。

## 未验证

至少两台真实 Android 设备与两个真实串口未验证。用户本轮明确回复“本轮暂无硬件，记录未验证”；不以虚拟 PTY、fake adb 或回环网络代替硬件验收。无人为 UP 总数限制，32 为默认批并发与验收规模。
