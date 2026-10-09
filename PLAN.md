# Modbus Control — Project Plan

Windows desktop client for reading and writing Modbus TCP data. It shows a live trend graph with
cursors, can export the graph as an image or the data as CSV, and ships with a test Modbus server.

> Working name: "Modbus Control" (placeholder name). Plot library: Qwt (confirmed for now).

## Decisions

| Topic | Choice |
|---|---|
| Language / UI | C++20, Qt 6.10.3 Widgets (prebuilt `msvc2022_64`, installed with aqtinstall to `C:\Qt`) |
| Build | CMake 3.31 + Ninja + MSVC 2022 (VS 2022 Build Tools 17.14); vcpkg manifest mode for libmodbus only |
| Plot | Qwt 6.3, built from source by our own CMake (FetchContent, hash-checked) |
| Modbus library | libmodbus (GitHub source, built through vcpkg), linked dynamically (LGPL 2.1) |
| Transport | Modbus TCP only |
| Devices | One server at a time (IP, port, unit ID) |
| Data | Holding registers (FC03 read, FC06/FC16 write) and input registers (FC04), int16/uint16, plus read-only int32/uint32/float32 over 2 registers with per-tag byte order (ABCD, CDAB, BADC, DCBA), all with gain/offset scaling |
| Graph | Live time trend, polling every 100 ms or slower, up to about 1 h of history in memory |
| Cursors | Two vertical cursors with t1, t2, Δt, 1/Δt and, per series, Y@C1, Y@C2, ΔY |
| Exports | PNG (plus SVG/PDF) of the plot; CSV of recorded data |
| Configuration | Tag list saved as a JSON project file (`.mbproj`) |
| Writes | Edit a value in the tag table, confirm, write |
| Test server | Console app on libmodbus with simulated signals |
| Packaging | Inno Setup installer and a windeployqt folder |
| UI language | English |

## Architecture

```
modbus-control/
├─ CMakeLists.txt, CMakePresets.json, vcpkg.json   (libmodbus; Qt prebuilt; Qwt via cmake/Qwt.cmake)
├─ src/core/        UI-independent, unit-testable (static lib "mbcore")
│   ├─ ModbusConnection   RAII wrapper around modbus_t*
│   ├─ Tag / decoding     name, area (HR/IR), address, int16|uint16, gain/offset, unit, color, writable
│   ├─ BlockPlanner       groups tags into contiguous reads of ≤125 registers per area
│   ├─ PollWorker         QObject on its own QThread; the only code that touches modbus_t*
│   ├─ Recorder           one ring buffer per tag, shared timestamp per poll cycle
│   ├─ CsvExporter, ProjectIO (JSON)
├─ src/app/         MainWindow, ConnectionBar, TagTable, TrendPlot, CursorPanel, export dialogs
├─ server/          modbus_test_server (console)
├─ tests/           QtTest unit + integration tests
└─ installer/       Inno Setup script + windeployqt step
```

### Threading and protocol
- A libmodbus context is not thread-safe, so `PollWorker` owns it exclusively. The UI sends write
  requests as queued signals, and the worker runs them between poll cycles.
- Each cycle reads all planned blocks and emits one `SampleBatch` (timestamp plus values) to `Recorder`.
- Timestamps come from a steady `QElapsedTimer`, anchored to a wall-clock time taken when the session starts.
- Errors (timeout, Modbus exception) are reported per block and set each tag's quality. A broken
  connection triggers automatic reconnection with backoff. Timeouts use
  `modbus_set_response_timeout`; messages come from `modbus_strerror`.

### Plot library
Qwt is LGPL with exceptions, is packaged in vcpkg and handles real-time curves well. Qt Charts,
Qt Graphs and QCustomPlot are GPL. The parts of Qwt used here:
- `QwtPlotMarker` for the cursors
- `QwtPlotZoomer` / `QwtPlotPanner` for zoom and pan
- `QwtPlotRenderer` for PNG/SVG/PDF export

## Client features

| Area | Details |
|---|---|
| Connection | IP, port, unit ID, timeout and poll period. Connect/disconnect with a status LED. Settings are kept in QSettings. |
| Tag table | Columns: name, area, address, type, gain, offset, unit, raw value, scaled value, quality, plot checkbox, color. Editable; saved and loaded as `.mbproj`. |
| Write | Editing a writable HR tag opens a confirmation, then writes with FC06. Selecting several contiguous tags writes with FC16. Success or error is shown. |
| Trend | Auto-scrolling window (60 s / 5 min / all). Pause/Run freezes the view while recording continues. Wheel zoom, drag pan, decimation. |
| Cursors | Draggable C1/C2; placing them pauses auto-scroll. Values use the last sample at or before the cursor time. |
| Export image | PNG at a chosen size and DPI, optionally including the cursors and a readout box. SVG and PDF are also available. |
| Export CSV | Wide format: ISO-8601 timestamp, elapsed seconds, one column per tag. Scope: full recording or C1..C2. Delimiter `,` or `;`; decimal separator `.`. |

