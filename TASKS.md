# Modbus Control — Task Breakdown & Sub-agent Orchestration

Companion to [PLAN.md](PLAN.md). Implementation has **not** started.

## 1. Roles

| Role | Who | Responsibilities |
|---|---|---|
| **Orchestrator** | Main session (Opus) | Writes the shared interface headers (the contracts). Writes each sub-agent prompt. Reviews diffs. Runs the full build and `ctest` after every wave. Merges worktrees and resolves conflicts. Does the final end-to-end check. Never hands off contract design. |
| **Sonnet** | `model: sonnet` | Threading, libmodbus integration, socket/`select()` loop, Qt model/view, interactive plotting, integration tests, toolchain troubleshooting. Anything where a subtle mistake builds fine but misbehaves at runtime. |
| **Haiku** | `model: haiku` | Small, fully specified, self-contained work with a clear test oracle: pure functions, serializers, simple widgets and dialogs, scripts, docs. |

### Model selection rule
Assign **Haiku** when *all* of the following hold:
- the task touches ≤ 3 files
- the inputs and outputs are fully specified by a header the orchestrator wrote
- there is no concurrency, networking, or Qt event-loop subtlety
- correctness can be checked with unit tests listed in the prompt

Otherwise assign **Sonnet**.

### Escalation
- If a Haiku task fails its acceptance check twice, reassign it to Sonnet with the failure log.
- If a Sonnet task fails twice, the orchestrator takes it over.
- A sub-agent that needs to change a contract header must stop and report. Only the orchestrator edits contracts.

## 2. Execution mechanics
- **Git:** T01 runs `git init`. Every wave ends with a commit by the orchestrator.
- **Parallel work:** tasks in the same wave run in parallel only if their file sets don't overlap.
  Each runs as a background `Agent` with `isolation: "worktree"`. The orchestrator merges the
  worktrees one by one and rebuilds after each merge.
- **Sequential work:** dependent tasks run after their prerequisites merge.
- **Prompt template** (filled in by the orchestrator for each task):
  1. Goal (1–2 sentences) and a link to the relevant PLAN.md section
  2. Files the agent **may** create or edit (explicit list); everything else is read-only
  3. Contract headers to implement or use (paths)
  4. Acceptance criteria: the exact build target and test names that must pass
  5. Rules: match existing style, no new dependencies, report what was done and what failed
- **Gate after each wave:**
  1. clean configure and build with `cmake --preset msvc-debug`
  2. `ctest` passes
  3. the orchestrator reviews the merged diff
  4. commit

## 3. Tasks

Legend: **H** = Haiku, **S** = Sonnet, **O** = Orchestrator. "Deps" lists the tasks that must be merged first.

### Wave 0 — Foundation (sequential)

| ID | Task | Model | Deps | Files | Acceptance |
|---|---|---|---|---|---|
| T01 | Scaffold the repo: `git init`, `.gitignore`, `vcpkg.json` (libmodbus only), `CMakePresets.json` (msvc-debug/release, Ninja, x64-windows dynamic triplet, `CMAKE_PREFIX_PATH=C:/Qt/6.10.3/msvc2022_64`), `cmake/Qwt.cmake` (FetchContent Qwt 6.3 source, build as shared lib), top `CMakeLists.txt` with targets `mbcore` (lib), `modbus_control` (app), `modbus_test_server`, `tests`. Placeholder sources: a window showing an empty QwtPlot; a server `main` that calls `modbus_new_tcp`. | S | — | root, `src/*/CMakeLists.txt` | Both executables build and start, and the libmodbus, Qt and Qwt DLLs are next to them |
| T02 | Contract headers: `Tag.h` (Area, ValueType, Quality, Tag struct), `SampleBatch.h`, `ModbusConnection.h`, `BlockPlanner.h`, `Recorder.h`, `PollWorker.h` (signals/slots), `ProjectIO.h`, `CsvExporter.h`, `server/SignalGen.h`, `server/ServerConfig.h`. Declarations and doc comments only. | O | T01 | `src/core/*.h`, `server/*.h` | Compiles |

### Wave 1 — Pure logic + server (parallel)

