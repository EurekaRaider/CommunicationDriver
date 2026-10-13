# 依赖、工具链与许可

固定源码版本：Boost 1.92.0（Boost Software License 1.0）、libssh2 1.11.1（BSD-3-Clause）、RE2 2025-11-05（BSD-3-Clause）、Abseil 20250512.1（Apache-2.0）。Windows libssh2 使用系统 WinCNG，Unix 使用系统 OpenSSL。adb 由使用者提供官方 Android Platform Tools，不打包或启动/停止全局 server。

CMake 3.28+；C++23 必需（expected、coroutine、span、stop_token 特性探测）。C# SDK 10，net48 引用程序集 1.0.3。具体编译器与平台通过情况以验证报告和 CI 为准。

Boost.Asio、Boost.Process、libssh2、RE2、CMake、.NET API 已按 ctx7 library/docs 核对；固定版本源码为最终构建依据。

libssh2 1.11 无法区分未收到 exit status 和零：单次 SSH 返回零时 ExitCode 保持 null，非零保留。持久托管 shell 的标记提供实际命令 status。libssh2 1.12+ 可使用 has_exit_status，本项目不假设其存在。

官方依据：[Boost 1.92](https://www.boost.org/releases/1.92.0/)、[libssh2 CMake](https://github.com/libssh2/libssh2/blob/libssh2-1.11.1/docs/INSTALL-CMAKE.md)、[RE2](https://github.com/google/re2/tree/2025-11-05)、[CMake](https://cmake.org/cmake/help/latest/)、[.NET native interop](https://learn.microsoft.com/en-us/dotnet/standard/native-interop/best-practices)。

分发包必须携带依赖 LICENSE/COPYING 与链接的运行库。公共源码不包含本机测试密钥、账号配置和硬件数据。
