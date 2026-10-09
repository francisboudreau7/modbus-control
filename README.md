# Modbus Control

Modbus Control is a Windows desktop client for reading and writing Modbus TCP registers. It polls
a server, shows the values in a table and as a live trend graph with two measurement cursors, and
can write holding registers. The graph can be exported as PNG, SVG or PDF, and the recorded data as
CSV. Projects are saved as `.mbproj` files. The repository also contains a test server that
simulates signals and can inject delays and dropped connections.

![Main window](docs/screenshot.png)

## Features

- Modbus TCP client for one server at a time (IPv4 address or `localhost`, unit id, timeout, poll period)
- Holding registers (FC03 read, FC06/FC16 write) and input registers (FC04 read)
- 16-bit (`int16`, `uint16`) and 32-bit (`int32`, `uint32`, `float32`) decoding with gain and offset
  scaling; 32-bit values use two registers with a selectable byte order (ABCD, CDAB, BADC, DCBA)
- Tag table with per-tag colour, plot and writable flags, raw and scaled values, and quality
- Writes with a confirmation step, single register or a group of consecutive registers
- Live trend with 60 s, 5 min, 30 min and All windows, zoom, pan, pause and fit-Y
- Two draggable cursors with Δt, 1/Δt and per-tag Y@C1, Y@C2, ΔY
- Export of the graph (PNG, SVG, PDF, optional cursors and readout box) and of the data (CSV)
- Project files (`.mbproj`, JSON) with New, Open, Save and Save As
- Test server `modbus_test_server` with simulated signals, a configuration file and fault injection

## Quick start

1. Build the project (see [Build from source](#build-from-source)), or use an existing build.
2. Start the test server in a console:

   ```bat
   build\msvc-debug\bin\modbus_test_server.exe --port 1502
   ```

   The server listens on 127.0.0.1 (local connections only) by default. Add `--host 0.0.0.0`
   to accept remote clients; it has no authentication.

3. Start the application, with or without a project file:

   ```bat
   build\msvc-debug\bin\modbus_control.exe examples\demo.mbproj
   ```

   The application starts with the demo tags (Sine, Triangle, Counter, Square, Noise, Sawtooth,
   Lag and Setpoint) and the connection 127.0.0.1:1502, unit 1, with a 200 ms period.

4. Click **Connect**. The values update in the table and the trend starts to draw. Edit the
   **Value** cell of **Setpoint** (HR 100) to write a value. The **Lag** signal follows it.

See [docs/USER_GUIDE.md](docs/USER_GUIDE.md) for the full description of the application, and
[docs/TEST_SERVER.md](docs/TEST_SERVER.md) for the test server.

## Build from source

### Requirements

- Visual Studio 2022 Build Tools with the C++ workload (MSVC)
- CMake 3.25 or newer, and Ninja (bundled with Visual Studio)
- vcpkg, with the `VCPKG_ROOT` environment variable set to its folder. The manifest in `vcpkg.json`
  installs libmodbus.
- Qt 6.10.3, `msvc2022_64` kit. Install it with aqtinstall:

  ```bat
  pip install aqtinstall
  aqt install-qt windows desktop 6.10.3 win64_msvc2022_64 -O C:\Qt
  ```

  This creates `C:\Qt\6.10.3\msvc2022_64`, which is the default in `CMakePresets.json`.
- Internet access during configuration. CMake downloads the Qwt 6.3.0 source archive (checked by
  SHA-256) and builds it.

### Commands

Run these from an "x64 Native Tools Command Prompt for VS 2022" (or after `vcvars64.bat`):

```bat
cmake --preset msvc-debug
cmake --build --preset msvc-debug
ctest --preset msvc-debug
```

Use `msvc-release` instead of `msvc-debug` for a release build.

Binaries and DLLs are placed in `build\<preset>\bin`.

The presets set `QT_ROOT` to `C:/Qt/6.10.3/msvc2022_64`. To use another Qt installation, pass
`-DQT_ROOT=<path>` to the first `cmake` command; a value given on the command line overrides the
preset.

### Installer

A Windows installer (Inno Setup 6) is built by `installer\build_installer.ps1`. It runs
`cmake --install` into `out\install` and compiles `installer\modbus_control.iss` into
`out\installer\ModbusControl-<version>-setup.exe`. The installer supports per-user and all-users
installs and associates `.mbproj` files. See [installer/README.md](installer/README.md) for the
prerequisites, the silent install and uninstall commands, and what is installed.

## Project layout

```
CMakeLists.txt, CMakePresets.json, vcpkg.json   top-level build
cmake/Qwt.cmake                                 fetches and builds Qwt 6.3.0
src/core/                                       mbcore library: Modbus connection, polling, recording,
                                                project and CSV code; no UI
src/app/                                        modbus_control.exe: window, tag table, trend, export dialogs
server/                                         modbus_test_server.exe and the mbsim library
tests/                                          QtTest programs (unit, UI and integration)
docs/                                           USER_GUIDE.md, TEST_SERVER.md
examples/                                       demo.mbproj
PLAN.md, TASKS.md                               design notes and task breakdown
```

## Tests

`ctest --preset msvc-debug` runs all of the tests below. Each is a QtTest executable (or a command
line check) registered with CTest.

| Kind | Tests |
|---|---|
| Unit (core) | `tst_tagcodec`, `tst_blockplanner`, `tst_recorder`, `tst_projectio`, `tst_csvexporter` |
| Unit (server) | `tst_signalgen`, `tst_serverconfig` |
| UI (run offscreen) | `tst_tagtablemodel`, `tst_cursorpanel`, `tst_exportimage` |
| Integration | `tst_integration`: starts `modbus_test_server` on a free port and talks to it |
| Smoke | `tst_smoke`, `app_smoke` (starts the application and exits), `server_version` |

To run one test, use its name with `-R`:

```bat
ctest --preset msvc-debug -R tst_projectio
```

Failing output is shown automatically (`outputOnFailure` is set in the test preset).

## Licenses

Modbus Control links these libraries dynamically (DLLs next to the executables):

- libmodbus, LGPL-2.1 or later
- Qt 6, LGPL-3.0
- Qwt 6.3.0, "Qwt License" (LGPL 2.1 with exceptions)
