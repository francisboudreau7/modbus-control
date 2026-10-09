# Modbus Control user guide

This guide describes the application as built from this repository. For building, see the
[README](../README.md). For the test server, see [TEST_SERVER.md](TEST_SERVER.md).

## Window layout

- **Connection bar** (top): host, port, unit, timeout and period fields, a status LED, and the
  Connect / Disconnect button.
- **Trend** (centre): the live graph with its controls above it.
- **Tags** (bottom dock): the tag table with Add, Duplicate, Remove and Write selected buttons.
- **Cursors** (right dock): cursor times and the readout table.
- **Menus**: File, Export, View and Help.
- **Status bar**: short messages (left) and the connection state (right).

Docks can be closed and reopened from the View menu. Window size and dock layout are remembered.

## Connecting

Fill in the fields, then click **Connect** (or press Enter in the Host field). Click **Disconnect**
to stop. While connecting or connected, the fields are read-only.

| Field | Range | Notes |
|---|---|---|
| Host | IPv4 address, or `localhost` | Host names other than `localhost` are rejected, and Connect stays disabled. Use a lower-case `localhost`. |
| Port | 1..65535 | Default 1502. |
| Unit | 0..255 | Modbus unit id. Units 248..254 are rejected when connecting. |
| Timeout | 100..60000 ms | How long to wait for each response. |
| Period | 100..60000 ms | Time from one poll cycle to the next. A cycle that takes longer than the period starts the next one at once. |

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
| Address | Zero-based Modbus address, 0..65535. HR 100 is address 100. |
| Type | `int16` (signed) or `uint16` (unsigned). |
| Gain | Multiplier applied to the raw value. Cannot be 0. |
| Offset | Added after the gain. |
| Unit | Shown after the name in the legend and the CSV header. Optional. |
| Writable | Allows writes from the Value column. Holding registers only. The box is disabled for input registers, and changing Area to Input clears it. |
| Plot | Shows the tag in the trend. Unticked tags are still polled and recorded. |
| Color | Trend colour. Double-click the cell, or press F2 or Enter, to open the colour picker. |
| Raw | The register value as read, in hex and decimal. Read-only. |
| Value | Scaled value: `raw * gain + offset`, with 6 significant digits. Read-only, except for writable tags (see Writing values). |
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

Writes go to the server between poll cycles. Only the following tags can be written:
holding registers with **Writable** ticked.

### One register

1. Connect first. Without a connection the status bar says "Not connected" and nothing is sent.
2. Double-click the **Value** cell of a writable tag and type a value in scaled units (not the raw
   number).
3. Confirm the question, for example: "Write 12.5 (raw 125) to HR 100 'Setpoint'?" The unit is
   shown too, when the tag has one.

The value is converted to a raw integer with the inverse of gain and offset, and rounded to the
nearest integer. If the raw value is outside the type's range (-32768..32767 for `int16`,
0..65535 for `uint16`), a warning is shown and nothing is sent. The write uses FC06.

The status bar shows "Writing ...", then "Write OK: <name>" or "Write failed: <reason>". A failure
also sets the reason as the tooltip of the tag's Quality cell. The Value column keeps showing the
polled value, so it changes only when the next read returns the new value.

### Several registers

1. Select two or more rows. Use Ctrl or Shift to select rows.
2. Click **Write selected...**. The rows must all be writable holding registers with consecutive
   addresses and no duplicates. At most 123 registers can be written at once.
3. Edit the value for each register. Each field starts with the current value. Use `.` as the
   decimal separator.
4. Confirm "Write N registers HR a..b?". The whole group is sent in one FC16 request.

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

Projects are saved as `.mbproj` files. A project holds the connection settings and the tag list,
including tag colours. Recorded data, cursors and the trend window are not saved.

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
Square, Noise, Sawtooth, Lag and Setpoint), and not with a file. It does not reopen the last project.
To start with a project, give its path on the command line:

```bat
build\msvc-debug\bin\modbus_control.exe examples\demo.mbproj
```

[`examples/demo.mbproj`](../examples/demo.mbproj) matches the demo tags and connects to the test
server on 127.0.0.1:1502, unit 1, with a 200 ms period.

## Recording

- Every poll cycle is recorded as one row, covering all tags, not just the plotted ones.
- The history is kept in memory: 72 000 rows, about 2 hours at a 100 ms period. Older rows are
  dropped first.
- The time base starts at the first connection in this run and is not reset. Disconnecting and
  reconnecting continues the same timeline, and the recorded data is not cleared. Restart the
  application to start a new recording.

## Limits

- One server at a time.
- Modbus TCP only. No serial (RTU) connections.
- 16-bit holding and input registers only, with `int16` or `uint16` types. There are no 32-bit
  or floating-point types, and no coils.
- Some TCP servers ignore the unit id. It is still sent with each request.
- The history is in memory only, and the project file does not store it.
