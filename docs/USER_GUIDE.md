# Modbus Control user guide

This guide describes the application as built from this repository. For building, see the
[README](../README.md). For the test server, see [TEST_SERVER.md](TEST_SERVER.md).

## Window layout

- **Connection bar** (top): host, port, unit, timeout and period fields, a status LED, and the Connect / Disconnect button.
- **Trend** and **Analysis** tabs (centre): **Trend** is the live graph with its controls above it.
  **Analysis** measures the step response of the tags marked for analysis (see
  [Analysis](#analysis-step-response)).
- **Tags** (bottom dock): the tag table with Add, Duplicate, Remove and Write selected buttons.
- **Cursors** (right dock): cursor times and the readout table of the Trend.
- **Quick write** (right dock, tabbed with Cursors): one button per preset value of the writable
  tags (see [Quick writes](#quick-writes)).
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
| Axis | `Left` or `Right`: the Y axis the tag's curve uses on the Trend and the Analysis plot. See [Trend](#trend). |
| Color | Trend colour. Double-click the cell, or press F2 or Enter, to open the colour picker. |
| Raw | The register value as read. 16-bit types show hex and decimal (`0x00AB (171)`); 32-bit types show the 32-bit value in hex (`0x42F6E979`). Read-only. |
| Value | Scaled value: `raw * gain + offset`, with 6 significant digits (7 for `float32`). Read-only, except for writable tags (see Writing values). |
| Min, Max | Write limits, in scaled units: the lowest and highest value that may be written. Empty means no limit. Editable only for writable tags; Min cannot be above Max. Every write, including quick writes, is refused outside the limits. See [Write safety](#write-safety). |
| Quick A, Quick B | Preset values for the Quick write dock, in scaled units. Empty means no button. Only writable tags can have them, and a preset must be within the Min and Max limits. See [Quick writes](#quick-writes). |
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

### Showing and hiding columns

Right-click any column header of the tag table to choose the columns to show: tick or untick a
column in the menu. **Name** is always shown. **Show all columns** shows everything again, and
**Compact view** hides the columns that are only needed while setting tags up: Raw, Gain, Offset,
Byte order, Axis, Color, Min, Max, Quick A and Quick B. Hidden columns keep their values and still
apply (for example, write limits still protect writes when Min and Max are hidden).

The choice is saved in the project. A new project, and a project saved by an older version, opens
in the compact view. The example projects keep the columns they rely on visible: Quick A/B in the
demo, Byte order, Min and Max in the CM50 example.

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
3. Confirm the question. Its headline is "Write to 'Setpoint'?" and it lists the **Current value**
   (or "unknown"), the **New value** in bold and, when the tag has limits, the **Limits**. A small
   note at the bottom gives the register, the type and the raw value, for example "HR 100 · int16 ·
   raw 125". The unit is shown too, when the tag has one.

The value is converted to a raw integer with the inverse of gain and offset, and rounded to the
nearest integer. If the raw value is outside the type's range (-32768..32767 for `int16`,
0..65535 for `uint16`; the finite `float32` range, and the `int32` and `uint32` ranges, for 32-bit
types), a warning is shown and nothing is sent. A write of one 16-bit register uses FC06 (Write Single Register).

For a 32-bit tag the value is encoded into two registers using the tag's byte order, and the
confirmation names the registers, type and byte order in its note, for example "HR 6639-6640 ·
float32 ABCD · raw 0x42480000". Both registers are sent in one FC16
(Write Multiple Registers) request, so the controller never sees half a value. Everything longer
than one 16-bit register (every 32-bit value, and Write selected with several registers) is sent
that way. If a device rejects the request with Modbus exception 1 ("Illegal function"), it does not
accept FC16. The app then shows a warning, "The device rejected the write of ...", saying so and
that nothing was written. Such a device cannot be used for 32-bit values or multi-register writes.

The status bar shows "Writing ...", then "Write OK: <name>" or "Write failed: <reason>". A failure
also sets the reason as the tooltip of the tag's Quality cell. The Value column keeps showing the
polled value, so it changes only when the next read returns the new value.

### Several registers

1. Select two or more rows. Use Ctrl or Shift to select rows.
2. Click **Write selected...**. The rows must all be writable holding registers with consecutive
   addresses and no duplicates. At most 123 registers can be written at once.
3. Edit the value for each register. Each field starts with the current value. Use `.` as the
   decimal separator.
4. Confirm "Write N tags?". A table shows each tag with its current and new value, and a note gives
   the registers (for example "HR 100-103 · FC16"). The whole group is sent in one FC16 request.
   16-bit and 32-bit tags can be mixed; the 123-register limit counts registers, not tags.

### Quick writes

Quick writes are one-click writes of preset values, for setpoints you change often (for example
a "run" and a "stop" value).

1. Set the presets in the tag table: type a value in the **Quick A** and/or **Quick B** cell of a
   writable tag, in scaled units. Clear the cell to remove the preset. Quick A and Quick B are
   read-only for tags that are not writable. A preset outside the tag's **Min**/**Max** limits is
   refused when you type it.
2. Open the **Quick write** dock (right side, tabbed with Cursors; reopen it from the View menu).
   It lists each writable tag that has a preset, with its name, live value and one button per
   preset: **A: <value>** and **B: <value>**. The buttons are disabled while not connected.
3. Click a button. A question asks for confirmation, for example "Write 1000 to HR 100
   'Setpoint'?". The write is then sent like a write of one tag (range check; a 32-bit tag is sent in
   one FC16 request).

For 16-bit tags the confirmation has a check box **Don't ask again this session**. Ticking it and
confirming sends the write, and later quick writes to 16-bit tags go without a question until the
application is restarted. To ask again, tick **Options > Confirm quick writes**, which is ticked when
the application starts. Unticking that option sends 16-bit quick writes without asking.

Quick writes to **32-bit** tags (`int32`, `uint32`, `float32`) always ask for confirmation, whatever
the Options setting; the check box is not shown for them.

Writes from the Value column and **Write selected...** always ask for confirmation. The
**Don't ask again** check box does not affect them.

### Write safety

Before a write is sent, the application runs these checks, in this order. A failed check shows a
warning and nothing is sent:

1. **Connected.** Without a connection nothing is sent.
2. **Limits.** If the tag has a **Min** and/or **Max**, the value must lie within them. The warning
   names the value, the tag and its limits, for example "150 °C is outside the limits of 'Setpoint'."
   followed by "'Setpoint' accepts 0 to 100 °C. Nothing was written."
3. **Valid reading (32-bit tags only).** A 32-bit write needs a current good reading of that tag. If
   the last read was not Good, or there is none yet, the write is refused ("No valid reading of
   'Setpoint' yet."). The warning lists what to check: the connection, the register number, and the
   type and byte order. A wrong setting usually makes reads fail or look wrong.
4. **Range.** The value must fit the tag's type (for example -32768..32767 for `int16`).
5. **Plausibility (float32 only).** If the current value of a `float32` tag is implausible (not a
   finite number, larger than 1e9 in size, or smaller than 1e-6 but not zero), an extra warning
   appears. It explains that the byte order or the register number is probably wrong, and the write
   goes ahead only if you click **Write anyway**. Cancel is the default button. A wrong byte order
   or register makes the reading look absurd, so this warning often catches the mistake before a
   write.

Then the confirmation shows the **Current value** (the last good reading, or "unknown") and the
**New value**. When the tag has limits, it also shows **Limits**, for example "0 to 2000". Read the current value before you confirm: if it is not what you expect, click **No**.

**Checking writes before using a real device.** Start the test server with `--log-writes` (see
the Test server guide): it prints every write it receives, with the registers in hex and the
value decoded in each byte order. Add a writable float32 holding tag at address 110 with the byte
order you plan to use, write a value, and check that the log shows that value under the same byte
order, that refused writes (outside the limits, no reading) produce no line.

Limits are a safety net, not a substitute for checking the controller's own ranges. Set them to the
ranges your controller accepts for that parameter (see the CM50 section below).

## Encoding detection

A 32-bit value can be read in 12 ways: `int32`, `uint32` or `float32`, each with one of four byte
orders. If the type or byte order is wrong, the value is nonsense. A float read as an integer is a
large random number, and a float with its words swapped is usually of the wrong size.

A write has the same problem in reverse. A value written with the wrong encoding reaches the device
as a different number, and the device accepts it. Encoding detection compares the device's readings
with the 12 encodings and works out which one fits. It decides by itself in the clear cases and asks
you when the answer is not clear. It also checks 16-bit tags that may be half of a 32-bit value (see
[A 16-bit tag on half of a 32-bit value](#a-16-bit-tag-on-half-of-a-32-bit-value)).

### Automatic detection

When you add a 32-bit tag, make a tag 32-bit, or change a 32-bit tag's register number, the tag is
**not confirmed** until detection has a result: its Name cell shows a warning icon, and writes to it
ask first (see Writes to an unconfirmed tag). While the application is connected, it waits for
three good readings of the tag and then ranks the 12 encodings. The result is one of these:

- **Clear, and it is the encoding the tag already has.** The readings confirm it: the warning icon
  goes away.
- **Clear, and another encoding fits while the tag's own encoding reads nonsense.** The tag is
  changed to the encoding that fits, for example `float32 CDAB`, and the status bar says so. The
  change is applied but stays **not confirmed**. Check the value against the device before you
  write.
- **Clear, but the tag's own encoding also reads a sensible value**, or **ambiguous.** The Detect
  encoding dialog opens, so you decide (see below). For example, `int32` 17000 is also `float32`
  58.0 in another byte order: only you know which one the device sends.
- **Undecidable.** The app keeps the latest three readings and waits for a result that is clear or
  ambiguous. The tag stays not confirmed meanwhile.

Changing only the type, or only the byte order, of a 32-bit tag does not start detection.
Choosing another byte order yourself confirms the encoding; changing the type keeps the tag's
confirmation as it was.

### The Detect encoding dialog

Right-click the tag in the tag table and choose **Detect encoding...**. The item is available for
every tag. For a 32-bit tag it opens this dialog. For a 16-bit tag it runs the check for half of a
32-bit value (see [A 16-bit tag on half of a 32-bit value](#a-16-bit-tag-on-half-of-a-32-bit-value)),
or, when the device refuses to read the register alone, the check in
[A device that refuses to read half of a 32-bit value](#a-device-that-refuses-to-read-half-of-a-32-bit-value).
The dialog has these parts:

- **The latest registers**, in hex and with their register numbers, for example
  `HR 110-111: 0x3333 0x4251`.
- **Value shown on the device (optional).** Type the number shown on the device's display, for
  example `52.3`. A unit after the number (`52.3 °C`), spaces and a thousands separator
  (`1,013.25`) are fine. The dialog then marks the encodings that give that value. A match allows
  for the last digit you typed, or 0.5 % of the value, whichever is larger. Typing the display
  value is the surest way to decide. If the text is not a number, the dialog says so.
- **The verdict**, one sentence, for example "float32 CDAB fits best." or "Several encodings are
  possible: type the value shown on the device to decide."
- **The table.** One row per group of encodings that read the same values, best first. The columns
  are **Encoding** (the encoding to use; the tooltip lists the others that read the same),
  **Value** (the latest reading), **Plausibility** (Good, Possible, Unlikely or Implausible) and
  **Match** (a check mark or No, once you have typed a display value).

Select a row and click **Apply**. Apply sets the tag's type and byte order and confirms the
encoding. When you typed a display value, only a row that matches it can be applied. If other
32-bit tags are not confirmed yet and use a different byte order, the dialog also offers **Also use
<order> for the other unconfirmed 32-bit tags**, which is ticked by default. That check box changes
only their byte order, not their types, and they stay not confirmed. Tags you set or confirmed are
never changed by it.

### When the result cannot be decided

Some readings do not tell the encodings apart:

- **Registers at 0.** Every encoding reads 0. Wait for a value, or type the value shown on the
  device.
- **Small integers.** The same two registers can mean two different numbers. As an example, the
  test server holds `int32` 100 at HR 120-121 in `ABCD` order (registers `0x0000`, `0x0064`). Read
  in `BADC` order, the same registers give 25600. If the app cannot tell which is right, the
  dialog lists both and asks for the display value. Type 100, and only the `int32 ABCD` row
  matches (`uint32 ABCD` reads the same value and is listed with it).

### The check on connect

When you connect, the app checks the 32-bit tags that have enough good readings. If a tag's current
encoding gives implausible values and another encoding fits far better, the tag is marked **not
confirmed**. One message lists these tags, with two buttons: **Review...** opens the dialog for each
listed tag in turn, and **Close** leaves them as they are. Tags that never read good are skipped.

### A 16-bit tag on half of a 32-bit value

A 16-bit tag can sit on half of a 32-bit value, and then it reads ordinary-looking numbers. The test
server holds `float32` 52.3 in `CDAB` order at HR 110-111: register HR 110 is `0x3333` and HR 111 is
`0x4251`. A tag left at `int16` on HR 110 reads 13107, and one on HR 111 reads 16977. Neither looks
wrong.

A write to such a tag is the real problem. A 16-bit write replaces half of the float, and the device
then holds a different number. For example, writing 50 to HR 111 leaves `0x3333` in HR 110 and
`0x0032` in HR 111. The device reads those two registers in `CDAB` order as `float32` 4.6e-39, which
is practically zero.

The app checks a 16-bit tag in these cases:

- when you add it, or change its register number, on its first good reading while connected;
- once for every 16-bit tag when you connect. This pass runs once per tag for as long as the tag
  list stays the same (until you open or start another project): reconnecting does not check the
  same tags again.

Each check reads the register below and the register above the tag, once. It does not repeat on
every poll. A neighbouring register that another tag of the project uses is ignored: it is mapped as
something else, and a change there would make one tag overwrite another. A tag whose own register
reads 0 is never flagged. If the device does not answer one of these reads (a timeout, or the
connection drops), the app stops checking 16-bit tags until you open or start another project, and
says so in the status bar. Some devices drop the connection on reads outside their register map.

The check looks for a "clean" float32: two neighbouring registers that read, in `ABCD` or `CDAB`
order, as a float of a sensible size with at most 5 significant digits, or 6 with at most 2
decimals, such as 52.3, 0.5, 100 or 1013.25.

When the check finds one, the tag is flagged. Its Name cell shows the warning icon, and its tooltip
names the 32-bit reading, for example "HR 110-111 read as float32 CDAB give 52.3". The app then asks
what to do:

- **Change to float32 CDAB at HR 110** moves the tag to HR 110 and makes it a `float32` in `CDAB`
  order, in one step. The change is confirmed. Gain and offset go back to 1 and 0 and the quick
  values are cleared, because they were meant for the 16-bit reading; the write limits are kept.
  The prompt says so when the tag has a gain, an offset or quick values.
- **Keep int16** confirms that the tag really is 16-bit, and clears the flag. The app remembers the
  answer in the project: the tag is not checked again on its own, unless you change its register
  number, its area or its type. **Detect encoding...** still checks it.
- **Decide later** leaves the tag flagged.

On connect, the flagged 16-bit tags are listed in one message, with **Review...** and **Close**.
**Review...** opens the prompt for each listed tag in turn. If another prompt or a Detect encoding
dialog is open at that moment, the message waits and appears when it closes.

Writes to a flagged 16-bit tag ask first, as writes to an unconfirmed 32-bit tag do (see Writes to an
unconfirmed tag). In that prompt, **Confirm and continue** means the tag really is 16-bit.

To check a flagged tag again, right-click it and choose **Detect encoding...**. This works only while
the application is connected. If the new check finds no float32, the tag stays flagged and a prompt
says so, with **Keep int16** and **Decide later**: choose **Keep int16** when the tag is 16-bit. A
check that runs on its own (when you connect, or after you move the tag) and finds nothing clears a
flag that has no 32-bit reading behind it, for example on a tag you moved away from the pair.

The check has limits:

- Two separate 16-bit registers can form a clean float by chance. For example, 16384 and 0 read as
  `float32` 2.0. The weak spot is a value next to an empty register: a value between roughly 15000
  and 20000 (or a status word such as `0x4000`) often reads as a clean float, because 17000 next to
  0 reads as 58.0. Over all 16-bit values next to an empty register, about 7 % read as a clean float
  (measured on the test data). Ordinary small values and typical raw analog values are almost never
  flagged (about 0.5 % for raw values 0 to 27648). If that happens, choose **Keep int16** once: the
  app remembers it in the project.
- A float that is a full-precision measurement, such as 23.456789, is not clean, so it is not
  detected.
- Halves of an `int32` value cannot be told apart from two separate 16-bit registers, so they are
  not detected.
- Only the word orders `ABCD` and `CDAB` are checked. With the byte-swapped orders (`BADC`, `DCBA`),
  ordinary small numbers next to an empty register would read as clean floats (63 and 0 read as
  0.5), so a device that byte-swaps its floats is not checked.

Tags that were never checked write normally.

To try it with the test server:

1. Start the test server and connect the application to it.
2. Add a **Holding** tag at address 110 with type `int16`. The app flags it: the Name cell shows the
   warning icon, and the tooltip reads "HR 110-111 read as float32 CDAB give 52.3". A prompt opens.
3. Choose **Change to float32 CDAB at HR 110**. The tag becomes `float32` `CDAB` at HR 110 and reads
   52.3.
4. To see the move, delete that tag (its registers HR 110-111 would otherwise be used by it), then
   add an `int16` tag at address 111, or do this in a new project. Choose **Change to float32 CDAB
   at HR 110** in its prompt. The tag is moved to HR 110.

### A device that refuses to read half of a 32-bit value

Some controllers, such as the ABB CM50, refuse to read one register of a 32-bit value. They answer a
read of that register alone with "Illegal data address". A 16-bit tag placed on a float32 then never
reads, so the check above cannot see its value. The app then tries to read the register as the
first and as the second half of a 32-bit value: for a tag at HR 110, it reads HR 110-111, then
HR 109-110.

- If one of these reads works, the tag is flagged (its Name cell shows the warning icon). The
  Detect encoding dialog opens, with a line that explains why: "'Setpoint' (HR 110, int16) can't be
  read alone." and the device's "Illegal data address" answer. Type the value shown on the device
  in **Value shown on the device (optional)** to pick the encoding, then click **Apply**. The tag is
  moved to the start of the 32-bit value, its type is changed and the encoding is confirmed. Gain,
  offset and quick values are reset, as in the section above.
- **Cancel** leaves the tag flagged. Writes to it ask first.
- If neither read works, the register probably does not exist. Nothing changes. Detect encoding...
  then reports that the register can't be read alone or as part of a 32-bit value, and asks you to
  check the register number.

The check runs when you add or move a 16-bit tag, once per session on connect, and when you choose
**Detect encoding...** from the right-click menu, which works on every tag.

To try it with the test server, start it with `--strict-32bit`, for example
`build\msvc-debug\bin\modbus_test_server.exe --port 1502 --strict-32bit`,
and connect the application to it. Add a **Holding** tag at address 110 with type `int16`. The read
of HR 110 alone is refused, the read of HR 110-111 works, and the Detect encoding dialog opens with
the explanation. Type `52.3` in **Value shown on the device**, select the `float32 CDAB` row, and
click **Apply**. The tag becomes `float32` `CDAB` at HR 110 and reads 52.3.

### Writes to an unconfirmed tag

Before a write to a 32-bit tag whose encoding is not confirmed, the application asks first. The
prompt says, for example, "The encoding of 'Setpoint' is not confirmed." and shows what the tag
reads (52.3 °C) and the encoding it reads it as (float32 CDAB, HR 110-111), then warns that a write
would send a wrong value if the encoding is wrong. It has three buttons:

- **Confirm and continue** confirms the encoding and carries on with the other write checks and the
  usual confirmation.
- **Detect encoding...** opens the dialog. Nothing is written.
- **Cancel** is the default. Nothing is written.

If you write several tags at once and some of them are unconfirmed, one prompt lists them all, and
**Confirm and continue** confirms all of them.

### What confirms an encoding

An encoding is confirmed when:

- you click **Apply** in the Detect encoding dialog;
- you click **Confirm and continue** in the write prompt;
- you choose another **Byte order** for the tag yourself;
- automatic detection finds that the readings fit the encoding the tag already has;
- for a flagged 16-bit tag, you click **Change to float32 CDAB at HR 110** or **Keep int16** in its
  prompt (**Keep int16** is also saved in the project).

A tag that automatic detection changed stays unconfirmed, with its warning icon, until one of these
happens.

### Try it with the test server

1. Start the test server and connect the application to it.
2. Add a **Holding** tag at address 110 with type `float32` and byte order `ABCD`. The test server
   holds `float32` 52.3 in `CDAB` order at HR 110-111.
3. After three readings the app detects `float32 CDAB`, changes the tag to it, and shows the warning
   icon on the Name cell.
4. Right-click the tag, choose **Detect encoding...**, type `52.3` in **Value shown on the device**,
   and click **Apply**. The float32 CDAB row matches, and the warning icon goes away.

If you choose `CDAB` yourself instead, your choice confirms the encoding and detection stops for
that tag. The dialog is still available from the right-click menu, and typing 52.3 shows the same
match.

## Trend

The trend shows each plotted tag as one curve, in the tag's colour. The X axis is time in seconds,
since the first connection in this run (see Recording). The left Y axis shows the scaled value of
the tags with **Axis** set to `Left`.

### Right Y axis

Tags with **Axis** set to `Right` (tag table) are drawn against a second Y axis on the right of the
plot. Use it for a tag whose unit or range differs from the others, for example a temperature next
to a valve position.

- The two Y axes scale separately. Each one fits only its own curves, so a tag on the right does not
  change the left axis, and the reverse.
- Each axis title lists the units of its tags. The right axis is shown only while at least one
  plotted tag uses it.
- **Fit Y** fits each axis to its own tags.

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
| Shift + mouse wheel | Zooms both Y axes. Auto-scaling stops until Run or Fit Y. |
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

- **Value at cursor** (default): the recorded value at the cursor, interpolated between the two
  samples around it.
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

The search never leaves the cursors: it starts at C1 with the 0 % level and ends at C2 with the
100 % level. So 0 % is always found and is never before C1: if the signal does not come back to
the starting level after C1 (noise, or C1 placed on a part that is already rising), 0 % is C1
itself. Likewise 100 % is never after C2.

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
| Fit RMS | How well the model follows the data (see Model curve). Shown as the value and, in brackets, as a percentage of ΔPV, for example `0.12 (0.6 %)`. |

A tag without a valid result shows its error text across the value columns. A value that cannot be
computed shows "—". Numbers use 6 significant digits.

Each found setpoint is also marked on the plot with a cross at its time and target level, in the
tag's colour and labelled, for example, "63.2 %".

The label is drawn in a white box with a border in the tag's colour, so it stays readable where
it crosses a curve. Tick **Show time and value on plot labels** (under Setpoints) to add two lines
to each label: the time (`t = 15.512 s`) and the target value with its unit (`y = 56.32 degC`).
This option is saved in the project.

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

### Model curve

Tick **Show model curve** in the Model group to draw the fitted model over the response. It is a
dashed line in the tag's colour, from C1 to C2. It starts at the 0 % level, stays there until the
step time plus the dead time L, and then rises towards the 100 % level with the time constant T.
It is drawn only when L and T are valid for the tag. The option is saved in the project.

Compare the curve with the measured curve. If they lie on top of each other, the first-order model
with dead time describes the process well.

**Fit RMS** in the Model table is the root-mean-square of the difference between the recorded values
and the model, over the samples between C1 and C2. It is in the units of the tag. The percentage is
Fit RMS as a share of |ΔPV|. A small value means a good fit: a few percent of ΔPV or less is a
close match. A large value means the response is not first order, or the times were read from
a noisy or badly placed step. Fit RMS shows "—" when no model is valid.

### Guide lines

**Guide lines** (under Setpoints, on by default) draws, for every found setpoint, a dashed line from
the marker straight down to the bottom of the plot and a dashed line from the marker straight left
to the left edge. They show the time and the level of each setpoint. The lines follow zoom and pan,
and are included in the graph image. Untick the box to hide them.

### Copying results

- **Ctrl+C** on the Setpoints or Model table copies the selected cells. With nothing selected it copies
  the whole table, with its header row.
- **Copy results** (below the buttons at the top of the Analysis tab) copies both tables, with a blank
  line between them.

The text is tab-separated. Pasting it into Excel or Word puts each value in its own cell.

### Exporting results

- **Export > Analysis results (CSV)...**, or **Export results (CSV)...** on the Analysis tab, is
  enabled when at least one tag has a valid result. The default name is `analysis.csv` in the last
  export folder, or in Documents.
- The file is UTF-8 with CRLF line endings and comma separators. It has two sections, separated by an
  empty line. The first has one row per setpoint (`tag,percent,target,measured,time_s,time_from_c1_s`).
  The second has one row per tag (`tag,c1_s,c2_s,y0,y100,delta_pv,delta_co,step_time_s,gain_k,dead_time_l_s,time_constant_t_s,fit_rms,fit_rms_percent,note`).
  A missing value is an empty cell, so `fit_rms` and `fit_rms_percent` are empty when no model is valid.
  For an invalid result, `note` holds the error text.
- **Export > Graph image...** while the Analysis tab is active exports the Analysis plot, including the
  setpoint markers. With **Include cursor readout** ticked, the box shows the results summary.

### Saving

The Analysis settings (reference, window, smoothing, setpoints, model and ΔCO, plot labels, model
curve and guide lines) and the **Analyze** flags are saved in the project. The cursors are not.
A session also keeps the cursors (see [Sessions](#sessions)).

### Try it with the test server

1. Start the test server in a console: `build\msvc-debug\bin\modbus_test_server.exe --port 1502`.
2. Start the application with `examples\demo.mbproj` (or open it with File > Open), then click **Connect**.
3. Double-click the **Value** cell of **Setpoint** (HR 100), type 0 and confirm. Or click **A: 0** in
   the Quick write dock. Wait a few seconds.
4. Open the **Analysis** tab and place C1 now, before the step.
5. Write 1000 to **Setpoint** the same way, or click **B: 1000** in the Quick write dock, and wait about 10 s. The **Lag** signal (IR 10) starts to
   rise after its 1 s dead time, with a time constant of 2 s. When it has settled, place C2.
6. In **Model**, select **Setpoint** as the ΔCO tag. Tick **Show model curve** to draw the fitted
   model over the Lag response between the cursors.
7. The Lag row shows K ≈ 1, L ≈ 1 s and T ≈ 2 s. Its **Fit RMS** should be small compared with ΔPV,
   and the dashed model curve should follow the measured curve.

## Projects

Projects are saved as `.mbproj` files. A project holds the connection settings, the register numbering
(1-based or not), the tag list, including tag colours, the **Analyze** flags, the Y axis and the quick-write presets, and the Analysis settings. Recorded data, cursors and the trend window are not saved in a project. To keep those, save a [session](#sessions).

In the file, each tag has an `analyzed` key, an `axis` key (`"left"` or `"right"`) and optional
`quickA` and `quickB` keys (the quick-write presets) and `writeMin` and `writeMax` keys (the write
limits; a key is written only when the limit is set). The Analysis settings are in an `analysis` object
(`percents`, `reference`, `referenceWindowS`, `smoothingS`, `modelPercents`, `deltaCoTag`, `deltaCo`,
`showModel`, `guideLines`). A top-level `hiddenColumns` array lists the tag-table columns hidden in this project (for example `"raw"`, `"gain"`, `"quickA"`). All of these are optional: a file without them loads with the defaults (the compact view for `hiddenColumns`).

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

## Sessions

A session file (`.mbsession`) keeps a recording so that you can look at it later, without a
connection. It holds:

- the project: connection settings, tags and Analysis settings,
- the recording: the trend history with its start time,
- the cursors of the Trend and of the Analysis tab.

| Menu item | Action |
|---|---|
| File > Save session... | Save the project, the recording and the cursors to a `.mbsession` file. `.mbsession` is added when missing. |
| File > Open session... | Open a session. Connection is closed first, if it is open. |

### Offline viewing and analysis

After **Open session...** the Trend, the Cursors panel and the Analysis tab show the recorded data
and the saved cursors. Nothing is polled. The Analysis works as usual, and so do the graph image
and CSV exports. Writes are not possible until you connect.

The window title shows the session file name. The project inside a session is not a `.mbproj` file
yet: use File > Save As... to keep it as one.

This is the way to analyse a lab recording at home: save the session at the lab, copy the file, and
open it with File > Open session... at home.

### Connecting after opening a session

When you click **Connect** with a session loaded, a question asks what to do with the recording:

- **Continue this recording**: the new samples are added after the loaded data. A gap is drawn in the
  trend, and the time keeps going from the loaded start time.
- **Start a new recording**: the loaded data is removed from the view and the time starts again at
  0. The session file on disk is not changed.
- **Cancel**: nothing is connected.

### Unsaved changes

Once a session is loaded, new samples recorded while connected make the recording "changed".
New, Open, Open session and Quit then ask "The recording has changed. Save the session?" with
**Save**, **Discard** and **Cancel**. Save opens the Save session dialog. Without a loaded session,
the project prompt is used, as before.

### File size

The session file holds the whole recording in memory, so its size follows the length of the
recording and the number of tags. The history is limited to 72 000 rows (about 2 hours at a 100 ms
period). For 2 hours at 100 ms with 20 tags, the file is about 14 MB.

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

**Writes.** The manual documents holding registers for FC03 (read) and FC06 (write single). The
float32 setpoints of `examples/cm50.mbproj` are written with FC16 (Write Multiple Registers): one
request carries both registers, so the controller never holds half old, half new. If your CM50 does
not accept FC16, it rejects the request (nothing is written) and the app says so; such a controller
cannot take float32 writes from this app.

**First writes to a real controller**

1. Connect with every tag read-only (Writable unticked). Compare each value with the controller's
   display. Byte order and register numbers are right only if the values match. A tag with the
   warning icon has an unconfirmed encoding: confirm it with **Detect encoding...** (see Encoding
   detection) before you write to it. A 16-bit tag on half of a float32 setpoint is flagged the same
   way (see [A 16-bit tag on half of a 32-bit value](#a-16-bit-tag-on-half-of-a-32-bit-value)).
2. On each writable tag, set **Min** and **Max** to the controller's allowed range for that
   parameter.
3. Tick **Writable** on the tag.
4. Write the setpoint's current value back unchanged, and check that it reads back identically.
5. Then make one small change, and check the controller's display and the trend.
6. Keep quick-write confirmation on (**Options > Confirm quick writes**) until you know the tags.

**Example tags** (register numbers as printed, with the 1-based option on):

| Name | Area | Register | Type | Notes |
|---|---|---|---|---|
| Loop 1 Process Variable | Input | 5009 | float32, `ABCD` | Read-only. |
| Loop 1 Control Output | Input | 5013 | float32, `ABCD` | Read-only. |
| Loop 1 Local Setpoint 1 | Holding | 6639 | float32, `ABCD` | Writable. Example limits Min 0, Max 100 (placeholders). |
| Alarm 1 Trip | Holding | 6591 | float32, `ABCD` | Writable. Example limits Min 0, Max 100 (placeholders). |
| Totalizer 1 Batch Total | Input | 4001 | uint32, `ABCD` | The manual does not state the signedness; try `int32` if the value looks wrong. |
| Instrument Type | Input | 3 | uint16 | Read-only. |

`examples/cm50.mbproj` contains these tags, with the 1-based option on, host 192.168.1.50, port 502,
unit 1, and a 500 ms period. Open it with File > Open, or give its path on the
command line, then change the host and unit to match your controller. Setpoint and trip values are
written with confirmation, but check them against the controller's own limits first.

The example file has **Min** and **Max** limits of 0 and 100 on **Loop 1 Local Setpoint 1** and
**Alarm 1 Trip**. These are placeholder values, not the controller's real ranges. Replace them with
the allowed range of each parameter from your controller before you write to it.

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
- The history is in memory only. A project file does not store it; a session file does (see
  [Sessions](#sessions)).
