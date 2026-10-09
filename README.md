# Modbus Control

See [PLAN.md](PLAN.md) for the design.

## Build

Requirements: MSVC 2022, CMake 3.25+, Ninja, vcpkg (`VCPKG_ROOT`, default `C:/Users/franc/source/vcpkg`),
prebuilt Qt 6.10.3 (`QT_ROOT`, default `C:/Qt/6.10.3/msvc2022_64`).

From a "x64 Native Tools" / `vcvars64.bat` shell:

```
cmake --preset msvc-debug
cmake --build --preset msvc-debug
ctest --preset msvc-debug
```

Binaries and DLLs end up in `build/<preset>/bin`.