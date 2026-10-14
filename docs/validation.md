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

## 分隔符接收性能复查（2026-10-14）

确认并修复 `ReadUntil` / `ReadLine` / 使用分隔符的 `Execute`、`ExecuteUntil` 的重复扫描：原实现每次收到数据都从头搜索累积回复，并重复编码文本分隔符。大回复分片越多，累计扫描越多。现在每个请求缓存线上的分隔符字节，并从上次扫描末尾减去 `delimiter.size()-1` 的位置继续搜索，保留跨块匹配需要的重叠范围。新请求重新初始化搜索状态，公开 API / ABI、超时和字节上限不变。

对比基线为 `d8955a9`，使用同一新增基准程序；两侧均为本机 AppleClang Release、`CD_FETCH_DEPENDENCIES=OFF` 的相同系统依赖。TCP 回环服务端每次写入 4 KiB，并关闭 Nagle；内核仍可能合并数据块。指标包含接收、字符串解码和结果构造，是该负载的端到端时间，不是纯扫描耗时或真实网络吞吐保证。每组交替运行三次，列中位数，原始样本见 [delimiter-performance-macos-arm64.json](evidence/delimiter-performance-macos-arm64.json)。

| 回复大小 | 修复前 | 修复后 | 耗时变化 |
| --- | --- | --- | --- |
| 8 MiB | 44.51 ms | 47.57 ms | 增加约 6.9% |
| 32 MiB | 219.74 ms | 176.57 ms | 减少约 19.6% |
| 128 MiB | 2400.53 ms | 698.20 ms | 减少约 70.9% |

收益集中在大回复，不能声称所有负载都更快。小负载仍有约 3 ms 的退化；关闭服务端 Nagle 后仍存在，因此未将其归因于 Nagle 或直接排除为噪声。基准显式提高 `MaxResponseBytes`，生产默认 1 MiB 上限未改变。

新增回归覆盖：重复前缀分隔符跨多个 UDP 包、包含/剥离分隔符、提前匹配后的清理和下一帧、GB18030 文本分隔符、含 NUL 的字节分隔符、1 MiB TCP 分片回复及剩余帧。Release（锁定依赖）和 ASan/UBSan 的四项 CTest 均通过，`git diff --check` 通过。该轮未重新运行跨平台 CI、真实 SSH/SFTP、Android 或串口硬件；不能据此断言整个库在所有负载下均无性能瓶颈。

复跑大回复基准：

```sh
cmake --build build-locked --parallel 4
ctest --test-dir build-locked --output-on-failure
build-locked/cd_tests --delimiter-mebibytes 8
build-locked/cd_tests --delimiter-mebibytes 32
build-locked/cd_tests --delimiter-mebibytes 128
```

## 第二轮：文本、调度与 C# 包装性能（2026-10-14）

修复前快照包含上一节的分隔符优化（`d8955a9` 加当时未提交的修改），不是直接拿旧提交替代本轮基线。Native 前后使用同一 Release 基准可执行文件和锁定依赖，只切换动态库；C# 前后使用同一 net10 基准源码分别编译旧、新包装层。每个负载串行交替运行三次，表内为中位数。全部原始样本和源码/二进制摘要见 [performance-round2-macos-arm64.json](evidence/performance-round2-macos-arm64.json)。

| 路径与负载 | 修复前 | 修复后 | 耗时减少 |
| --- | --- | --- | --- |
| 8 MiB `ExecuteUntil` 文本 Contains，直到协议结束仍不匹配 | 546.23 ms | 49.13 ms | 91.0% |
| 8 MiB `ReadExactlyString` | 691.64 ms | 54.09 ms | 92.2% |
| 同一取消源，提交并保留 16,000 个未完成操作 | 661.97 ms | 2.70 ms | 99.6% |
| 同一 Driver 创建并保留 16,000 个连接 | 152.97 ms | 2.34 ms | 98.5% |
| 取消已调度的 8,000 个批次项，另有 3 项未调度 | 279.96 ms | 2.45 ms | 99.1% |
| C# 结构体初始化 1,000,000 次 | 26.62 ms | 2.58 ms | 90.3% |
| C# UTF-8 编码：256 KiB ASCII × 128 | 5.12 ms | 1.31 ms | 74.4% |
| C# UTF-8 解码：256 KiB ASCII × 128 | 7.18 ms | 4.45 ms | 38.0% |

