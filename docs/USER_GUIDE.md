# Modbus Control user guide

This guide describes the application as built from this repository. For building, see the
[README](../README.md). For the test server, see [TEST_SERVER.md](TEST_SERVER.md).

## Window layout

- **Connection bar** (top): host, port, unit, timeout and period fields, the **FC06 only**
  checkbox, a status LED, and the Connect / Disconnect button.
- **Trend** (centre): the live graph with its controls above it.
- **Tags** (bottom dock): the tag table with Add, Duplicate, Remove and Write selected buttons.
- **Cursors** (right dock): cursor times and the readout table.
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

## Projects

Projects are saved as `.mbproj` files. A project holds the connection settings (including FC06 only), the register numbering
(1-based or not) and the tag list, including tag colours. Recorded data, cursors and the trend window are not saved.

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
