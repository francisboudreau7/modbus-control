# Windows installer

The installer is an Inno Setup 6 package that contains the runnable folder produced by
`cmake --install`: `modbus_control.exe`, `modbus_test_server.exe`, the Qt, Qwt and libmodbus DLLs,
the Qt plugins, the MSVC runtime, the example server configuration and the license texts.

## Prerequisites

- The same toolchain as the main README: Visual Studio 2022 Build Tools with the C++ workload,
  CMake 3.25 or newer, Ninja, vcpkg (`VCPKG_ROOT`) and Qt 6.10.3 `msvc2022_64`
  (`C:\Qt\6.10.3\msvc2022_64`, or `QT_ROOT`).
- Inno Setup 6, for example from <https://jrsoftware.org/isinfo.php>. The script looks for `ISCC.exe`
  on `PATH`, then in `%LOCALAPPDATA%\Programs\Inno Setup 6`, then in `%ProgramFiles(x86)%\Inno Setup 6`.

## Build the installer

From the repository root:

```bat
powershell -NoProfile -ExecutionPolicy Bypass -File installer\build_installer.ps1
```

Parameters:

- `-Preset` (default `msvc-release`): CMake preset to build. `msvc-debug` also works.
- `-Version` (default: read from `project(ModbusControl VERSION x.y.z)` in `CMakeLists.txt`).

The script runs these steps and stops with a message and a non-zero exit code if one fails:

1. Finds `vcvars64.bat` with `vswhere` (VS 2022 Build Tools path as fallback).
2. Runs `cmake --preset`, `cmake --build` and `cmake --install` in a `cmd` session with the MSVC
   environment. The build tree is `out\build` and the installed folder is `out\install`.
3. Compiles `installer\modbus_control.iss` with `ISCC.exe`.

The first configure downloads and builds Qwt, so it takes several minutes.

## Output

```
out\installer\ModbusControl-<version>-setup.exe
```

`out\` is git-ignored. Remove it to start from a clean build.

## What is installed

Default folder: `{autopf}\Modbus Control`. For a per-user install this is
`%LOCALAPPDATA%\Programs\Modbus Control`; for an all-users install it is `C:\Program Files\Modbus Control`.

```
bin\                       modbus_control.exe, modbus_test_server.exe, DLLs, Qt plugins
share\modbus-control\      server.example.json
licenses\                  third-party license texts
examples\*.mbproj         demo project and ABB CM50 example
docs\                      USER_GUIDE.md, TEST_SERVER.md
README.md
unins000.exe               uninstaller
```

Start menu entries: **Modbus Control**, **Modbus Test Server** (starts with
`--host 127.0.0.1 --port 1502`), **User Guide** and **Uninstall Modbus Control**. A desktop icon is
optional and off by default.

## Installation mode

The installer runs per user by default, without administrator rights. On the first page, or with the
`/ALLUSERS` switch, it can install for all users into Program Files (this asks for administrator rights).

## `.mbproj` file association

The installer registers `.mbproj` for the current user (or for all users in an all-users install):

- `HKCU\Software\Classes\.mbproj` default value `ModbusControl.Project`
- `HKCU\Software\Classes\ModbusControl.Project`: "Modbus Control project", with the icon from
  `bin\modbus_control.exe` and the open command `"<app>\bin\modbus_control.exe" "%1"`

Uninstalling removes these keys. The `.mbproj` key is removed only when it is empty afterwards, so
other settings for that extension are kept.

## Silent install and uninstall

Per-user silent install into a folder of your choice:

```bat
ModbusControl-1.3.0-setup.exe /VERYSILENT /SUPPRESSMSGBOXES /CURRENTUSER /NOICONS /DIR="C:\Tools\ModbusControl" /LOG="%TEMP%\mc_install.log"
```

`/CURRENTUSER` selects the per-user mode without a prompt. `/ALLUSERS` selects an all-users install and
needs administrator rights.

Silent uninstall:

```bat
"C:\Tools\ModbusControl\unins000.exe" /VERYSILENT /SUPPRESSMSGBOXES
```

The uninstaller can also be started from **Settings > Apps** or from **Programs and Features**.
