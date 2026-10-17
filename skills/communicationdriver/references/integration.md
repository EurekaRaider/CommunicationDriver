# Build and package integration

Use the skill shipped with the consumer's library version. Native and NuGet
packages include `skills/communicationdriver/`; the separate archive is
`CommunicationDriver-Skill-<version>.zip`. Copy the whole communicationdriver
folder into the target agent's skills directory. Keep SKILL.md and references
together. The skill does not install binaries or require a particular agent.

## C / C++ through CMake

Use installed headers/libraries and the exported target:

```cmake
cmake_minimum_required(VERSION 3.28)
project(consumer LANGUAGES CXX)
find_package(CommunicationDriver CONFIG REQUIRED)
add_executable(consumer main.cpp)
target_compile_features(consumer PRIVATE cxx_std_23)
target_link_libraries(consumer PRIVATE CommunicationDriver::communicationdriver)
```

Configure with CMAKE_PREFIX_PATH pointing to the install prefix. For C use
LANGUAGES C, main.c, c_std_99. Raw native RID archives contain include/ and
runtimes/<rid>/native/ (plus lib/<rid>/ import library on Windows); link those
directly when no CMake config is present.

Source builds need CMake 3.28+, Ninja, required C++23 features (expected,
coroutine, span, stop_token), and OpenSSL development files on Unix. Dependencies
are pinned by the project.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
cmake --install build --prefix install
```

CMake installation also places the skill at
`share/communicationdriver/skills/communicationdriver/` under the prefix
(or the configured CMAKE_INSTALL_DATADIR).

## C# through NuGet

Use the package from the same release or successful CI run. Do not assume it
exists on nuget.org; use the provided feed/package location. Release 0.1.2:

```xml
<Project Sdk="Microsoft.NET.Sdk">
 <PropertyGroup>
  <OutputType>Exe</OutputType>
  <TargetFramework>net10.0</TargetFramework>
  <CommunicationDriverRid>osx-arm64</CommunicationDriverRid>
 </PropertyGroup>
 <ItemGroup>
  <PackageReference Include="CommunicationDriver" Version="0.1.2" />
 </ItemGroup>
</Project>
```

Choose the actual process RID: linux-x64, linux-arm64, osx-arm64, osx-x64,
win-x64, win-x86. Unix can set RuntimeIdentifier or CommunicationDriverRid;
Windows also selects x86/x64 according to PlatformTarget. Native architecture
must match the process. net48 executes on Windows; .NET SDK 10 builds the
dual-target bindings. Existing packages.config projects import build targets.

ProjectReference builds only managed bindings; it does not automatically supply
native libraries. NuGet packages include native dependencies and build targets.
For manual loading, keep companion dependencies beside the native library and
configure the platform's loader path as necessary.

| Platform | Native library |
| --- | --- |
| Windows | communicationdriver_native.dll |
| Linux | libcommunicationdriver_native.so |
| macOS | libcommunicationdriver_native.dylib |

The managed assembly is CommunicationDriver.dll. Do not rename the native DLL
to that name. Loader/architecture failures cannot be fixed by changing command
mode. Check the native path, dependencies, ABI version, and process architecture
before changing transport behavior.