取消订阅的 4k/8k/16k 修复后分别为 0.50/1.29/2.70 ms；连接注册分别为 0.54/1.17/2.34 ms；批次取消的 2k/4k/8k 分别为 0.65/1.24/2.45 ms。对照项也保留在 JSON：不使用取消源的 16k 提交为 2.32→2.33 ms，普通 1 MiB 分隔符接收为 7.04→6.98 ms。没有对未改变的路径声称普遍加速，也不使用跨机器固定耗时作为测试通过条件。

C# 当前线程分配量：结构体初始化 64,005,288→40 字节（后者为计时器）；编码 33,625,936→14,376 字节；解码 100,767,936→67,111,976 字节。解码仍需创建最终 .NET string，消除的是中间 byte[]。这些是热路径微基准，不代表完整 C# 网络请求延迟，也不能外推为 net48 的实测收益。

实现与边界：

- 文本解码按请求缓存已消费的原始字节、解码文本和 Unicode scalar 数，只处理新字节及不足一个字符的尾部；Contains 保留跨块匹配所需的重叠范围。检查采用只读视图，避免为字节检查或隐藏协议分隔符复制完整回复。
- Regex/Pattern 复用增量解码，但每次正则匹配仍使用完整文本，保留锚点、不定长匹配与捕获组语义；未宣称任意正则模式都变成线性增量匹配。
- 取消订阅和连接注册只在增长阈值到达时清理弱引用，清理后按存活数设置下次阈值，避免每次插入全表扫描；并未取消已完成/过期项的清理。大并发峰值之后允许暂存弱引用元数据，其量受此前峰值约束。
- 批次取消只遍历并通知一次，后续完成回调只收集结果。已调度项仍返回 Cancelled，未调度项仍为 NotStarted，结果顺序和恰好一次完成不变。
- C# 缓存每个原生结构体的零值模板（含 `struct_size`），每次返回值副本。UTF-8 使用有长度的指针重载，保留严格非法编码检查、嵌入 NUL、空值和原生结果生命周期；[Encoding.GetString](https://learn.microsoft.com/en-us/dotnet/api/system.text.encoding.getstring) 与 [Encoding.GetBytes](https://learn.microsoft.com/en-us/dotnet/api/system.text.encoding.getbytes) API 已核对，net48/net10 实际编译通过。

验证：

- Release 四项 CTest 通过；ASan/UBSan、ThreadSanitizer 四项各三轮通过，并对最后补充的回归再次检查。
- 新增 UTF-8（含补充平面字符）/GB18030（含 U+0080 四字节序列）跨块 scalar、Contains/Regex、Pattern 边界、stderr、后续帧，以及非法/不完整编码的部分结果回归；包含前一轮分隔符测试。
- 新增 512 个待完成操作的共享源取消、512 个注册连接随 Driver 关闭、受控大批次取消回归；既有并发取消、保留已完成操作、生命周期测试继续运行。
- C# net48/net10 编译：0 warning / 0 error；net10 包装层测试验证零值结构体、并发转换、非法 UTF-16/UTF-8、截断 UTF-8、超长结果拒绝、空字符串和嵌入 NUL。
- C# 压力：44,000 次操作、1,408 次并发关闭，20 个已释放包装对象全部可回收。真实回环 OpenSSH/SFTP 连续三轮通过，包含 host key 校验、持久/单次、提前匹配清理与文件一致性。
- 托管互操作回归已加入 CI 配置；上述测量与验证来自本机，跨平台结果以对应提交的 Platform CI 为准。net48 本机仅编译；真实 Android、实体串口未验证。

复跑：