| ID | Task | Model | Deps | Files | Acceptance |
|---|---|---|---|---|---|
| T10 | Server config loader: parse `server.json` (port, unit ID, register ranges, signal assignments, tick period) into `ServerConfig`, with defaults when the file is missing | H | T02 | `server/ServerConfig.cpp`, `tests/tst_serverconfig.cpp` | Tests for defaults, full file and malformed file pass |
| T11 | Signal generators: sine, ramp, counter, square, noise, sawtooth, first-order lag. Pure functions of (t, params, state) that return int16 | H | T02 | `server/SignalGen.cpp`, `tests/tst_signalgen.cpp` | Tests check period, amplitude, wraparound and the lag's time constant |
| T12 | Server main loop: CLI parsing, `modbus_mapping_new_start_address`, `select()` loop with multiple clients, simulation tick that updates IR from `SignalGen`, IR 10 follows HR 100, `-v` request logging, clean Ctrl+C shutdown | S | T02 (stubs T10/T11 until merged) | `server/main.cpp`, `server/ServerLoop.*` | Two clients can read at the same time; written HR values persist; illegal address returns exception 2 |
| T13 | Fault injection: `--delay <ms>` before replying; `--drop <percent>` closes the client socket at random | S | T12 | `server/ServerLoop.*` | Manual check, then covered by T22 |
| T14 | Tag decoding: raw uint16 → int16/uint16 → scaled double; reverse encoding for writes, with range checks | H | T02 | `src/core/TagCodec.*`, `tests/tst_tagcodec.cpp` | Tests cover sign handling, gain/offset round-trip and out-of-range rejection |
| T15 | BlockPlanner: group tags by area, merge across small gaps (configurable threshold), split at 125 registers, map each tag to its block index and offset | S | T02 | `src/core/BlockPlanner.cpp`, `tests/tst_blockplanner.cpp` | Tests cover empty input, duplicates, gaps, the 125 split, mixed areas and unsorted input |
| T16 | Recorder: ring buffer per tag with configurable capacity; append a batch; `valueAt(tag, t)` returns the last sample at or before t (binary search); range iterator | H | T02 | `src/core/Recorder.cpp`, `tests/tst_recorder.cpp` | Tests cover wraparound, lookup before the first sample, exact hits and range bounds |
| T17 | ProjectIO: save/load `.mbproj` (format version, connection settings, tag list) | H | T02 | `src/core/ProjectIO.cpp`, `tests/tst_projectio.cpp` | Tests cover round-trip, unknown version and missing fields falling back to defaults |
| T18 | CsvExporter: wide format, ISO-8601 plus elapsed-seconds columns, full or [t1, t2] scope, `,` or `;` delimiter, quoting tag names | H | T16 | `src/core/CsvExporter.cpp`, `tests/tst_csvexporter.cpp` | Golden-file tests pass |

Wave 1 ordering: T10, T11, T14, T15, T16 and T17 run in parallel with T12. T18 starts after T16 merges; T13 after T12.

### Wave 2 — Modbus core (Sonnet)

| ID | Task | Model | Deps | Files | Acceptance |
|---|---|---|---|---|---|
| T20 | ModbusConnection: RAII around `modbus_t*`; connect, disconnect, set unit ID and timeout; `readHolding`/`readInput`/`writeSingle`/`writeMultiple`; libmodbus errno mapped to an error enum plus message | S | T02 | `src/core/ModbusConnection.cpp` | Covered by T22 |
| T21 | PollWorker: lives on a QThread and owns the ModbusConnection; timer-driven cycle over the planned blocks; emits `SampleBatch`; write queue served between cycles; per-block quality; reconnect with exponential backoff; signals for status and errors | S | T15, T20 | `src/core/PollWorker.cpp` | Covered by T22; no data races under a debug build |
| T22 | Integration tests: start the server with `QProcess` on a free port, then cover counter read, HR write/read-back, illegal address, timeout via `--delay`, reconnect after killing and restarting the server, and a write queued during polling | S | T12, T13, T21 | `tests/tst_integration.cpp` | All scenarios pass 10 runs in a row (flakiness check) |

### Wave 3 — UI (parallel where file sets allow)