## Test server (`modbus_test_server.exe`)
- `modbus_new_tcp` + `modbus_tcp_listen`. A single thread runs a `select()` loop over several
  clients, calling `modbus_receive`/`modbus_reply`. The `select()` timeout doubles as the
  simulation tick, so no locks are needed.
- Register map in `modbus_mapping_new_start_address`. These defaults can be overridden by `server.json`:
  - IR 0–5: sine (0.2 Hz), ramp, counter, square, noise, sawtooth
  - IR 10: first-order lag that follows HR 100, so writing a setpoint shows a step response
  - HR 0–99: plain read/write memory
  - Addresses outside the map return `ILLEGAL DATA ADDRESS`
- Command line: `--port 1502 --unit 1 --config server.json -v` (`-v` logs each request).
- Fault injection: `--delay <ms>` for timeouts, `--drop <percent>` to test reconnection.

## Tests
- **Unit tests:** BlockPlanner, decoding and scaling, ring buffer and cursor lookup, CSV writer,
  project JSON round-trip, signal generators.
- **Integration tests:** QtTest starts the server with `QProcess` on a free port and checks:
  - the counter keeps increasing
  - a write to HR 100 reads back correctly
  - reading an illegal address returns an exception
  - `--delay` produces a timeout
  - killing the server leads to reconnection

## Milestones
1. Scaffold: vcpkg manifest, presets, empty window; check that libmodbus and Qwt link as DLLs
2. Test server
3. Core layer and integration tests
4. UI: connection bar, tag table, project save/load, writes
5. UI: live trend plot
6. Cursors and readout panel
7. Exports (PNG / CSV)
8. Polish, windeployqt, Inno Setup installer

## Risks and notes
- Dynamic linking of libmodbus, Qt and Qwt keeps the app's licence open.
- Windows Firewall prompts the first time the server listens. Port 1502 avoids clashing with real devices on 502.
- Later additions (32-bit/float types, byte order, RTU) mainly affect `Tag` decoding and
  `ModbusConnection`; the architecture stays the same.

## Analysis mode (added on request, v1.1)

Purpose: school lab step tests. Students find the 0 % and 63.2 % points of a step response so
they can use the Ziegler–Nichols open-loop method. They place two cursors around the response
and ask for any percentages between them.

| Topic | Choice |
|---|---|
| Location | Main window becomes a tab widget: **Trend** and **Analysis**. The Analysis tab has its own plot (with its own C1/C2 cursors and a "Copy cursors from Trend" button) showing only the tags ticked **Analyze**, plus a side panel with settings and results. |
| Tag selection | New tag-table checkbox column **Analyze** (`Tag::analyzed`, JSON `"analyzed"`, default false). |
| 0 % / 100 % references | Per the analysis settings, either the value at C1 / C2 or the mean of the samples in the W seconds ending at each cursor (`[c1-W, c1]`, `[c2-W, c2]`). W is set in the view. |
| Setpoints | Editable list of percentages (default 0 and 63.2). Target = y0 + p/100·(y100 − y0). Rising and falling steps both work. |
| Crossing time | Optional centred moving-average smoothing of width S seconds (0 = off) before searching. Anchor = first time the (smoothed) signal reaches 50 %. For p < 50 the search goes **backwards** from the anchor to the last sample still before the target; for p ≥ 50 it goes **forwards** to the first sample at or past it. The time is interpolated linearly between the two samples. This makes 0 % = "the moment the response leaves the baseline" even when the baseline is noisy. |
| Shown per setpoint | Tag, %, target value, measured (raw recorded) value at the crossing time, time (s), time since C1 (s). Markers (tag colour, label "63.2 %") are drawn on the Analysis plot at (t, target). |
| Model | First-order plus dead time from two chosen setpoints a, b (default 0 % and 63.2 %): T = (t_b − t_a) / (ln(1−a) − ln(1−b)), L = t_a − T·(−ln(1−a)) − t_step. ΔPV = y100 − y0. ΔCO is either the change of a chosen controller-output tag between the cursors (then t_step = that tag's 50 % crossing) or a value typed in (then t_step = C1). K = ΔPV / ΔCO. No tuning table (students compute Ziegler–Nichols themselves). |
| Updates | Live: recomputed when cursors, settings, tags or data in the cursor range change. |
| Persistence | Analysis settings saved in `.mbproj` (top-level `"analysis"` object, optional). Cursor positions are not saved. |
| Export | "Export results (CSV)…" in the Analysis tab and the Export menu. The graph image export uses the visible tab's plot; on the Analysis tab the setpoint markers are included and the readout is the results summary. |
| Test server | The `lag` signal gets an optional dead time (`deadTimeS`, default 1 s on IR 10), so a step on HR 100 gives a textbook first-order-plus-dead-time response. |