```sh
cmake --build build-locked --parallel 4
ctest --test-dir build-locked --output-on-failure
build-locked/cd_tests --text-performance
build-locked/cd_stress_tests --performance
build-locked/cd_tests --batch-cancel-count 8000
dotnet run --project tests/dotnet/Performance -c Release
dotnet build tests/dotnet/Stress -c Release
DYLD_LIBRARY_PATH="$PWD/build-locked" dotnet tests/dotnet/Stress/bin/Release/net10.0/Stress.dll 10
python3 scripts/test_ssh_local.py --build build-locked --user-server --repeat 3
```

保留修复前的动态库及两份 C# 基准输出后，可用 `scripts/compare_performance.py --build <基准构建目录> --before-native <旧库目录> --after-native <新库目录> --before-managed <旧包装层Performance.dll> --after-managed <新包装层Performance.dll> --output <结果.json>` 交替复测。C# 基准项目支持 `-p:DriverSourceRoot=<包装层源码目录>`，用于让两侧复用相同测试程序。TCP 大回复显式调高响应上限，默认 1 MiB 未改变；这些结果来自本机回环与受控排队负载。

## 第三轮：取消队列、结果发布与托管选项快照（2026-10-14）

从 `7b9469c` 重新审查原生请求入队/开始/关闭、取消源与操作发布、批次调度、收发缓冲、SSH/SFTP 文件任务，以及 C++/C# 包装层生命周期。确认并修复以下五处问题：

- 已取消的 token 在请求入队前发出取消通知，原先通知找不到请求就丢弃；当连接已有阻塞读取时，新请求会一直留在队列。现在入队后重新检查取消状态，立即完成该请求，不影响正在执行的请求。
- 已取消空批次会同时排入取消调度和初始调度，第二次调度改写已发布结果的 `elapsed_ns`，与调用方读取存在竞争。现在批次完成后不再调度或修改结果。
- 从队尾或队中取消请求时，原先每次遍历整个 deque 查找，逆序取消总成本为 O(N²)。现在用 list 的稳定迭代器直接删除，开始执行和关闭时同步清除队列成员状态；单次定位/删除为 O(1)。代价是每个排队节点的独立分配和迭代器元数据。
- 事件循环阻塞期间已超过截止时间的请求，原先可能先执行后端操作再收到计时器事件。现在开始执行前检查绝对截止时间，超时的 Open 不会启动后端。
- 七种 C# Read*Async 回调原先重新读取可变 `CallOptions.ReadStream`；提交后复用并修改 options 会让回调取错流。现在捕获提交时的读取流，不再于完成回调访问该可变字段。调用返回后修改 options 的回归已覆盖全部七种读取接口；这不承诺多个线程在提交过程中同时修改配置对象是安全的。

三个原生功能缺陷及 C# 选项缺陷都先在旧实现复现失败，再在修复后通过。测试使用事件循环屏障控制顺序；截止时间测试仅在屏障内等待截止时间过去，不以机器性能快慢作为判定。回归继续检查预取消不影响活动读取、恰好一次批次回调、已交付结果不变、队列取消状态及随后关闭。

取消性能对比：macOS ARM64，相同锁定依赖的 Release 构建，同一测试程序交替加载旧、新动态库，每组串行三轮中位数。计时包含取消通知、事件循环处理及完成屏障。原始样本与摘要见 [performance-round3-macos-arm64.json](evidence/performance-round3-macos-arm64.json)。

| 逆序取消的排队请求数 | 修复前 | 修复后 |
| --- | --- | --- |
| 2,000 | 1.369 ms | 0.981 ms |
| 4,000 | 2.786 ms | 1.315 ms |
| 8,000 | 9.734 ms | 2.693 ms |
| 16,000 | 38.913 ms | 5.338 ms |
| 32,000 | 150.775 ms | 12.691 ms |