| ID | Task | Model | Deps | Files | Acceptance |
|---|---|---|---|---|---|
| T30 | MainWindow shell: menus (File, View, Export, Help), toolbar, dock layout (tag table, cursor panel), status bar with connection LED; stub actions | H | T01 | `src/app/MainWindow.*` | Builds; layout matches the PLAN.md description |
| T31 | ConnectionBar widget: IP, port, unit ID, timeout and period fields with validators; Connect button; settings kept in QSettings | H | T30 | `src/app/ConnectionBar.*` | Builds; invalid IP is rejected; settings survive a restart |
| T32 | TagTableModel and view: QAbstractTableModel over the tags; delegates for area/type combos, color picker and plot checkbox; add/remove/duplicate rows; live value and quality columns updated from batches without resetting the model | S | T02, T14 | `src/app/TagTable*` | Edits stay stable while polling at 100 ms; no selection loss |
| T33 | Write flow: editing the value column → confirmation dialog → encode with TagCodec → queue to PollWorker → success/error feedback in the row and status bar; FC16 for a contiguous multi-selection | S | T21, T32 | `src/app/WriteController.*` | Manual test against the server: writing HR 100 moves IR 10 |
| T34 | Wiring: connect ConnectionBar, PollWorker, Recorder and TagTable; File New/Open/Save/Save As with a dirty flag; re-plan blocks when the tags change | S | T21, T31, T32, T17 | `src/app/AppController.*`, `MainWindow.cpp` | Open project → connect → values update → save → reopen gives the same result |

### Wave 4 — Trend, cursors, exports

| ID | Task | Model | Deps | Files | Acceptance |
|---|---|---|---|---|---|
| T40 | TrendPlot: one QwtPlotCurve per plotted tag (color from the tag); time axis in seconds; auto-scroll window selector (60 s / 5 min / all); Pause/Run; wheel zoom and drag pan; incremental append with min/max decimation | S | T16, T34 | `src/app/TrendPlot.*` | 20 tags at 100 ms for 1 h stays smooth (repaint < 30 ms) |
| T41 | Cursor interaction: two draggable vertical QwtPlotMarkers (C1/C2) with labels; placing or dragging them pauses auto-scroll; a "Clear cursors" action; emits cursor-moved signals | S | T40 | `src/app/PlotCursors.*` | Dragging works while zoomed and paused; cursors stay correct after zoom/pan |
| T42 | CursorPanel: shows t1, t2, Δt and 1/Δt, then a table per series with Y@C1, Y@C2 and ΔY using `Recorder::valueAt` | H | T16, T41 (signal signature only) | `src/app/CursorPanel.*` | Values match the CSV export over the same range |
| T43 | Image export dialog: format (PNG/SVG/PDF), width, height, DPI, option to include cursors and a readout box; rendered with QwtPlotRenderer | H | T41, T42 | `src/app/ExportImageDialog.*` | Output file opens and the cursors are visible when the option is on |
| T44 | CSV export dialog: scope (all or C1..C2, disabled without cursors), delimiter, file picker; calls CsvExporter | H | T18, T41 | `src/app/ExportCsvDialog.*` | Output opens in Excel with both delimiter settings |

### Wave 5 — Packaging & finish

| ID | Task | Model | Deps | Files | Acceptance |
|---|---|---|---|---|---|
| T50 | Deploy step: CMake `install()` rules plus a windeployqt post-install step, including the libmodbus and Qwt DLLs | H | T01 | `CMakeLists.txt` (install section only) | `cmake --install` produces a folder that runs on a clean machine or VM |
| T51 | Inno Setup script: installs the app and server, a Start-menu shortcut, the `.mbproj` file association and an uninstaller | H | T50 | `installer/modbus_control.iss` | `iscc` builds the installer; install and uninstall run cleanly |
| T52 | README and user guide: build steps, running the server, a sample project file, screenshots placeholder | H | T44 | `README.md`, `docs/`, `examples/demo.mbproj` | Steps reproduce a working build from a fresh clone |
| T53 | Code review of the whole repository (`/code-review high`) and fixes for the findings | O | all | — | No high-severity findings remain |
| T54 | End-to-end acceptance: run the server and app; connect; plot the sine and the lag; write HR 100; place cursors; export PNG and CSV; save and reopen the project | O | all | — | Every PLAN.md feature is checked off |

## 4. Summary

| Model | Tasks | Count |
|---|---|---|
| Haiku | T10, T11, T14, T16, T17, T18, T30, T31, T42, T43, T44, T50, T51, T52 | 14 |
| Sonnet | T01, T12, T13, T15, T20, T21, T22, T32, T33, T34, T40, T41 | 12 |
| Orchestrator | T02, T53, T54, plus every wave gate | 3 + gates |

The critical path is T01 → T02 → T15/T20 → T21 → T34 → T40 → T41 → T43/T44 → T51/T54, which
puts most of the risk in Sonnet's tasks.
