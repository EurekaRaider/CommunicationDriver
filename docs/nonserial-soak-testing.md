# 非串口高强度并发压测

本入口针对 macOS 上的当前构建，同时运行 TCP、UDP、本地 CLI、真实回环 OpenSSH/SFTP 和两台 USB Android。本轮每项持续混合阶段为 1,800 秒（按用户更新的 30 分钟要求）；初始化、功能前置、回归和清理另计。实体串口不在此入口范围内。

## 负载与判定

| 连接方式 | 独立 Driver | 同时工作的任务 |
| --- | ---: | --- |
| TCP | 4 | 32 路文本/二进制命令、2 路 1 MiB 收发、4 路取消/超时/重开 |
| UDP | 4 | 32 路文本/二进制命令、2 路 8 KiB 报文、4 路取消/超时/重开 |
| CLI | 4 | 24 路 Persistent、8 路 OneShot、2 路 stdout/stderr 流、4 路取消/超时/重开 |
| SSH/SFTP | 4 | 24 路 Persistent、8 路 exec、2 路 stdout/stderr 流、4 路 SFTP、4 路取消/超时/重开 |
| ADB，每 slot | 4 | 24 路 Persistent、8 路 OneShot、4 路 push/pull、logcat、dmesg、受控输出流、4 路取消/重开 |

合计 242 个负载任务；ADB 两个 slot 各 43 个。各组先等待连接与测试文件就绪，再开始完整计时。TCP/UDP 回环服务为实际 socket 收发；SSH 使用实际隔离 OpenSSH 服务和 libssh2。

文本携带任务和序号标记，二进制与文件逐字节比对；SSH/CLI 另核对 stderr、非零退出码、早匹配后的尾部归属和恢复结果。SFTP 每路往返 1 MiB，ADB 每路往返 4 MiB。UDP 每个工作任务有独立服务端端口，避免共用单个接收队列制造假丢包；8 KiB 在本机 9,216 字节的 UDP 报文限制以内，不修改系统网络参数。

每个工作任务都必须实际完成 IO。网络/命令负载另检查任务超过 60 秒不推进；外部采样器对所有负载检查 120 秒无日志推进和总超时。任一断言、非零退出、超时、已观测子进程残留或运行库指纹变化均判失败，并保留失败产物。预期的取消/操作超时必须返回对应状态。

## 执行

先准备 CMake/Ninja/C++23 工具链、ADB，以及带 `psutil==7.0.0` 的 Python 环境。按 [ADB 真机说明](adb-hardware-testing.md) 连接两台在线设备。以该 Python 运行：

```sh
python scripts/run_all_transport_soak.py \
  --build build-locked \
  --adb /absolute/path/to/adb \
  --output artifacts/soak-current/half-hour-001 \
  --seconds 1800
```

每次使用新的输出目录。入口会先构建并执行 CTest 和 OpenSSH/SFTP 功能回归，再启动五组长压。Mac 使用 `caffeinate` 保持运行。SSH 服务只监听回环地址 22482，使用忽略目录内的测试密钥和明确 SHA256 主机信任；不修改系统 SSH 配置，不需要 sudo。隔离服务的 `MaxStartups 128` 只用于承载并发认证。

要持续读取真实 `dmesg -w`，设备必须允许读取内核日志。此入口在权限不足时会执行 `adb root`，因此运行前需要设备所有者授权；会记录原始 uid，并在结束或失败后恢复原始 shell 权限。两台设备的本次执行已有用户明确授权。不会 remount、重启设备、安装软件或清空系统日志。

SSH 单项长压可复用同一入口组件：

```sh
python scripts/test_ssh_local.py --build build-locked --user-server \
  --soak-seconds 1800 --output artifacts/ssh-soak/half-hour-001
```

当前固定依赖 libssh2 1.11.1 无法区分 exec 成功退出的 0 和未收到退出状态，因此 SSH OneShot 的零退出码保持可空；检查输出、完成原因及非零退出码，另统计未知退出码。Persistent 的帧化退出码仍需精确匹配。此规则来自已有接口约定。

## 证据

忽略目录包含运行库/测试程序/源码 SHA256、各组日志和资源采样、各 slot 进度与真实 ADB 子进程并发采样、设备权限恢复和清理结果。`suite-result.json` 仅在全部工作负载、采样器和清理检查通过时写入 `passed: true`。

原始日志可能含设备标识、用户名、测试密钥指纹和本机路径，禁止提交。公开报告仅保留匿名 slot、统计数字和文件指纹。各项同时施压，所得操作数与延迟是本轮正确性压力证据，不用于吞吐性能对比。Mac 结果不扩展为其他主机系统、Android 版本、实体串口或隔夜运行结论。

## 本轮修复与补充验证

- SSH 关闭时先取消并 shutdown socket，保留 FD 所有权到 libssh2 对象清理完成，最后 close，防止清理写入被其他 Driver 复用的 FD。
- stdout/stderr 交替读取后，检查 libssh2 内部缓冲；已有数据时继续调度读取，避免等待不会再到来的 socket 事件。
- 固定依赖 libssh2 1.11.1 的 Curve25519/ECDH 重协商释放旧 host key；OpenSSL 3+ ECDH 释放独立拥有的临时对象。补丁见 `cmake/PatchLibssh2.cmake`，按精确源码片段匹配，重复配置不重复改写，依赖升级不匹配时明确失败。`CD_FETCH_DEPENDENCIES=OFF` 使用外部库，不自动修补该库。
- macOS 通过系统 `posix_spawn` 启动 CLI/ADB，避免高并发 fork 子进程在 exec 前进入 ObjC/ASan 回调；stdio、工作目录、环境和错误码继续保留，异步 IO 与进程等待仍由 Boost 管理。其他平台保留原启动路径。

重协商泄漏可用同一 SSH 压力入口加速复现：

```sh
python scripts/test_ssh_local.py --build build-locked --user-server \
  --soak-seconds 120 --output artifacts/ssh-soak/rekey-curve \
  --port 22483 --rekey-limit 1M --key-exchange curve25519-sha256
python scripts/test_ssh_local.py --build build-locked --user-server \
  --soak-seconds 120 --output artifacts/ssh-soak/rekey-ecdh \
  --port 22484 --rekey-limit 1M --key-exchange ecdh-sha2-nistp256
```

macOS 长压中的内存检查应使用 `leaks --forkCorpse -quiet PID` 的快照模式。直接扫描运行进程会暂停客户端数秒，干扰真实 IO 超时。快照分析不要求客户端停顿整个分析过程；命令延迟仍记录实际经历时间，保留测试本身的原始超时与断言，不调大超时来规避失败。
