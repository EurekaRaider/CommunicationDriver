# 架构与源码导航

所有通信方式通过同一个 `Connection` 提供既有的同步、异步和批处理接口。创建参数决定后端，通信实现分别保存在六个独立模块中。

## 源码边界

| 目录 | 职责 |
| --- | --- |
| `include/communicationdriver/` | 两个可独立交付的公开头文件：C ABI `.h` 和 C++23 `.hpp` |
| `src/api/cpp/` | 按类型组织的 C++ 包装源码；用于生成公开 `.hpp` |
| `src/c_api/` | C 导出函数、参数转换、异常转换和引用计数句柄 |
| `src/core/` | 运行时、操作、取消、连接状态、事务排队和批调度 |
| `src/execution/` | 回复边界、增量文本解码和响应检查 |
| `src/backends/` | 六种后端及类型化工厂 |
| `src/backends/shared/` | 异步进程管道、shell 标记与提示符处理 |
| `src/platform/` | 可执行文件查找和必要的平台进程启动适配 |
| `bindings/dotnet/CommunicationDriver/` | 按职责分组的 C# API、选项、结果、检查、批处理、错误与内部互操作 |

`src/core/backend.hpp` 定义统一内部契约。连接层只使用该契约以及能力、命令完成和通道复用特征，不识别具体后端类或数字编号。只有 `src/backends/backend_factory.cpp` 将类型化配置与具体实现关联。工厂仅用于创建或重开连接，不参与逐块数据处理。

连接按值保存 `BackendOptions` 中的一种配置，不同时持有六种后端的全部字段，也不为工厂闭包增加堆分配。支持 IPO/LTO 的工具链会在 Release 构建中恢复跨文件内联，避免模块拆分阻断原有优化。

## 六种通信方式

每个目录均有独立配置头文件、后端类头文件和实现文件。

| 目录 | 实现类 | 配置 | 负责的行为 |
| --- | --- | --- | --- |
| `src/backends/tcp/` | `TcpBackend` | `TcpConfig` | 解析地址、TCP 连接、异步字节流、半关闭 |
| `src/backends/udp/` | `UdpBackend` | `UdpConfig` | 固定对端、本地绑定、数据报边界与包长限制 |
| `src/backends/serial/` | `SerialBackend` | `SerialConfig` | 串口设备、波特率、数据位、校验和流控 |
| `src/backends/ssh/` | `SshBackend` | `SshConfig` | 主机信任、认证、会话和命令通道、SFTP |
| `src/backends/adb/` | `AdbBackend` | `AdbConfig` | 设备绑定、状态检查、ADB shell、push/pull |
| `src/backends/cli/` | `CliBackend` | `CliConfig` | 本地程序、shell、参数、工作目录和环境 |

ADB 和 CLI 组合使用 `ProcessChannel`，直接绑定原有输出回调，不增加逐块转发队列。SSH、ADB、CLI 使用共用 shell 完成标记规则。TCP、UDP、串口分别拥有各自的 I/O 资源；UDP 的数据报语义独立保留。所有后端仍共享同一运行时与连接调度规则。

## 对外单头文件

C 用户交付 `communicationdriver.h`，C++ 用户交付 `communicationdriver.hpp`，并提供对应平台和位数的库文件与运行依赖。内部目录不进入安装或原生包的 include 目录。原有 include 路径、C 导出、结构布局、C++ 类型和 C# 类型保持兼容。

`communicationdriver.h` 是 C ABI 声明来源；`src/api/cpp/` 是 C++ 包装源码来源。修改任一来源后运行：

```sh
python3 scripts/generate_cpp_header.py
python3 scripts/generate_cpp_header.py --check
```

生成文件提交到仓库，普通用户编译不需要 Python 或生成步骤。CI 检查生成文件同步；原生包测试为 C、C++ 分别创建只包含单个公开头文件的目录，并实际编译、链接和运行消费者。

## 验证约束

重构保持异步 I/O、缓冲区大小、增量匹配、完成信号和取消/关闭顺序。性能通过同一组公开 API 工作负载对比重构前后的动态库，采用交替顺序重复测量；功能验证包括完整 CTest、SSH/SFTP、托管回归、独立消费者、双 ADB 真机和 sanitizer。具体已执行范围与结果记录在验证报告，历史平台结果不替代当前版本测试。