本机验证：Release 四项 CTest 通过；ASan/UBSan 和 ThreadSanitizer 四项各三轮通过；C# net48/net10 编译零警告/错误，net10 七种读取接口、预取消队列、既有互操作回归全部通过；托管压力 44,000 次操作、1,408 次并发关闭、20 个释放对象全部可回收；真实回环 OpenSSH/SFTP 三轮通过。macOS 的 ASan 不支持 `detect_leaks=1`，该参数导致测试启动失败后已移除；本轮不声称完成了 LeakSanitizer 检查。

新增托管集成回归已接入六平台 CI 配置（Windows x86 使用实际 x86 host）；上述测量与验证来自本机，跨平台结果以对应提交的 Platform CI 为准，net48 本机只编译。真实 Android、实体串口仍未验证。任意 Regex/Pattern 的完整文本匹配成本、子进程启动、操作系统及远端服务开销仍取决于负载，不能由本轮测试推断所有场景都没有瓶颈。

`git diff --check` 与 actionlint 工作流语法/表达式检查通过。完整 actionlint 仍报告原有 macOS 工具链脚本的五条 ShellCheck 引号/风格提示；对比 `7b9469c` 的工作流，提示完全相同，本轮未改动该脚本。

复跑新增回归与基准：

```sh
build-locked/cd_stress_tests --precancelled
build-locked/cd_stress_tests --batch-snapshot
build-locked/cd_stress_tests --expired
build-locked/cd_stress_tests --reverse-cancel 32000
DYLD_LIBRARY_PATH="$PWD/build-locked" dotnet run --project tests/dotnet/Stress -c Release -- --regressions-only
```

## 第四轮：持续高并发、内存与释放竞态（2026-10-14）

本轮以 `0b8f7bd` 为基线，在 macOS ARM64 上增加持续负载及内存采样。原生压力复用同一 Driver、8 个 UDP 连接和长寿命取消源，每轮 32 个生产线程提交 4,096 次请求，交错执行单操作取消、共享取消、短截止时间和连接关闭，并验证每个已接受操作恰好一次回调。每轮重新打开、排空和关闭连接，10 秒记录一次存活分配；外部 watchdog 每 5 秒采样 RSS、线程与文件描述符。混合负载复用 32 路 TCP/UDP/CLI/虚拟串口连接，执行完整批次并检查回复。

三项主要持续负载均跑满 600 秒并成功退出，未触发 watchdog；另一次修复前的托管基线完成 49,111,040 次请求。汇总、资源窗口及二进制 SHA256 见 [soak-round4-macos-arm64.json](evidence/soak-round4-macos-arm64.json)。

| 负载 | 规模 | 结果 |
| --- | --- | --- |
| 原生取消/关闭 | 84,105 轮，344,494,080 次请求 | 每请求恰好一次回调，重开及终态验证通过 |
| 32 路混合通信 | 1,110,424 个批次，35,533,568 次操作 | 所有批次与回复验证通过；线程 14、文件描述符 109 保持稳定 |
| 修复后 C# 取消/关闭 | 16,990 轮，69,591,040 次请求 | 全部完成；另有 60 轮并发释放、122,880 次伴随读取，118 个已检查包装对象均可回收 |

原生每轮排空后的 malloc 存活分配为 677,504–721,664 字节，释放句柄后为 96,656 字节；RSS 则从约 18 MiB 阶梯升至约 29 MiB。测试累计创建/回收 2,691,360 个生产线程，线程/FD 数保持有界；结合存活分配和 leaks 结果，没有确认库的持续存活内存增长，不能仅把 RSS 高水位直接认定为泄漏或宣称 RSS 完全不增长。修复后 C# 在 30 秒预热后的 GC 存活量为 662,392–702,408 字节，末次 GC 为 669,968 字节，未见持续上升。上述有限时长不能证明所有负载下都没有泄漏。

补查释放边界时确认并修复两处 C# 问题，均有旧实现失败记录：

