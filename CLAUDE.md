# Modbus Control — notes for Claude sessions

Windows Modbus TCP client (Qt 6 Widgets + Qwt + libmodbus) with a live trend, cursors,
PNG/SVG/PDF and CSV export, and a libmodbus test server. Design: [PLAN.md](PLAN.md).
Task list, orchestration rules and **current status / what's left**: [TASKS.md](TASKS.md)
(section "Status and remaining work" at the end — read it first when resuming).

## Toolchain on this machine
- MSVC 2022: `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools` (VS 2019 is also
  installed — never use it). CMake 3.31 + Ninja come with it (on PATH after vcvars64).
- Qt 6.10.3 prebuilt (aqtinstall): `C:/Qt/6.10.3/msvc2022_64` (preset cache var `QT_ROOT`).
- vcpkg: `C:\Users\franc\source\vcpkg`; `VCPKG_ROOT` is a user env var, but processes started
  before it was set don't see it — pass it explicitly (below). Only libmodbus comes from vcpkg.
- Qwt 6.3.0 is fetched and built by `cmake/Qwt.cmake`.
- Git: `C:\Program Files\Git\cmd\git.exe`; GitHub CLI: `C:\Program Files\GitHub CLI\gh.exe`
  (signed in as francisboudreau7). The Claude harness process may not have either on PATH —
  prepend `C:\Program Files\Git\cmd` to PATH or use full paths. Remote: `origin` =
  https://github.com/francisboudreau7/modbus-control (private). Push `main` after each merge.
- Inno Setup 6: `%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe`.

## Build and test (PowerShell 5.1 — no `&&` in PowerShell itself)
```
cmd /c 'set VCPKG_ROOT=C:\Users\franc\source\vcpkg&& "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --preset msvc-debug && cmake --build --preset msvc-debug && ctest --preset msvc-debug'
```
Keep the `&&` chain (or check the exit code): a gate that runs ctest after a failed build tests
stale binaries and reports a false pass. Outputs land in `build/<preset>/bin`. `ctest -E tst_integration` skips the slow integration
suite (~15 s, starts `modbus_test_server`). Installer: `installer\build_installer.ps1`.

## Conventions
- C++20, namespace `mb` (client, `src/core` = `mbcore`, `src/app` = `mbui`) and `mbsim`
  (`server/`). 4-space indent, `/W4 /permissive- /utf-8` via `mc_warnings` — keep zero warnings
  in our code (the only accepted warning is C5055 in Qwt's `qwt_date.cpp`).
- Public headers are contracts with doc comments; implementations follow them. The
  orchestrating session owns contract changes; sub-agents may change only private sections.
- Every module has QtTest unit tests in `tests/` (UI tests run offscreen). Integration tests in
  `tests/tst_integration.cpp` start the real test server on `--port 0`.
- Tag addresses are zero-based protocol addresses everywhere in code and project files.

## Orchestration (see TASKS.md §1–2)
Opus orchestrates; Sonnet gets concurrency/libmodbus/model-view/plot work, Haiku gets small
fully-specified tasks. Each task runs in its own worktree `..\modbus-control-wt\tNN` on branch
`task/tNN` (created manually with git; the built-in worktree isolation didn't work here),
commits there, and the orchestrator reviews, merges with `--no-ff`, rebuilds and runs ctest.

## Safety notes
- Don't drive the user's desktop with synthetic mouse/keyboard input (an earlier attempt typed
  into the user's browser). Verify the GUI with offscreen QtTests or ask the user to try it.
- Publishing a GitHub release is outward-facing: confirm with the user first.
