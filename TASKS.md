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
  The orchestrator creates one git worktree per task
  (`..\modbus-control-wt\tNN`, branch `task/tNN`) and launches a background `Agent` confined to
  it. The agent commits on its branch; the orchestrator merges branches one by one into `main`
  and rebuilds after each merge. (The built-in `isolation: "worktree"` option could not be used
  because the harness process does not see the newly installed git.)
- **No CMake conflicts:** T02 pre-created every planned `.cpp` and test file as a stub and
  registered them in CMake, so tasks normally edit only their own source and test files.
- **Server code:** the server logic is a static library `mbsim` (`server/`) linked by both
  `modbus_test_server` and the server unit tests. "Ramp" from PLAN.md is implemented as
  `SignalKind::Triangle`.
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

### Wave 6 — 32-bit encodings (added on request)

| ID | Task | Model | Deps | Files | Acceptance |
|---|---|---|---|---|---|
| — | Contracts: `ValueType` Int32/UInt32/Float32, `ByteOrder`, `registerCount`, `Tag::byteOrder`; `combineRegisters`/`decodeValue(quint32)`; `TagSample.raw` 32-bit; server `RegisterFormat`/`WordOrder`/`encodeRegisters` | O | — | core + server headers, TagCodec.cpp | Builds |
| T60 | Codec tests for all byte orders; project file `type`/`byteOrder` keys | H | contracts | tests, ProjectIO.cpp | Tests pass |
| T61 | Planner never splits 2-register tags; poller decodes them | S | contracts | BlockPlanner.cpp, PollWorker.cpp, tests | Tests pass incl. integration |
| T62 | Test server publishes int32/float32 in any byte order; defaults IR20–25 | H | contracts | server/*.cpp, tests, docs | Tests pass |
| T63 | Tag table Byte order column, new types, demo tags, docs | H | contracts | TagTableModel/Widget, AppController, docs | Tests pass |
| T64 | Integration test: float32 ABCD/CDAB and int32 read from the server match the expected signal | S | T61, T62 | tests/tst_integration.cpp | Passes 10x |

### Wave 7 — CM50 support and release (added on request)

Target device: ABB ControlMaster CM50 over Modbus TCP (Ethernet option; port 502; max 3
connections). From ABB IM/CM/C-EN Rev. I: the manual's tables use 1-based register numbers
(register N = protocol address N-1); floats are IEEE-754 in high/low word order (ABCD), or CDAB
with the controller's "Reverse IEEE Data" setting; holding registers are documented for FC03/FC06
only. Read-only 32-bit integers: IR 4001-4007 (totalizers). Floats: IR 5001-5107 (PV, outputs,
setpoints); writable floats HR 6591-7149 (alarm trips, setpoints, PID, constants); writable
32-bit HR 6001-6011 (totalizer presets); 16-bit HR 2001-2008, 851-854. Coils/discrete inputs
(FC01/02/05) and RTU were explicitly left out of scope by the user.

| ID | Task | Model | Deps | Files | Acceptance |
|---|---|---|---|---|---|
| — | Contracts: 32-bit tags writable (`Tag::isWritable`), `encodeRegisters`, `ConnectionSettings::singleRegisterWrites`, PollWorker/WriteController docs | O | — | headers | Builds |
| T70 | `encodeRegisters` + tests; project key `singleRegisterWrites` | H | contracts | TagCodec.cpp, ProjectIO.cpp, tests | Tests pass |
| T71 | Per-register FC06 write mode in PollWorker; WriteController 32-bit + mixed selection; integration tests for 32-bit reads/writes (covers T64) | S | contracts | PollWorker.cpp, WriteController.cpp, tst_integration | 5x stable |
| T72 | Final UI: see "Remaining work" below | H/S | T63, contracts for 1-based numbering | app + docs | Tests pass |
| T80 | Release v1.0.0 on GitHub with the installer | O | everything | CMakeLists version, gh release | User confirmed, release visible |

### Wave 8 — Analysis mode (v1.1, added on request; design in PLAN.md "Analysis mode")

| ID | Task | Model | Deps | Files | Acceptance |
|---|---|---|---|---|---|
| T90 | Contracts + stubs: `Tag::analyzed`; `StepAnalysis.h` (AnalysisSettings, SetpointResult, StepResult, referenceLevel/smooth/findCrossing/analyzeStep); `Project::analysis`; `AnalysisCsv.h`; `AnalysisView.h`; MainWindow tabs + actions; TagTableModel `Analyze` column; `SignalParams::deadTimeS`; CMake + empty test files | O | — | headers, stub .cpp, CMake | Builds, existing tests pass |
| T91 | Step-analysis numerics: references (cursor / W-second mean), centred moving average, 50 %-anchored crossing search with interpolation, FOPDT K/L/T, falling steps, NaN gaps; `tst_stepanalysis` with synthetic noisy FOPDT data | S | T90 | StepAnalysis.cpp, tests/tst_stepanalysis.cpp | L, T within 2 % on clean data, 5 % with noise; edge cases return `found=false` / errors |
| T92 | `Analyze` checkbox column in TagTableModel; ProjectIO `"analyzed"` per tag and the `"analysis"` object (ranges validated) + tests | H | T90 | TagTableModel.cpp, ProjectIO.cpp, tst_tagtablemodel, tst_projectio | Tests pass |
| T93 | Results CSV writer `exportAnalysisCsv` + tests | H | T90 | AnalysisCsv.cpp, tests/tst_analysiscsv.cpp | Tests pass |
| T94 | Test server: dead time for the `lag` signal (`deadTimeS`, JSON key, default IR 10 = 1 s), `--list` output, docs + tests | H | T90 | server/SignalGen.cpp, ServerConfig.cpp, main.cpp, tst_signalgen, tst_serverconfig, docs/TEST_SERVER.md | Tests pass |
| T95 | `AnalysisView` widget: plot of analyzed tags with own cursors, "Copy cursors from Trend", settings panel (reference mode, W, S, % list, model a/b, ΔCO tag or value), setpoint table, model table, plot markers, live recompute, `readoutText()`; offscreen tests | S | T91 | AnalysisView.cpp, tests/tst_analysisview.cpp | Tests pass |
| T96 | Wiring: MainWindow Trend/Analysis tabs; AppController feeds the view (recorder, tags, 10 Hz refresh, settings ↔ project, modified flag), image export of the visible tab, "Export results (CSV)…"; integration test: HR 100 step → IR 10 gives L ≈ 1 s, T ≈ 2 s | S | T92–T95 | MainWindow.cpp, AppController.cpp, tst_integration.cpp | All tests pass, integration 3x stable |
| T97 | Docs and examples: USER_GUIDE "Analysis mode" (Ziegler–Nichols walk-through on the test server), README, demo.mbproj with IR 10 analyzed and HR 100 as ΔCO tag | H | T96 | docs, README.md, examples/demo.mbproj | Reviewed |
| T98 | Review, version 1.1.0, installer; GitHub release only after the user confirms | O | all | CMakeLists etc. | User confirmed |

Parallel groups: T91, T92, T93, T94 run together after T90; T95 after T91; T96 after T92–T95; T97 after T96.

### Wave 9 — v1.2 user experience (planned on request; design in PLAN.md "v1.2 user-experience features")

| ID | Task | Model | Deps | Files | Acceptance |
|---|---|---|---|---|---|
| T100 | Contracts + stubs: `Tag::axis`, `Tag::quickA/quickB`; `AnalysisSettings::showModel/guideLines`; `StepResult::fitRms/fitRmsPercent` + `fopdtModelValue()`; `SessionIO.h`; `ClipboardCopy.h`; `QuickWritePanel.h`; TrendPlot right-axis doc; WriteController quick-write API; MainWindow session actions, Quick write dock, Options toggle; TagTableModel Axis / Quick A / Quick B columns; CMake + empty tests | O | — | headers, stubs, CMake | Builds, existing tests pass |
| T101 | Model value + fit RMS numerics; AnalysisCsv fit columns; tests | H | T100 | StepAnalysis.cpp, AnalysisCsv.cpp, their tests | Tests pass |
| T102 | Tag table Axis / Quick A / Quick B columns (+ delegates); ProjectIO keys `axis`, `quickA`, `quickB`, `showModel`, `guideLines`; tests | H | T100 | TagTableModel.cpp, TagTableWidget.cpp, ProjectIO.cpp, tests | Tests pass |
| T103 | SessionIO: save/load project + recording + cursors (compressed float64 columns, NaN kept, 72 000 rows × 20 tags round-trip < 1 s); tests | S | T100 | SessionIO.cpp, tests/tst_sessionio.cpp | Tests pass |
| T104 | ClipboardCopy: table/selection → TSV; Ctrl+C helper; tests | H | T100 | ClipboardCopy.cpp, tests/tst_clipboard.cpp | Tests pass |
| T105 | TrendPlot right Y axis: curves per tag axis, separate auto-scale/Fit Y, unit titles, right axis hidden when unused; tests | S | T100 | TrendPlot.cpp, tests | Tests pass |
| T106 | AnalysisView: model curve, Fit RMS column, guide-line plot item, markers/curves on the tag's axis, Ctrl+C + Copy results; tests | S | T101, T104, T105 | AnalysisView.cpp, tests/tst_analysisview.cpp | Tests pass |
| T107 | QuickWritePanel dock + WriteController quick writes with "Don't ask again this session"; tests | S | T100 | QuickWritePanel.cpp, WriteController.cpp, tests | Tests pass |
| T108 | Wiring: Save/Open session (offline view, Connect → continue/new prompt with time offset + NaN gap row), Quick write dock + Options toggle, modified/unsaved prompts; integration test: record → save session → reopen → same analysis results | S | T101–T107 | MainWindow.cpp, AppController.cpp, tst_integration | All tests pass |
| T109 | Docs + demo: USER_GUIDE (sessions, axis, quick writes, model curve, guides, copy), README, demo.mbproj (Setpoint quick A 0 / B 1000, Setpoint on the right axis) | H | T108 | docs, README, examples | Reviewed |
| T110 | Review, version 1.2.0, installer; GitHub release only after the user confirms | O | all | | User confirmed |

Parallel groups: T101–T105 and T107 after T100; T106 after T101/T104/T105; T108 after the rest; T109 after T108.

## Status and remaining work

_Last updated: 2026-10-09. Update this section whenever a task is merged._

**Merged into `main` and pushed** (all 14 ctest targets pass): T01, T02, T10–T18, T20–T22, T30–T34,
T40–T44, T50–T52, T53a (polish fixes), T60–T63, T70, T71, plus the 32-bit contracts and an MIT
LICENSE. T64 is covered by T71's tests. 32-bit holding tags are already writable in the tag
table (orchestrator fix after merging T63).

**In progress:** Wave 9 (v1.2): T100–T105, T107 merged; T106 (Analysis view) next, then T108 (wiring), T109 (docs). **v1.1.0 released** on 2026-10-09: https://github.com/francisboudreau7/modbus-control/releases/tag/v1.1.0 (installer `ModbusControl-1.1.0-setup.exe`, sha256 6d96c9f1…e14d). **v1.0.0 released** on 2026-10-09: https://github.com/francisboudreau7/modbus-control/releases/tag/v1.0.0 (tag v1.0.0 = 42edcd6, installer `ModbusControl-1.0.0-setup.exe`, sha256 bf4a07b4…14eb). The user confirmed the tag-table editor fix in the real app.

**Remaining, in order**
1. ~~Merge T63~~ (done).
2. ~~Orchestrator contract for **1-based register numbering**~~ (done) (user-requested):
   - `Project::addressBase` (0 or 1, default 0), JSON top-level key `"addressBase"`; tag
     addresses in files stay zero-based protocol addresses.
   - `TagTableModel::setAddressBase(int)` / `addressBase()`: Address column shows and accepts
     address + base (edit range base..65535+base, 32-bit tags up to 65534+base); header
     "Address" (0-based) or "Register" (1-based) with tooltip; emits dataChanged for the column.
   - `MainWindow::Actions::oneBasedAddresses`: checkable action in a new "Options" menu
     ("Use 1-based register numbers").
   - WriteController texts use the model's base ("HR 6639-6640" for protocol 6638).
   - AppController: wire the action, apply/save with the project, mark modified on toggle.
   - PollWorker status messages stay in protocol addresses (documented limitation).
3. ~~**T72 final UI**~~ (done) (Haiku, or Sonnet if it grows): implement item 2; ConnectionBar "FC06 only" checkbox bound to `singleRegisterWrites` (see ConnectionBar.h);
   ProjectIO `addressBase` + tests; tests for all of it; README Quick start still lists the old
   8 demo tags (now 11); USER_GUIDE still says 32-bit tags are read-only — fix both; USER_GUIDE: 32-bit writes, FC06-only
   option, 1-based numbering, and a "Connecting to an ABB CM50" section with a few example tags
   (Loop 1 PV IR 5009 float32 ABCD read-only; Loop 1 Local Setpoint 1 HR 6639 float32 writable;
   Totalizer 1 batch total IR 4001 uint32), noting "Reverse IEEE Data" = CDAB and to enable
   "FC06 only" if FC16 writes are rejected; add `examples/cm50.mbproj`.
4. ~~**Final review (T53)**~~ (done: 7 findings fixed in 4a0fd36): run `/code-review` (or a review agent) over the whole repo; fix
   high-severity findings; rerun `ctest --repeat until-fail:3`.
5. ~~**Acceptance (T54)**~~ (done): user runs the app against the test server (see README Quick start) and
   checks write/cursors/exports; Claude verifies by offscreen tests only (no desktop input).
6. ~~**T80 release v1.0.0**~~ (done): bump `project(ModbusControl VERSION 1.0.0)`, run
   `installer\build_installer.ps1`, commit + push, then — after the user confirms —
   `gh release create v1.0.0 out\installer\ModbusControl-1.0.0-setup.exe --title "Modbus Control 1.0.0"`
   with release notes (features, CM50 notes, test server usage, licences).
