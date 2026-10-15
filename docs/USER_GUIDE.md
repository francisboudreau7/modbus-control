# Modbus Control user guide

Read online: [https://francisboudreau7.github.io/modbus-control/guide/](https://francisboudreau7.github.io/modbus-control/guide/)

New to the app? Start with the [Quick Start](QUICK_START.md). En français :
[Guide de l'utilisateur](fr/GUIDE_UTILISATEUR.md).

This guide describes the application as built from this repository. For building, see the
[README](../README.md). For the test server, see [TEST_SERVER.md](TEST_SERVER.md). For a simulated level loop to practise step
tests on, see the [Loop simulator](LOOP_SIMULATOR.md).

## Window layout

- **Connection bar** (top): host, port, unit, timeout and period fields, a status LED, and the Connect / Disconnect button.
- **Trend** (centre): the live graph with its controls above it.
- **Tags** (bottom dock): the tag table with Add, Duplicate, Remove and Write selected buttons.
- **Cursors** (right dock): cursor times and the readout table of the Trend.
- **Quick write** (right dock, tabbed with Cursors and Analysis): one button per preset value of the writable
  tags (see [Quick writes](#quick-writes)).
- **Analysis** (right dock, tabbed with Cursors and Quick write): the step-response analysis of the
  Trend (see [Analysis](#analysis-step-response)).
- **Setpoints** (right dock, tabbed after Analysis): which curves show setpoint markers, the
  setpoint percentages, references, smoothing and the setpoint table (see
  [Setpoints and options](#setpoints-and-options)).
- **Menus**: File, Export, View, Options and Help. **Help > Quick Start** and **Help > User Guide**
  open these guides online (in French when Windows is in French), or the copy installed with the
  application when the browser cannot be opened.
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
| Axis | `Left` or `Right`: the Y axis the tag's curve uses on the Trend. See [Trend](#trend). |
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
- **Add** opens the Add tag dialog (see below). Nothing is guessed: you choose the area and the
  address yourself.
- **Duplicate** copies every selected row. The copy's name gets " (copy)".
- **Remove**, or the Delete key, removes the selected rows. You are asked to confirm when more
  than one row is selected.
- **Write selected...** is enabled when two or more rows are selected and all of them are
  writable holding registers. See Writing values.

Tag edits mark the project as modified. Live values do not.

### Add tag dialog

Choose the **Area** (holding or input register) and type the **Address**, in the numbering the
Options menu shows. The area is empty and the address is blank when the dialog opens. The **Add**
button stays disabled until both are valid and the tag does not share a register with an existing
tag. A red line names the tag it overlaps. Name, type, byte order, unit and Writable are optional.
A blank name becomes `Tag <id>`.

While connected, the dialog reads the register for you:

- **Read** shows the raw register and its neighbours (as hex), and what they mean as a 16-bit
  value and as float32 in both directions. It also runs by itself shortly after you change the
  area or the address, or when you connect. Neighbours that belong to another tag are not read.
- **Detect encoding** decides between a 16-bit value and a float32, at the address or one register
  earlier. If it looks like a float32, the encoding dialog opens (see Encoding detection). When you
  apply it, the dialog sets the type, the byte order and, if needed, the address one register
  earlier. Some devices refuse to read a single register of a 32-bit value. Detect then reads the
  pair instead and says so. A register that reads 0 cannot be told apart: try again when the value
  is not 0. If a neighbour cannot be read, Detect can't rule out a float32 and says so: choose the
  type yourself.
- A tag added with Detect's answer is confirmed, as long as you did not change the area, address,
  type or byte order afterwards. You do not get the encoding dialog a second time. Otherwise the
  tag is checked like any other new tag.

When you are not connected, the dialog cannot read anything: it adds the tag as you set it. The tag
is checked when you connect, like any other new tag. Cancel adds nothing. The new row is selected.

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
2. Open the **Quick write** dock (right side, tabbed with Cursors and Analysis; reopen it from the View menu).
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

The Add tag dialog has its own **Detect encoding** button. It decides before the tag exists, so
the new tag starts confirmed (see Add tag dialog).

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
Before that it reads the tag's register alone once more and continues only if the device answers
"Illegal data address". When one tag is refused, the tags next to it keep reading: polling falls
back to reading the tags of a failing group one by one.

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

Two vertical cursors, **C1** (red) and **C2** (blue), mark times on the trend. The
[Analysis](#analysis-step-response) panel uses the same two cursors.

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
  box at the top left of the image. When the Analysis panel has a valid result, the box also holds
  the analysis summary (K, L, T and the fit error; see [Analysis](#analysis-step-response)).

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

The Analysis panel measures a step response on the Trend plot, between the two cursors C1 and C2.
It finds the times at which the response reaches the setpoints (0 % and 63.2 % by default). From
these times it computes a first-order model with dead time: the gain K, the dead time L and the time
constant T. It also computes PID settings from them. It is meant for step tests, for example to get
the values for Ziegler-Nichols open-loop tuning.

Open the panel with **View > Analysis**. It is the **Analysis** dock on the right side, tabbed with
**Cursors** and **Quick write**. Its controls are in four numbered groups, one for each step:

1. **Signals**: the process value, the controller output and the optional ranges.
2. **Cursors**: where C1 and C2 are, and **Find step**.
3. **Model**: the method, the results, the model curve and the checks.
4. **Tuning**: the PID settings.

The setpoint options and the setpoint table are in their own **Setpoints** tab, next to Analysis (see
[Setpoints and options](#setpoints-and-options)). Hover over any value to see how it was calculated
(see [Tooltips](#tooltips)).

The overlays (setpoint markers, guide lines and the model curve) are drawn on the Trend plot, and the
graph image export includes them.

### 1. Signals

- **Process value (PV)**: the measured signal that responds to the step, for example a temperature.
  Choose it in the list. It shows **Choose...** until you choose a tag. Choosing a PV also shows its
  setpoints (see [Setpoints and options](#setpoints-and-options)).
- **Controller output (CO)**: the signal that was stepped, for example a setpoint or a valve position.
  Its change between the cursors is **ΔCO**. Choose the CO tag in the list. To type the step yourself,
  choose **Typed ΔCO** and enter the step, in the output's units, in the box next to it. A typed ΔCO
  has no unit in the results.

Without a ΔCO the panel cannot compute K. L and T are still computed.

**Ranges for %/% gains** (optional). Type the **PV min** and **PV max** of the process value, and the
**CO min** and **CO max** of the controller output (default 0 and 100). The panel then also shows the
gains in percent of the ranges, **K (%/%)** and **Kp (%/%)**:

- K (%/%) = K × CO span / PV span, where the span is max − min.
- The PV range is used only when both PV boxes are filled in. An empty box means no range.
- A span of 0 counts as no range. This includes a range whose min and max are equal: it is ignored,
  and a project saved with such a range still opens.

### 2. Cursors

C1 and C2 are the two cursors of the Trend (see [Cursors](#cursors)). The **C1 = …, C2 = …** line in
this group shows their times. A dash (—) means that cursor is not placed.

- **C1** goes just before the step, while the PV still has its starting value.
- **C2** goes after the PV has settled.

The hint line below the buttons tells you what is still missing:

- "Choose the process value (PV) in step 1." The PV is not chosen yet.
- "Place C1 just before the step and C2 after the PV has settled, or click Find step." A cursor is
  missing.
- Otherwise, the reason why there is no result, if there is one (for example "No change between C1
  and C2").

**Find step** places C1 and C2 for you. It needs a PV and a CO in step 1. When it is disabled, its
tooltip says what is missing. It looks at the visible time range of the Trend (the whole recording if
nothing is visible), so zoom or scroll to the step first. It then:

- finds the largest change of the controller output in that range. That change is the step;
- puts C1 5 s before the step, or halfway back to the previous change of the output when that is
  closer. C1 is never later than the last recorded sample of the output before the step, so with a
  slow poll period ΔCO between the cursors is the whole step;
- puts C2 after the PV has settled: at the settling time plus 20 % of the time the settling took.
  If the PV does not settle before the end of the range (for example because of noise), C2 goes to
  the end of the range, and the check **The PV is still moving at C2** appears. It disappears when
  you move a cursor yourself or change the PV or CO.

If no step is found, the hint line says "No step found in the controller output in the visible
range." Find step is a starting point. Check the cursors on the Trend afterwards.

### 3. Model

**Method** chooses the two points of the response that give L and T. Let ta and tb be the times at
which the PV reaches the two points a and b (as fractions). All times are measured from the step time
t_step, which is the time of the controller output's step.

| Method | Points (a / b) | T (time constant) | L (dead time) |
|---|---|---|---|
| 0 % / 63.2 % (two points), the default | 0 % / 63.2 % | T = t63.2 − t0 | L = t0 |
| Smith | 28.3 % / 63.2 % | T = 1.5 (t63.2 − t28.3) | L = t63.2 − T |
| Sundaresan-Krishnaswamy | 35.3 % / 85.3 % | T = 0.67 (t85.3 − t35.3) | L = 1.3 t35.3 − 0.29 t85.3 |
| Custom | **From %** (a) and **To %** (b) | see below | see below |

For **Custom**:

- T = (tb − ta) / (ln(1 − a) − ln(1 − b))
- L = ta + T · ln(1 − a)

**From %** and **To %** are used only for Custom. They must satisfy a < b, and both must be below 100.
The two points do not have to be in the Setpoints list: the panel searches for their times anyway.

**Results** (the table in this group):

| Row | Meaning |
|---|---|
| y0 | The PV level at C1 (0 %). |
| y100 | The PV level at C2 (100 %). |
| ΔPV | y100 − y0: how much the PV moved. |
| ΔCO | The change of the controller output between the cursors, or the typed value. |
| K | The process gain, K = ΔPV / ΔCO, in the PV unit per CO unit, for example `°C/%`. |
| K (%/%) | The gain in percent of the ranges. Shown only when both ranges are set. |
| L (s) | The dead time: the delay before the PV starts to respond. |
| T (s) | The time constant: how fast the PV follows after the dead time. |
| L/T | The controllability ratio. Below 0.3 is easy to control. Above 1, the dead time dominates. |
| Fit error | How far the model is from the measured PV, as an RMS value in the PV unit and as a percentage of ΔPV, with a word: **Good**, **Acceptable** or **Poor**. |

The fit error is Good below 3 %, Acceptable below 8 %, and Poor otherwise. A small fit error means the
first-order model with dead time describes the response well.

A value that cannot be computed shows "—".

**Show model curve** (on by default) draws the fitted model over the response: a dashed line in the
PV's colour, from C1 to C2. It stays at the 0 % level until the step time plus L, and then rises
towards 100 % with the time constant T. Compare it with the measured curve. If they lie on top of each
other, the model describes the process well. The option is saved in the project.

**Checks.** Below the results the panel lists the checks that apply, each with a warning icon. The
list is hidden when there are no checks. Each message and what to do:

| Message | What to do |
|---|---|
| "K can't be computed: choose the controller output (CO) or type ΔCO in step 1." | Choose the CO tag, or choose **Typed ΔCO** and type the step. |
| "The controller output (CO) does not change between the cursors: K can't be computed. Move the cursors around the step." | A CO tag is chosen, but it has the same value at C1 and C2. Place C1 before the step and C2 after it, or use **Find step**. |
| "L and T can't be computed: the PV does not reach both model points (a % and b %) between the cursors." | The PV does not get past both levels before C2. Move C2 later, after the PV has risen past the second level, or choose another method. |
| "The PV is still moving at C2: move C2 later." | Move C2 later, until the PV is flat. |
| "The step is small compared with the noise: the results are uncertain." | Use a bigger step, or set **Reference** to **Mean before cursor** for a noisy signal. |
| "L is negative: C1 is after the start of the response. Move C1 before the step." | Move C1 earlier, to before the step. Or use **Find step**. |
| "L/T > 1: dead time dominates. Ziegler-Nichols and Cohen-Coon will be aggressive; prefer IMC / lambda or SIMC." | Choose **IMC / lambda** or **SIMC** in the Tuning group. |
| "The model does not fit the response well (fit error > 8 %)." | Check that the cursors frame the step and that the model curve follows the PV. The response may not be first order, so K, L and T are rough. |

### 4. Tuning

The tuning calculator turns K, L and T into P, PI and PID settings. Choose a rule in **Rule**. The
table shows one row for each controller type. With r = L / T, the rules are:

| Rule | Controller | Kp | Ti (s) | Td (s) |
|---|---|---|---|---|
| Ziegler-Nichols (open loop) | P | T / (K · L) | — | — |
| | PI | 0.9 · T / (K · L) | L / 0.3 (about 3.33 · L) | — |
| | PID | 1.2 · T / (K · L) | 2 · L | 0.5 · L |
| Cohen-Coon | P | (T / (K · L)) · (1 + r / 3) | — | — |
| | PI | (T / (K · L)) · (0.9 + r / 12) | L · (30 + 3r) / (9 + 20r) | — |
| | PID | (T / (K · L)) · (4/3 + r / 4) | L · (32 + 6r) / (13 + 8r) | 4 · L / (11 + 2r) |
| IMC / lambda | P | not defined | — | — |
| | PI | T / (K · (λ + L)) | T | — |
| | PID | (T + L/2) / (K · (λ + L/2)) | T + L/2 | T · L / (2T + L) |
| SIMC | P | T / (K · (λ + L)) | — | — |
| | PI | T / (K · (λ + L)) | min(T, 4 · (λ + L)) | — |
| | PID | not defined | not defined | not defined |

- **λ** (lambda) is the closed-loop time constant of IMC / lambda and SIMC, in seconds. A larger λ
  gives a slower, gentler controller. Enter 0, which is shown as **Auto (= L)**, to use λ = L. The box
  is enabled only for IMC / lambda and SIMC.
- The Ti and Td values are in seconds. Tick **Ti, Td in minutes** to show them in minutes. The
  calculation is the same.
- The controllers use the ideal (ISA) form, which the note under the table gives:
  u = Kp (e + 1/Ti ∫e dt + Td de/dt).
- Kp is in the CO unit per PV unit. If K is negative (the PV falls when the output rises), Kp is
  negative too. The note then says "K is negative: the controller must be direct acting (Kp is shown
  negative)."
- **Kp (%/%)** is shown only when both ranges are set.
- The table is empty while K, L or T is missing, or while L is zero or negative. Fix the results above
  first. "not defined" means that the rule gives no setting for that controller.

The rules are a starting point. Test the controller on the process before you rely on the values.

### Setpoints and options

These options are in the **Setpoints** dock, a tab next to **Analysis** on the right side (View >
Setpoints).

- **Show setpoints on**: tick the PV and the other plotted tags whose setpoints you want to see. The
  panel finds their setpoint times too. Each ticked tag gets markers on the Trend, guide lines and
  rows in the setpoint table. The list is saved in the project. A tag that is not plotted on the
  Trend shows no markers, guide lines, model curve or rows, even when it is ticked. An unplotted PV
  is still analysed (results and tuning), and an unplotted tag other than the PV is not analysed at
  all, so the exported results match what the panel shows.
- **Reference**: how the 0 % and 100 % levels are taken.
  - **Value at cursor** (default): the recorded value at the cursor, interpolated between the two
    samples around it.
  - **Mean before cursor**: the mean of the recorded values in the window **W** seconds before the
    cursor (0.1 to 3600 s, default 1 s). Use it when the signal is noisy.
- **Smoothing** S (**Off** by default, up to 3600 s): a centred moving average of width S, applied to
  the samples between the cursors before the setpoint times are searched. The levels are not smoothed.

  Smoothing has a bias. A centred average starts to rise up to S/2 before the real response, so the
  0 % time comes out early by up to S/2, and L comes out too small. With smoothing on, use a model
  **From %** above 0 (for example 10), or keep S small compared with L.
- **Setpoints (%)**: the percentages of the step whose times are found. The default is 0 % and 63.2 %.
  Type a value from 0 to 100 (two decimals; the box starts at 28.3, the value Smith's method needs)
  and click **Add**. Select an entry and click **Remove**. The list stays sorted, has no duplicates and
  holds at most 20 values.
- **Show time and value on plot labels**: adds two lines to each setpoint label on the plot: the time
  (t = 15.512 s) and the target value with its unit (y = 56.32 °C).
- **Guide lines** (on by default): for every found setpoint, a dashed line from its marker straight
  down to the bottom of the plot, and one straight left to the Y axis. They show the time and the level
  of each setpoint. They follow zoom and pan, and they are included in the graph image.
- **Setpoint table**: one row for each checked tag and each setpoint, with these columns:

| Column | Meaning |
|---|---|
| Tag | Colour swatch and name [unit]. |
| % | The setpoint. |
| Target | The level of the setpoint: y0 + % × ΔPV. |
| Measured | The recorded (unsmoothed) value at the found time. |
| Time (s) | The found time. "—" when it is not found between the cursors. |
| t − C1 (s) | The found time minus the time of the earlier cursor. |

Each found setpoint is marked on the Trend with a cross at its time and level, in the tag's colour,
with a label such as "63.2 %". The label is drawn in a white box with a border in the tag's colour, so
it stays readable where it crosses a curve.

#### How the times are found

For each setpoint, the recorded samples between the cursors are searched as follows:

1. The 50 % point is found first: the first sample that has reached the 50 % level, counted from the
   starting value. This is the anchor.
2. For a setpoint of 50 % or more, the search runs forwards from the anchor to the first sample that
   has reached the setpoint level.
3. For a setpoint below 50 %, the search runs backwards from the anchor to the last sample that is
   still on the starting side of the setpoint level.
4. The time is interpolated linearly between the two samples on either side of the level.

Missing samples are skipped. When the level is never reached, the time shows "—".

The search never leaves the cursors: it starts at C1 with the 0 % level and ends at C2 with the 100 %
level. So 0 % is never before C1. If the signal does not come back to the starting level after C1
(noise, or C1 placed on a part that is already rising), 0 % is C1 itself. In the same way, 100 % is
never after C2.

The model points are searched in the same way, even when they are not in the Setpoints list.

### Tooltips

Every value in the results, tuning and setpoint tables has a tooltip. It explains the value in three
parts:

1. the name of the value and what it means;
2. the general formula;
3. the same formula with this result's numbers, and the value.

For example, the K tooltip reads: "K, process gain: change of the PV per unit of controller output,
once settled. K = ΔPV / ΔCO, K = 12.5 / 10 = 1.25 °C/%." The tooltips for L and T show the formula of
the chosen method with the crossing times. The tooltips of the tuning values show the rule's formula
with K, L, T and λ filled in. Numbers in the tooltips have 4 significant digits.

### Copy results and export

- **Copy results** (the button at the top of the panel) copies the results table, the tuning table and
  the setpoint table, with a blank line between them. The tuning rows start with the controller type
  (P, PI or PID).
- **Ctrl+C** on a table copies the selected cells. With nothing selected, it copies the whole table,
  with its header row.

The text is tab-separated. Pasting it into Excel or Word puts each value in its own cell.

**Export results (CSV)...** on the panel, or **Export > Analysis results (CSV)...**, is enabled when
the PV has a valid result. The default name is `analysis.csv` in the last export folder, or in
Documents.

- The file is UTF-8 with CRLF line endings and comma separators. It has two sections, separated by an
  empty line. The first has one row per setpoint (`tag,percent,target,measured,time_s,time_from_c1_s`).
  The second has one row per tag (`tag,c1_s,c2_s,y0,y100,delta_pv,delta_co,step_time_s,gain_k,dead_time_l_s,time_constant_t_s,fit_rms,fit_rms_percent,note`).
- A missing value is an empty cell, so `fit_rms` and `fit_rms_percent` are empty when no model is
  valid. For an invalid result, `note` holds the error text.

**Export > Graph image...** exports the Trend with the setpoint markers and the model curve. With
**Include cursor readout** ticked, the box at the top left of the image holds the analysis summary: the
times of C1 and C2, the K, L, T and fit error of the PV, and the setpoint times of the other tags.

### Saving

The Analysis settings are saved in the project: the PV and CO choices, the ranges, the method, the
setpoints, the reference, the model and tuning options, the plot labels, the model curve and the guide
lines. The cursors are not saved in a project. A session keeps them (see [Sessions](#sessions)).

### Try it with the test server

1. Start the test server in a console: `build\msvc-debug\bin\modbus_test_server.exe --port 1502`.
2. Start the application with `examples\demo.mbproj` (or open it with File > Open), then click **Connect**.
3. Double-click the **Value** cell of **Setpoint** (HR 100), type 0 and confirm. Or click **A: 0** in
   the Quick write dock. Wait a few seconds.
4. Open the **Analysis** dock (View > Analysis). In **1. Signals**, choose **Lag** as the PV and
   **Setpoint** as the CO.
5. Place C1 now, before the step: click **Place cursors** on the Trend, then click once in the plot
   before the step.
6. Write 1000 to **Setpoint** the same way, or click **B: 1000** in the Quick write dock, and wait about
   10 s. The **Lag** signal (IR 10) starts to rise after its 1 s dead time, with a time constant of 2 s.
   When it has settled, place C2 the same way, or click **Find step** in **2. Cursors** and check the
   cursors on the Trend.
7. The **Lag** results show K ≈ 1, L ≈ 1 s and T ≈ 2 s. The fit error should be Good, the model curve
   should follow the measured curve, and the checks list should be empty.

For a more realistic process, with a dead time, noise and a tank that is not linear, use the
[Loop simulator](LOOP_SIMULATOR.md) and the project `examples\loop101.mbproj`.

## Projects

Projects are saved as `.mbproj` files. A project holds the connection settings, the register numbering
(1-based or not), the tag list, including tag colours, the Y axis and the quick-write presets, and the Analysis settings. Recorded data, cursors and the trend window are not saved in a project. To keep those, save a [session](#sessions).

In the file, each tag has an `axis` key (`"left"` or `"right"`) and optional `quickA` and `quickB`
keys (the quick-write presets) and `writeMin` and `writeMax` keys (the write limits; a key is written
only when the limit is set). The Analysis settings are in an `analysis` object: `pvTag` and
`setpointTags` (the PV and the tags whose setpoints are shown), `deltaCoTag` and `deltaCo`, `method`
(`"0-63.2"`, `"smith"`, `"sundaresan"` or `"custom"`), `modelPercents`, `percents`, `reference`
(`"cursor"` or `"mean"`), `referenceWindowS`, `smoothingS`, `pvRange`, `coRange`, `showModel`,
`guideLines`, `detailedLabels`, `tuningRule` (`"zn"`, `"cohen-coon"`, `"imc"` or `"simc"`), `lambdaS`
and `timeUnit` (`"s"` or `"min"`). A top-level `hiddenColumns` array lists the tag-table columns hidden in this project (for example `"raw"`, `"gain"`, `"quickA"`). All of these are optional: a file without them loads with the defaults (the compact view for `hiddenColumns`).

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
- the cursors (C1 and C2) of the Trend.

| Menu item | Action |
|---|---|
| File > Save session... | Save the project, the recording and the cursors to a `.mbsession` file. `.mbsession` is added when missing. |
| File > Open session... | Open a session. Connection is closed first, if it is open. |

### Offline viewing and analysis

After **Open session...** the Trend, the Cursors panel and the Analysis panel show the recorded data
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

**Register numbers depend on the manual's revision.** The registers themselves are the same, but
ABB changed how the tables number them:

- Rev. A prints zero-based addresses: the input register table starts at `0000` (Output 1), the
  coils start at `0`, and the Loop 1 process variable is `5008`.
- Later revisions (for example Rev. I) print 1-based numbers: the same table starts at `0001`, the
  coils start at `1`, and the Loop 1 process variable is `5009`.

Look at the first row of your manual's input register table. If it is `0001`, tick **Options > Use
1-based register numbers** and type the numbers as printed. If it is `0000`, leave the option off
and type them as printed. Either way the app sends address 5008 for the Loop 1 process variable.
If a value still looks wrong, compare it with the controller's display: a register off by one
gives a nonsense value (or "Illegal data address").

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

**Example tags** (1-based numbers, as in Rev. I, with the 1-based option on; subtract 1 for a
Rev. A manual):

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