- Driver 已 Dispose 后再 Dispose Connection，后者在租用父句柄时抛出 ObjectDisposedException。连接释放现在允许父驱动已关闭。
- 多个线程同时 Dispose Driver，初始 IsClosed 检查与后续句柄租用之间存在竞态，较晚调用同样会抛出 ObjectDisposedException。驱动及连接释放现在只在相关句柄确已关闭时忽略该异常，仍在 finally 释放自己的 SafeHandle；其他关闭错误继续传播，普通 IO 的已释放检查保持不变。

确定性的父先释放回归及 1,000 轮、每轮 32 路 Driver.Dispose 在修复前失败、修复后通过。持续托管负载复用 8 个连接、32 个生产者及长寿命 CancellationToken，每轮 4,096 次请求；每 10 秒额外让 32 路读取与 16 路连接/驱动 Dispose 竞争，并在后续轮次 GC 后检查 WeakReference。不能在刚完成的 async 调用栈仍存活时断言其局部对象已回收，因此回收检查延后一轮；最后一个尚未经过后续轮次的对象组不计入已回收数量。

批次竞态使用事件循环屏障，确保 1,024 个批次项已准入、另外 3 项尚未准入，再启动 32 路取消/连接关闭/驱动关闭。1,000 轮全部通过，已准入项只允许 Cancelled 或 Closed，未准入项保持 NotStarted，所有关闭操作完成，报告完整。该测试的短版加入 native_behavior；持续取消/关闭的 2 秒版本加入 CTest；C# --regressions-only 包含新的释放及短时负载回归，沿用既有六平台 CI 入口。

Release 五项 CTest 通过；ASan/UBSan 和 TSan 五项各三轮通过，最终压力指标调整后的短测也通过；net48/net10 包装层构建零警告/错误；既有托管互操作、44,000 次 CLI 操作与 1,408 次并发关闭、20 个对象回收通过；本机真实 OpenSSH/SFTP 回归 20 轮通过。ASan/UBSan 和 TSan 专项各持续 180 秒，分别完成 11,444,224 和 6,905,856 次操作，无 sanitizer 诊断。macOS leaks 另跑 20 秒原生压力并在退出时扫描，报告 0 leaks / 0 leaked bytes。macOS LeakSanitizer 不可用，不将 leaks 等同于 LeakSanitizer。

ASan/TSan 替换分配器后，malloc_zone_statistics 的计数不适合作为存活分配量；最初 sanitizer 长测日志保留其原始输出，但结论不使用这些值，最终测试已在 sanitizer 构建禁用该输出。常规构建的存活分配、实际 RSS 和托管 GC 存活量分别记录，不混为同一指标。

本轮属于本机无硬件压力证据；真实 Android/实体串口、其他操作系统的新回归及小时级/隔夜负载未由本轮验证。多个压力进程曾同时运行，操作量是正确性负载规模，不作为吞吐性能对比。

复跑（从仓库根目录，先完成相应构建）：

```sh
python3 scripts/run_stress.py --output artifacts/soak/native.json --timeout 660 --interval 5 -- build-locked/cd_stress_tests --soak-seconds 600
python3 scripts/run_stress.py --output artifacts/soak/mixed.json --timeout 660 --interval 5 -- build-locked/cd_tests --soak-seconds 600
python3 scripts/run_stress.py --output artifacts/soak/managed.json --library-dir build-locked --timeout 660 --interval 5 -- dotnet tests/dotnet/Stress/bin/Release/net10.0/Stress.dll --soak-seconds 600
build-locked/cd_tests --batch-close-rounds 1000
DYLD_LIBRARY_PATH="$PWD/build-locked" dotnet tests/dotnet/Stress/bin/Release/net10.0/Stress.dll --parent-disposed
DYLD_LIBRARY_PATH="$PWD/build-locked" dotnet tests/dotnet/Stress/bin/Release/net10.0/Stress.dll --driver-dispose-race
```

`--library-dir` 在 Python 启动子进程时设置动态库目录，避免 macOS 系统 Python 启动时清除 DYLD_LIBRARY_PATH；采样同时记录测试程序集和原生库 SHA256。完整运行日志和 5 秒采样保留在忽略目录 `artifacts/soak-round4/`。

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
