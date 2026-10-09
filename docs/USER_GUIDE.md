# Modbus Control user guide

This guide describes the application as built from this repository. For building, see the
[README](../README.md). For the test server, see [TEST_SERVER.md](TEST_SERVER.md).

## Window layout

- **Connection bar** (top): host, port, unit, timeout and period fields, the **FC06 only**
  checkbox, a status LED, and the Connect / Disconnect button.
- **Trend** and **Analysis** tabs (centre): **Trend** is the live graph with its controls above it.
  **Analysis** measures the step response of the tags marked for analysis (see
  [Analysis](#analysis-step-response)).
- **Tags** (bottom dock): the tag table with Add, Duplicate, Remove and Write selected buttons.
- **Cursors** (right dock): cursor times and the readout table of the Trend.
- **Menus**: File, Export, View, Options and Help.
- **Status bar**: short messages (left) and the connection state (right).

Docks can be closed and reopened from the View menu. Window size and dock layout are remembered.

## Connecting

Fill in the fields, then click **Connect** (or press Enter in the Host field). Click **Disconnect**
to stop. While connecting or connected, the fields are read-only.

| Field | Range | Notes |
|---|---|---|
| Host | IPv4 address, or `localhost` | Host names other than `localhost` are rejected, and Connect stays disabled. Use a lower-case `localhost`. |
| Port | 1..65535 | Default 1502. |
| Unit | 0..247 | Modbus unit id. |
| Timeout | 100..60000 ms | How long to wait for each response. |
| Period | 100..60000 ms | Time from one poll cycle to the next. A cycle that takes longer than the period starts the next one at once. |
| FC06 only | on / off | Off by default. When on, a value that spans several registers (a 32-bit tag, or Write selected) is written as one FC06 request per register instead of one FC16 request. See Writing values. |

The last values you connected with are remembered and used the next time the application starts.

### Status LED

| Colour | State | Meaning |
|---|---|---|
| Grey | Disconnected | Not connected. Fields are editable. |
| Yellow | Connecting | First connection attempt in progress. |
| Green | Connected | Polling the server. |
| Orange | Reconnecting | The connection was lost, or the connection attempt failed. The application retries. |

Hover over the LED to see the reason for the last failure and the time until the next retry.

When the connection is lost or a connection attempt fails, the state becomes **Reconnecting**.
Retries follow a back-off of 0.5 s, 1 s, 2 s, 4 s and 8 s, then every 10 s. Each failed attempt
is reported in the status bar. Press **Disconnect** to stop retrying. Tags show quality
*Disconnected* while the connection is down, and the trend shows a gap.

## Tags

Each row is one register value that the application reads. Columns:

| Column | Meaning |
|---|---|
| Name | Name shown in the table, trend legend and cursor panel. Cannot be empty. |
| Area | `Holding` (FC03 read, FC06/FC16 write) or `Input` (FC04 read). |
| Address | Zero-based Modbus address, 0..65535 (HR 100 is address 100). With Options > Use 1-based register numbers the column is headed **Register** and shows and accepts the number as printed in device manuals (1..65536; 1..65535 for 32-bit tags). See 1-based register numbers. |
| Type | `int16` (signed) or `uint16` (unsigned), one register. `int32` (signed), `uint32` (unsigned) and `float32` (IEEE-754 single precision), two consecutive registers. See 32-bit values. |
| Byte order | `ABCD`, `CDAB`, `BADC` or `DCBA`: how the two registers of a 32-bit value are arranged. Shows `—` for 16-bit types, where it has no effect. See 32-bit values. |
| Gain | Multiplier applied to the raw value. Cannot be 0. |
| Offset | Added after the gain. |
| Unit | Shown after the name in the legend and the CSV header. Optional. |
| Writable | Allows writes from the Value column. Holding registers only, of any type. The box is disabled for input registers, and changing Area to Input clears it. |
| Plot | Shows the tag in the trend. Unticked tags are still polled and recorded. |
| Analyze | Shows the tag on the Analysis tab and includes it in the step-response results. See [Analysis](#analysis-step-response). |
| Color | Trend colour. Double-click the cell, or press F2 or Enter, to open the colour picker. |
| Raw | The register value as read. 16-bit types show hex and decimal (`0x00AB (171)`); 32-bit types show the 32-bit value in hex (`0x42F6E979`). Read-only. |
| Value | Scaled value: `raw * gain + offset`, with 6 significant digits (7 for `float32`). Read-only, except for writable tags (see Writing values). |
| Quality | State of the last read. Read-only. |

Quality values:

| Quality | Meaning |
|---|---|
| Unknown | Not read yet, or the connection was closed. |
| Good | The last read succeeded. |
| Timeout | No response within the Timeout setting. |
| Exception | The server returned a Modbus exception, for example illegal data address. |
| Disconnected | No connection to the server. |
| Error | Any other failure. |

Hover over a Quality cell to see the last failed write for that tag. The tooltip is cleared by the
next successful write. Read errors are reported in the status bar only.

### 32-bit values

`int32`, `uint32` and `float32` tags read two consecutive registers, starting at **Address**. The
second register is Address + 1, so a 32-bit tag at address 65535 would need a register past the end
of the address space and is rejected. Use 65534 or lower.

The two registers are combined according to **Byte order**. A, B, C and D are the four bytes of the
value, from most to least significant. The table shows what is in each register for the value
`0x42F6E979` (`float32` 123.456):

| Byte order | Register at Address (n) | Register at Address + 1 | Typical use |
|---|---|---|---|
| `ABCD` | `0x42F6` (A B) | `0xE979` (C D) | Big endian. The Modbus default. |
| `CDAB` | `0xE979` (C D) | `0x42F6` (A B) | Word swap. Common in PLCs and meters. |
| `BADC` | `0xF642` (B A) | `0x79E9` (D C) | Byte swap within each register. |
| `DCBA` | `0x79E9` (D C) | `0xF642` (B A) | Little endian. |

If a 32-bit value looks wrong (a float that is far too large or small, or an integer that jumps
around), the byte order is usually wrong. Try `CDAB` first, then the others.

The Raw column shows the 32-bit value after the byte order has been applied, as ABCD. If the byte
order matches the server, Raw shows the value the server meant, and Value is the scaled result.

32-bit holding tags can be written like 16-bit ones (see Writing values). Their Raw and Value cells
are updated by polling like any other tag.

### 1-based register numbers

Modbus itself addresses registers from 0, but many device manuals number them from 1 (the first
holding register is "40001" or "register 1"). Tick **Options > Use 1-based register numbers** to
type and read numbers as the manual prints them. The Address column header changes to **Register**,
and its cells, the write confirmations ("Write ... to HR 6639-6640") and the Write selected dialog
all use the 1-based numbers. Nothing else changes: tags are stored with zero-based protocol
addresses, so register 5009 is stored and sent as address 5008.

The setting is saved in the project file (`addressBase`) and restored when the project is opened.
Toggling it marks the project as modified. File > New returns to 0-based numbering. Status
messages about failed reads still show protocol (zero-based) addresses.

### Editing the table

- Edit a cell by double-clicking it or pressing F2.
- **Add** creates a tag after the last row, with the same area as that row and the address one
  higher. Its name is `Tag <id>`.
- **Duplicate** copies every selected row. The copy's name gets " (copy)".
- **Remove**, or the Delete key, removes the selected rows. You are asked to confirm when more
  than one row is selected.
- **Write selected...** is enabled when two or more rows are selected and all of them are
  writable holding registers. See Writing values.

Tag edits mark the project as modified. Live values do not.

## Writing values

Writes go to the server between poll cycles. Only holding-register tags with **Writable** ticked
can be written, of any type (16-bit or 32-bit).

### One register

1. Connect first. Without a connection the status bar says "Not connected" and nothing is sent.
2. Double-click the **Value** cell of a writable tag and type a value in scaled units (not the raw
   number).
3. Confirm the question, for example: "Write 12.5 (raw 125) to HR 100 'Setpoint'?" The unit is
   shown too, when the tag has one.

The value is converted to a raw integer with the inverse of gain and offset, and rounded to the
nearest integer. If the raw value is outside the type's range (-32768..32767 for `int16`,
0..65535 for `uint16`; the finite `float32` range, and the `int32` and `uint32` ranges, for 32-bit
types), a warning is shown and nothing is sent. A 16-bit write uses FC06.

For a 32-bit tag the value is encoded into two registers using the tag's byte order, and the
confirmation names the registers, type and byte order, for example "Write 50 (raw 0x42480000) to
HR 6639-6640 'Local Setpoint 1' [float32, ABCD]?". By default both registers are sent in one FC16
(Write Multiple Registers) request, so the controller never sees half a value. If the **FC06 only**
box in the connection bar is ticked, the app sends one FC06 (Write Single Register) request per
register instead, in ascending address order, and stops at the first failure. In that case a failed
write can leave the first register changed and the second unchanged. Use the option only for devices
that do not accept FC16.

The status bar shows "Writing ...", then "Write OK: <name>" or "Write failed: <reason>". A failure
also sets the reason as the tooltip of the tag's Quality cell. The Value column keeps showing the
polled value, so it changes only when the next read returns the new value.

### Several registers

1. Select two or more rows. Use Ctrl or Shift to select rows.
2. Click **Write selected...**. The rows must all be writable holding registers with consecutive
   addresses and no duplicates. At most 123 registers can be written at once.
3. Edit the value for each register. Each field starts with the current value. Use `.` as the
   decimal separator.
4. Confirm "Write N registers HR a..b?". The whole group is sent in one FC16 request, or as one
   FC06 request per register (ascending, stopping at the first failure) when **FC06 only** is ticked.
   16-bit and 32-bit tags can be mixed; the 123-register limit counts registers, not tags.

## Trend

The trend shows each plotted tag as one curve, in the tag's colour. The X axis is time in seconds,
since the first connection in this run (see Recording). The Y axis is the scaled value.

Controls above the plot:

- **Window**: 60 s (default), 5 min, 30 min or All. In a time window, the view follows the newest
  data. All shows everything in memory.
- **Pause** / **Run**: Pause freezes the view while recording continues. Run returns to the newest
  data and turns auto-scaling back on.
- **Place cursors**, **Clear cursors**: see Cursors.
- **Fit Y**: sets the Y range to the visible data, and keeps fitting while the view follows the data.
- **Clear data**: discards all recorded data (after a confirmation when there is any), clears the
  cursors, and restarts the time axis at 0. The connection stays as it is.

Mouse:

| Action | Effect |
|---|---|
| Mouse wheel | Zooms the time (X) axis. |
| Shift + mouse wheel | Zooms the value (Y) axis. Auto-scaling stops until Run or Fit Y. |
| Right-button drag | Pans. |

Zooming and panning pause the trend.

Click a tag name in the legend below the plot to hide or show its curve. The legend shows names as
`name [unit]`.

When data is missing (disconnected, a failed read, or bad quality) the line breaks.

## Cursors

Two vertical cursors, **C1** (red) and **C2** (blue), mark times on the trend.

- **Place cursors** (toggle): click once to put C1 at that time, click again to put C2. Placing
  stops after C2. Press Esc to stop placing without placing C2.
- **Move a cursor**: press the left button within 5 pixels of a cursor line and drag. This works
  while zoomed or paused.
- **Clear cursors**: removes both cursors.

Placing or moving a cursor pauses the trend, so the cursor stays where you put it.

### Cursors panel

- Header: `t1` and `t2` in seconds (3 decimals), `Δt = t2 - t1`, and `1/Δt` in Hz. Δt is negative
  when C2 is before C1, and 1/Δt is shown as a positive frequency. A dash (—) means a cursor is
  missing, or Δt is 0 for 1/Δt.
- Table: one row per plotted tag, with `Y@C1`, `Y@C2` and `ΔY = Y@C2 - Y@C1`. Each Y value is the
  last recorded sample at or before the cursor time. A dash means there is no such sample, or the
  sample was missing.

## Export

### Graph image

Export > Graph image... (Ctrl+E).

- **File**: type a name ending in `.png`, `.svg` or `.pdf`. Browse... adds the extension for you.
  Other extensions are rejected.
- **Width** and **Height**: 16..20000 px. The default is the current size of the plot.
- **Resolution**: 36..1200 dpi, default 96. It sets the PNG metadata and the SVG resolution, and
  the PDF page size is the pixel size divided by the resolution.
- **Include cursors**: available when a cursor is set. Unticked hides the cursor lines.
- **Include cursor readout**: available when a cursor is set. Draws the Cursors panel text in a
  box at the top left of the image.

The image shows the plot as it is on screen, including the current zoom and pan.

### Data (CSV)

Export > Data (CSV)... (Ctrl+Shift+E).

- **Scope**: All recorded data (default), or Between cursors. Between cursors is available when
  both cursors are set. It includes rows whose time is between the two cursor times, inclusive.
- **Delimiter**: comma or semicolon. The default is semicolon when the Windows decimal separator is
  a comma, and comma otherwise. Your choice is remembered.
- **File**: default `trend.csv` in your Documents folder. `.csv` is added when missing.

The file has one column per tag that has recorded data, including tags with Plot unticked. The
header is `timestamp,elapsed_s,<name> [<unit>],...` (the unit part is omitted when empty).

- Encoding UTF-8, CRLF line endings.
- `timestamp` is local time with milliseconds and the UTC offset, for example
  `2026-10-09T14:03:12.250-04:00`.
- `elapsed_s` is seconds since the time base started, with 3 decimals.
- Values are the scaled values, with `.` as the decimal point. A missing sample is an empty cell.

## Analysis (step response)

The Analysis tab measures a step response between two cursors. It finds the times at which the
response reaches the setpoints (0 % and 63.2 % by default) and computes a first-order model with
dead time: the gain K, the dead time L and the time constant T. It is meant for step tests, for
example to get the values for Ziegler–Nichols open-loop tuning.

### Preparing the tags

Tick **Analyze** in the tag table for each tag to analyse. Only analysed tags are shown on the
Analysis plot and in the results. Changing the box marks the project as modified.

### Placing the cursors

Each tag is analysed between the two cursors:

- **C1** goes before the step, while the response is still at its starting value.
- **C2** goes after the response has settled.

Place them on the Analysis plot in the same way as on the Trend (see [Cursors](#cursors)), or place
them on the Trend and click **Copy cursors from Trend**. The Analysis tab has its own cursors, and
the Cursors dock shows only the Trend's cursors.

### References

The 0 % level is taken at C1 and the 100 % level at C2. **Reference** selects how:

- **Value at cursor** (default): the recorded value at the cursor.
- **Mean before cursor**: the mean of the recorded values in the **Window** W seconds before the
  cursor (0.1 to 3600 s, default 1 s). Use it when the signal is noisy.

ΔPV is the 100 % level minus the 0 % level. When the two levels are equal, the result says
"No change between C1 and C2".

### Smoothing

**Smoothing** S (Off by default, up to 3600 s) applies a centred moving average of width S to the
samples between the cursors before the setpoint times are searched. The reference levels are not
smoothed.

Smoothing has a bias. A centred average starts to rise up to S/2 before the real response, so the
0 % time comes out early by up to S/2 and L comes out too small. With smoothing on, use a model
**From %** above 0 (for example 10), or keep S small compared with L.

### Setpoints

The **Setpoints** list holds the percentages of the change to find. The default is 0 % and 63.2 %.
Type a value (0 to 100, two decimals) and click **Add**; select an entry and click **Remove**. The
list stays sorted, has no duplicates and holds at most 20 values. For Smith's method, add 28.3 and
use the 28.3 % and 63.2 % times.

### How the times are found

For each setpoint, the recorded samples between the cursors are searched as follows:

1. The 50 % point is found first: the first sample that has reached the 50 % level, counted from the
   starting value. This is the anchor.
2. For a setpoint of 50 % or more, the search runs forwards from the anchor to the first sample that
   has reached the setpoint level.
3. For a setpoint below 50 %, the search runs backwards from the anchor to the last sample that is
   still on the starting side of the setpoint level.
4. The time is interpolated linearly between the two samples on either side of the level.

Missing samples are skipped. When the level is never reached, the time shows "—".

The search starts at the samples that define the 0 % level: the sample at or before C1 (Value at
cursor), or the window of W seconds before C1 (Mean before cursor). So 0 % is always found. If the
signal never returns to its starting value after C1, the 0 % time is the C1 sample itself, which can
be a few milliseconds before C1, so t − C1 can be slightly negative.

### Results tables

**Setpoints** table, one row per analysed tag and setpoint:

| Column | Meaning |
|---|---|
| Tag | Colour swatch and `name [unit]`. |
| % | The setpoint. |
| Target | The level of the setpoint: 0 % level + % × ΔPV. |
| Measured | The recorded (unsmoothed) value at the found time. |
| Time (s) | The found time. |
| t − C1 (s) | The found time minus the time of the earlier cursor. |

**Model** table, one row per analysed tag:

| Column | Meaning |
|---|---|
| Tag | As above. |
| y0, y100 | The 0 % and 100 % levels. |
| ΔPV | y100 − y0. |
| ΔCO | The change of the controller output (see Model). |
| K, L (s), T (s) | The model values. |

A tag without a valid result shows its error text across the value columns. A value that cannot be
computed shows "—". Numbers use 6 significant digits.

Each found setpoint is also marked on the plot with a cross at its time and target level, in the
tag's colour and labelled, for example, "63.2 %".

### Model

The model uses two setpoints, **From %** (a) and **To %** (b). They must be below 100, with a < b.
The defaults are 0 % and 63.2 %. Let ta and tb be their times, and a and b the fractions:

- K = ΔPV / ΔCO
- T = (tb − ta) / (ln(1 − a) − ln(1 − b))
- L = ta − T · (−ln(1 − a)) − t_step

For From 0 % and To 63.2 % this is T = t63 − t0 and L = t0 − t_step.

**ΔCO** is the step of the controller output. Either choose a tag in the list (for example
**Setpoint**), and its change between the cursors is used, or choose **Typed value** and enter the
step in the output's units. With no ΔCO (or 0 typed), K is not computed.

t_step is the 50 % time of the ΔCO tag's step between the cursors. When the ΔCO tag gives no step,
t_step is the time of C1.

The app shows K, L and T only. It does not compute controller settings. For Ziegler–Nichols
open-loop (reaction curve) tuning, apply these rules yourself:

| Controller | Kp | Ti (s) | Td (s) |
|---|---|---|---|
| P | T / (K · L) | — | — |
| PI | 0.9 · T / (K · L) | L / 0.3 (about 3.33 · L) | — |
| PID | 1.2 · T / (K · L) | 2 · L | 0.5 · L |

### Exporting results

- **Export > Analysis results (CSV)...**, or **Export results (CSV)...** on the Analysis tab, is
  enabled when at least one tag has a valid result. The default name is `analysis.csv` in the last
  export folder, or in Documents.
- The file is UTF-8 with CRLF line endings and comma separators. It has two sections, separated by an
  empty line. The first has one row per setpoint (`tag,percent,target,measured,time_s,time_from_c1_s`).
  The second has one row per tag (`tag,c1_s,c2_s,y0,y100,delta_pv,delta_co,step_time_s,gain_k,dead_time_l_s,time_constant_t_s,note`).
  A missing value is an empty cell. For an invalid result, `note` holds the error text.
- **Export > Graph image...** while the Analysis tab is active exports the Analysis plot, including the
  setpoint markers. With **Include cursor readout** ticked, the box shows the results summary.

### Saving

The Analysis settings (reference, window, smoothing, setpoints, model and ΔCO) and the **Analyze**
flags are saved in the project. The cursors are not.

### Try it with the test server

1. Start the test server in a console: `build\msvc-debug\bin\modbus_test_server.exe --port 1502`.
2. Start the application with `examples\demo.mbproj` (or open it with File > Open), then click **Connect**.
3. Double-click the **Value** cell of **Setpoint** (HR 100), type 0 and confirm. Wait a few seconds.
4. Open the **Analysis** tab and place C1 now, before the step.
5. Write 1000 to **Setpoint** the same way and wait about 10 s. The **Lag** signal (IR 10) starts to
   rise after its 1 s dead time, with a time constant of 2 s. When it has settled, place C2.
6. In **Model**, select **Setpoint** as the ΔCO tag.
7. The Lag row shows K ≈ 1, L ≈ 1 s and T ≈ 2 s.

## Projects

Projects are saved as `.mbproj` files. A project holds the connection settings (including FC06 only), the register numbering
(1-based or not), the tag list, including tag colours and the **Analyze** flags, and the Analysis settings. Recorded data, cursors and the trend window are not saved.

In the file, each tag has an `analyzed` key, and the Analysis settings are in an `analysis` object
(`percents`, `reference`, `referenceWindowS`, `smoothingS`, `modelPercents`, `deltaCoTag`, `deltaCo`).
Both are optional: a file without them loads with the defaults.

| Menu item | Shortcut | Action |
|---|---|---|
| File > New | Ctrl+N | Empty tag list. The connection fields are reset to the last connected settings. |
| File > Open... | Ctrl+O | Open a `.mbproj` file. |
| File > Save | Ctrl+S | Save to the current file. Asks for a name when the project has none. |
| File > Save As... | Ctrl+Shift+S | Save under a new name. `.mbproj` is added when missing. |
| File > Quit | Ctrl+Q | Close the application. |

The window title shows `*` when there are unsaved changes. New, Open and Quit ask whether to save
(Save, Discard or Cancel).

Start-up: the application always starts with the built-in demo tags (Sine, Triangle, Counter,
Square, Noise, Sawtooth, Lag, Setpoint, Float sine, Float sine (CDAB) and Int32 saw), and not with a file. It does not reopen the last project.
To start with a project, give its path on the command line:

```bat
build\msvc-debug\bin\modbus_control.exe examples\demo.mbproj
```

[`examples/demo.mbproj`](../examples/demo.mbproj) matches the demo tags and connects to the test
server on 127.0.0.1:1502, unit 1, with a 200 ms period.
[`examples/cm50.mbproj`](../examples/cm50.mbproj) is a starting point for an ABB CM50 (see the next
section).

## Connecting to an ABB ControlMaster CM50

The facts below come from the ABB manual IM/CM/C-EN Rev. I. Check them against your controller's
firmware and configuration.

**Setup on the controller**

- Modbus TCP needs the controller's Ethernet option.
- The port is fixed at 502. Enter 502 in the Port field.
- The controller accepts at most 3 simultaneous connections, so close other Modbus clients if
  Connect fails.
- The controller can restrict clients by IP address (client IP authorization). If it does, add this
  PC's address.
- **Unit** is the controller's Device Address (1..247).

**Register numbers.** The manual's register tables use 1-based register numbers. Tick **Options >
Use 1-based register numbers** and type the numbers as printed (for example 5009 for the Loop 1
process variable). The app sends address 5008.

**Floating-point values** are IEEE-754 single precision, high word first: byte order `ABCD`. If the
controller's "Reverse IEEE Data" setting is Yes, the words are swapped: use `CDAB`.

**Writes.** The manual documents holding registers for FC03 (read) and FC06 (write single) only. If
writing a 32-bit value fails with a Modbus exception, tick **FC06 only** in the connection bar
before connecting; each 32-bit write is then sent as two FC06 requests.

**Example tags** (register numbers as printed, with the 1-based option on):

| Name | Area | Register | Type | Notes |
|---|---|---|---|---|
| Loop 1 Process Variable | Input | 5009 | float32, `ABCD` | Read-only. |
| Loop 1 Control Output | Input | 5013 | float32, `ABCD` | Read-only. |
| Loop 1 Local Setpoint 1 | Holding | 6639 | float32, `ABCD` | Writable. |
| Alarm 1 Trip | Holding | 6591 | float32, `ABCD` | Writable. |
| Totalizer 1 Batch Total | Input | 4001 | uint32, `ABCD` | The manual does not state the signedness; try `int32` if the value looks wrong. |
| Instrument Type | Input | 3 | uint16 | Read-only. |

`examples/cm50.mbproj` contains these tags, with the 1-based option on, host 192.168.1.50, port 502,
unit 1, a 500 ms period and FC06 only off. Open it with File > Open, or give its path on the
command line, then change the host and unit to match your controller. Setpoint and trip values are
written with confirmation, but check them against the controller's own limits first.

**Not supported by this app:** coils and discrete inputs (alarm states, auto/manual mode) and
Modbus RTU (serial). Use the controller's front panel or another tool for those.

## Recording

- Every poll cycle is recorded as one row, covering all tags, not just the plotted ones.
- The history is kept in memory: 72 000 rows, about 2 hours at a 100 ms period. Older rows are
  dropped first.
- The time base starts at the first connection in this run. Disconnecting and reconnecting
  continues the same timeline, and the recorded data is not cleared. Use **Clear data** above the
  trend (or restart the application) to start a new recording.

## Limits

- One server at a time.
- Modbus TCP only. No serial (RTU) connections.
- Holding and input registers only, with `int16` and `uint16` (one register) or `int32`,
  `uint32` and `float32` (two registers) types. There are no coils or discrete inputs.
- Some TCP servers ignore the unit id. It is still sent with each request.
- The history is in memory only, and the project file does not store it.
