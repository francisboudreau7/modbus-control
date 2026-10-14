# Modbus Control — Quick Start

This guide walks through the basic workflow with an ABB ControlMaster CM50 controller:

1. [Connect to the controller](#1-connect-to-the-controller)
2. [Add a tag](#2-add-a-tag)
3. [Put cursors on the trend](#3-put-cursors-on-the-trend)
4. [Show setpoints on a curve](#4-show-setpoints-on-a-curve)

Each step takes a few minutes. The full [User Guide](USER_GUIDE.md) explains every option.
Version française : [Guide de démarrage rapide](fr/GUIDE_DEMARRAGE_RAPIDE.md).

**Before you start**

- The CM50 needs its Ethernet option, and this PC must be on the same network.
- Note the controller's IP address and its Modbus **Device Address** (1..247).
- The CM50 accepts at most 3 Modbus connections at the same time. Close other Modbus programs if
  the connection fails.
- Don't write anything to a controller that runs a real process unless you are allowed to.

## 1. Connect to the controller

1. Start **Modbus Control**.
2. Check how your CM50 manual (Communications Supplement IM/CM/C-EN) numbers its registers: look at
   the first row of the input register table.
   - `0001` (Rev. I and other recent revisions): in the **Options** menu, tick **Use 1-based
     register numbers**. This guide uses these numbers.
   - `0000` (Rev. A): leave the option off, and use each number of this guide minus 1 (for example
     `5008` instead of `5009`).
3. In the connection bar at the top, fill in:

   | Field | Value |
   |---|---|
   | **Host** | the controller's IP address, for example `192.168.1.50` |
   | **Port** | `502` (fixed on the CM50) |
   | **Unit** | the controller's Device Address, usually `1` |
   | **Timeout** | `1000` ms |
   | **Period** | `500` ms (one reading every half second) |

4. Click **Connect**. The status LED turns green and the status bar says "Connected to ...".

If it stays yellow or turns orange, check the cable, the IP address, the port and the unit. The controller
may also restrict clients by IP address: ask for this PC's address to be allowed.

**Shortcut:** **File > Open** `examples\cm50.mbproj` (in the installation folder) loads the
connection and the tags of this guide. Change the host to your controller's address.

## 2. Add a tag

A tag is one value read from the controller. Start with the Loop 1 process variable (PV).

1. In the **Tags** panel at the bottom, click **Add**. The **Add tag** dialog opens.
2. **Name**: `Loop 1 Process Variable`.
3. **Area**: **Input register**.
4. **Address**: `5009` (`5008` with a Rev. A manual, see step 1).
5. Wait a moment: while connected, the dialog reads the register and shows its raw value and what it
   means as a 16-bit value and as float32.
6. Click **Detect encoding**. The CM50 stores this value as a float32 on two registers, so the
   **Detect encoding** dialog opens with its best answer selected (normally `float32 ABCD`).
   - To be sure, type the value shown on the controller's display in **Value shown on the device
     (optional)**.
     The matching encoding gets a check mark in the **Match** column.
   - Click **Apply**. The type and the byte order are filled in for you.
7. **Unit**: `°C` (or the unit of your loop).
8. Click **Add**. The new row appears in the table and its curve appears on the trend.

Add the other two tags of the loop the same way:

| Name | Area | Address | Unit | Notes |
|---|---|---|---|---|
| Loop 1 Control Output | Input register | `5013` | `%` | Read-only. |
| Loop 1 Local Setpoint 1 | Holding register | `6639` | `°C` | Tick **Writable** only if you are allowed to change the setpoint. |

Compare each value in the **Value** column with the controller's display. If a value looks wrong,
right-click the row and choose **Detect encoding...**.

**Before writing to a writable tag**, set its **Min** and **Max** columns to the range the
controller allows. A write outside them is refused.

## 3. Put cursors on the trend

The trend shows every plotted tag as a curve. Two cursors, **C1** (red) and **C2** (blue), measure
times and values on it.

1. Make something happen: double-click the **Value** cell of **Loop 1 Local Setpoint 1**, type a
   new setpoint (a small change, for example 2 °C above the current one) and confirm. Wait until
   the process value has settled.
2. Above the trend, set **Window** to **5 min** so the whole change is visible. Use the mouse wheel
   to zoom in time; drag with the right button to pan.
3. Click **Place cursors**. Click on the trend just before the change: that is **C1**. Click again
   after the process value has settled: that is **C2**.
4. To move a cursor, drag its line with the left button.
5. Open the **Cursors** tab on the right. It shows the times `t1`, `t2`, `Δt`, and for every curve
   its value at C1 and C2 and the change `ΔY`.

Placing a cursor pauses the trend so the cursors stay put. Click **Run** to follow the newest data
again; **Clear cursors** removes both cursors.

## 4. Show setpoints on a curve

Setpoints are percentage levels of a step response: the 63.2 % setpoint is the time at which the
process value has covered 63.2 % of its change between C1 and C2. They are drawn as markers on the
curve.

1. Keep the two cursors around the change (step 3).
2. Open the **Analysis** tab on the right. In **1. Signals**:
   - **Process value (PV)**: choose **Loop 1 Process Variable**.
   - **Controller output (CO)**: choose **Loop 1 Control Output** (or the tag you changed).
3. Open the **Setpoints** tab (next to Analysis).
   - In **Show setpoints on**, the PV is already ticked. Tick other curves if you want their
     setpoints too.
   - **Setpoints (%)** lists the percentages, `0 %` and `63.2 %` by default. To add one, type it
     (for example `28.3`) and click **Add**.
4. On the trend, each setpoint appears as a marker on the curve, labelled `63.2 %`, with dashed
   guide lines to the time axis and the value axis. The **setpoint table** below gives each
   setpoint's target value and time, and its time after C1.
5. Tick **Show time and value on plot labels** to print the time and the value on each marker.

Hover over any value to see how it was calculated.

**Going further:** the **Analysis** tab also fits a first-order model (gain K, dead time L, time
constant T) and suggests PID settings. See [Analysis (step response)](USER_GUIDE.md#analysis-step-response)
in the User Guide. To keep your work, use **File > Save** (the project) or **File > Save session...**
(the project plus the recording and the cursors).
