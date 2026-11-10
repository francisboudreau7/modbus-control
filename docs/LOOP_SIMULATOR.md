# Level loop 101 simulator

Read online: [https://francisboudreau7.github.io/modbus-control/loop-simulator/](https://francisboudreau7.github.io/modbus-control/loop-simulator/)

En français : [Simulateur de boucle de niveau](fr/SIMULATEUR_DE_BOUCLE.md).

`modbus_loop_sim.exe` simulates a small level control loop, for classes and for trying the
step-response analysis of Modbus Control. A tank is filled by a pump through a control valve and
drains through a hand valve. The level transmitter and the regulator are a Modbus TCP server, so
Modbus Control reads and writes them like a real controller. The program has a window with the
P&ID, the regulator faceplate and a few controls.

The regulator starts in **Manual** (MAN). Then the valve command (OUT) changes only when a Modbus client
writes it or when you change it on the faceplate. The loop is open, so you can do step tests and measure
the process. Switch the regulator to **Auto** and it becomes a PID controller that holds the level at
SP: you then enter the tuning you calculated from the step test and see the closed loop work.

Contents: [Starting it](#starting-it), [The P&ID](#the-pid), [The physics](#the-physics),
[Modbus registers](#modbus-registers), [The window](#the-window), [The PID controller](#the-pid-controller),
[Scenario files](#scenario-files), [Exercise: step test](#exercise-step-test-with-modbus-control),
[Exercise: closed loop](#exercise-closed-loop).

## Starting it

From the Start menu, choose **Modbus Loop Simulator**. Or start it from a console:

```bat
modbus_loop_sim.exe --speed 2
```

The programs are in the `bin` folder of the installation folder. For a per-user installation (the
default) this is `%LOCALAPPDATA%\Programs\Modbus Control`; for an all-users installation it is
`C:\Program Files\Modbus Control`. Example files such as the scenario below are in its `examples`
folder, so with `--scenario` start from the installation folder:

```bat
cd "%LOCALAPPDATA%\Programs\Modbus Control"
bin\modbus_loop_sim.exe --scenario examples\loop101-step.mbloop
```

(For an all-users installation, use `cd "C:\Program Files\Modbus Control"`.)

The window follows the interface language of Modbus Control (**Options > Language**, see the
[User Guide](USER_GUIDE.md#language)) when it starts: French if that setting is French, or is System
language on a French Windows, otherwise English. The command-line help and the headless output stay in
English.

The window version is a Windows GUI program: started from `cmd` or PowerShell it returns to the prompt
at once, and Ctrl+C or closing the console does not stop it (close its window instead). Its exit
code is therefore not available to the shell. To wait for it and read the exit code, use
`start /wait bin\modbus_loop_sim.exe --headless` in `cmd` or `Start-Process -Wait -NoNewWindow -PassThru`
in PowerShell (then read `ExitCode` of the returned object). `--help`, `--version`, command-line
errors and `--headless` do write to the console.

The server listens on `127.0.0.1:1601` by default, so only this PC can connect. The status bar of the
window shows the address and the number of clients.

| Option | Meaning |
|---|---|
| `-h`, `--help` | Show the help. |
| `--version` | Show the version and the libmodbus version. |
| `--port <port>` | TCP port. Default 1601. `0` lets the OS choose. |
| `--host <address>` | Address to listen on. Default `127.0.0.1`. Use `0.0.0.0` to accept clients from the network. The server has no authentication. |
| `--scenario <file>` | Load a scenario file (`.mbloop`) at start (see [Scenario files](#scenario-files)). |
| `--speed <factor>` | Simulation speed: 1, 2 or 5 (see **Speed** below). Default 1. |
| `--headless` | No window. Runs the loop and the Modbus server only, and prints `Listening on <host>:<port>`. Stop it with Ctrl+C. |

Exit codes: 0 for a normal stop, 1 when the server cannot start (for example the port is in use,
only in `--headless`), 2 for an invalid command line or scenario file. Without `--headless`, a start
error is shown in the status bar and the window stays open.

### From Modbus Control

To start a simulation without the command line, choose **Simulation > Load simulation...** in Modbus
Control, select "Level loop LIC-101, step test" and press **Load**. Modbus Control starts the simulator
with that scenario, loads the matching tags and connects by itself, so you can go straight to the
exercises below. Closing Modbus Control also closes the simulator it opened. See
[Loading a simulation](USER_GUIDE.md#loading-a-simulation) for the details.

- If a simulator that Modbus Control opened is still running, Modbus Control asks whether to restart it
  with the chosen simulation.
- If another program already listens on the simulator's port, close it first. Modbus Control then
  changes nothing.
- A simulator you started yourself is never closed by Modbus Control.
- Your own `.mbloop` files are not in the list. Open them from the File menu or with `--scenario`.

## The P&ID

The drawing follows the ISA symbols (ISA-5.1). Process lines are solid. The dashed lines are
electric signals.

```
 TK-102 --> P-101 --> FCV-101 --> (pipe) --> TK-101 --> HV-102 --> TK-102
                         ^                      |  \--- overflow (99 %) ---> TK-102
                         |                      |
                      LIC-101 <---------------- LT-101
```

| Tag | What it is | ISA symbol in the window |
|---|---|---|
| TK-102 | Base tank. It is very large, so its level does not change. It supplies the pump and receives the water from HV-102 and the overflow. | Wide tank, bottom left. |
| P-101 | Centrifugal pump. Its speed is set in the window (default 100 %). The pressure is 1.00 bar at 100 % and falls with the square of the speed. | Circle with a triangle pointing downstream. |
| FCV-101 | Flow control valve. **Fail closed** (air to open): it closes when the signal or the air is lost. Linear: the flow is proportional to the stem position. Full stroke takes 5 s. It is moved by the regulator output OUT (4 to 20 mA = 0 to 100 %). | Globe valve (two triangles tip to tip) with a diaphragm actuator and "FC" under it. |
| Pipe | The line from FCV-101 to TK-101. It adds a pure dead time of 2 s. | Solid line with arrows. |
| TK-101 | Process tank, diameter 0.30 m, height 1 m. The level is the controlled variable. | Tall cylinder with graduations 0, 25, 50, 75 and 100 %. |
| HV-102 | Hand valve at the bottom outlet of TK-101. It is the load: you change it by hand (default 50 % open). | Hand valve (two triangles) with a T handle. |
| Overflow | A pipe at 99 % of the height that returns the excess water to TK-102. The level cannot go above 99 %. | Line from the top of TK-101 to TK-102, drawn in colour while water flows. |
| LT-101 | Level transmitter: 0 to 1 m = 0 to 100 % = 4 to 20 mA. It has 2 s damping and a noise of ±0.05 %. Its output is the regulator input (PV). | Circle (field instrument). |
| LIC-101 | Level indicating controller. It is the Modbus server. It shows PV, SP and OUT. | Circle with a horizontal line (panel instrument). |

## The physics

In plain words: the pump pushes water through FCV-101. The more the valve is open, the more water
reaches TK-101, after a delay. Water leaves through HV-102. The more water in the tank, the higher
the pressure at the bottom, and the faster it drains. So the level rises until the outflow equals the
inflow, and then it stays there. This is a self-regulating process, but not a linear one: the
outflow depends on the square root of the level.

With the pump speed s, the FCV stem position y, the HV-102 opening v (all as fractions from 0 to 1) and
the level h (in m):

| Quantity | Equation |
|---|---|
| Pump pressure | p = 1 bar × s² |
| Flow through FCV-101 | Qfcv = 5.0×10⁻³ m³/s × y × s (300 L/min at full opening and full speed) |
| Stem movement | moves toward OUT at most 100 % per 5 s (full stroke time) |
| Flow into TK-101 | Qin(t) = Qfcv(t − 2 s) |
| Flow out through HV-102 | Qhv = 7.0711×10⁻³ m^2.5/s × v × √h |
| Level | A × dh/dt = Qin − Qhv, with A = π × 0.15² = 0.070686 m² |
| Overflow | when h would exceed 0.99 m, h stays at 0.99 m and the excess leaves through the overflow |
| LT-101 | The sensor reads 100 × h / 1 m plus a random value between −0.05 and +0.05 % (a new value every 0.1 s of simulation time); the transmitter damping, a first-order lag of 2 s, filters that reading, noise included |

The simulation runs in steps of 10 ms of simulation time.

**Settled level.** When Qin = Qhv, the level stops:

h = ( 5.0×10⁻³ × y × s / ( 7.0711×10⁻³ × v ) )²

With the pump at 100 % and HV-102 at 50 %, the stem y equals OUT:

| OUT | Inflow | Settled level (LT-101) |
|---|---|---|
| 30 % | 90 L/min | 18 % |
| 50 % | 150 L/min | 50 % |
| 60 % | 180 L/min | 72 % |
| 70 % | 210 L/min | 98 % |
| above about 70 % | | 99 % and water leaves through the overflow |

**Gain and time constant.** Near a level h, the process behaves like a first-order lag. Its gain is
the slope of the table above: about 1.6 %/% between 30 and 50 %, 2.2 %/% between 50 and 60 %, and 2.4 %/%
between 50 and 70 %. Its time constant is

T = 2 × A × h / Qhv

This is about 28 s at a level of 50 % (0.50 m and 150 L/min). It is about 17 s at a level of 18 % and about
34 s at 72 %. The flows are large for such a small tank on purpose: a step settles in about 2.5 minutes,
in real time. The higher the level, the slower the loop.

## Modbus registers

The server answers any unit id. Values are `float32` in `ABCD` order (high word first). The
register numbers below are 1-based, as in the manual of an ABB CM50 and as the example project
shows them (**Address base 1**). The protocol addresses in the frames are one less.

| Value | Area | Register (1-based) | Protocol address | Range |
|---|---|---|---|---|
| LT-101 PV (level) | Input register (FC04) | 1-2 | 0-1 | read only, 0 to 100 % |
| LIC-101 SP | Holding register (FC03, FC16) | 1-2 | 0-1 | 0 to 100 % |
| LIC-101 OUT | Holding register (FC03, FC16) | 3-4 | 2-3 | 0 to 100 % |
| LIC-101 A/M | Coil (FC01, FC05) | 1 | 0 | 0 = Auto, 1 = Manual |

Rules, like the CM50:

- Read a float with one request of 2 registers. Reading holding registers 1-4 in one request also works.
- A write of SP or OUT must be a FC16 request on a pair (start at 1 or 3, an even count). A value
  outside 0 to 100, or a value that is not a number, is refused with exception 3 (illegal data value).
- Anything else is refused with exception 2 (illegal data address): FC06, a request that starts in the
  middle of a float, a count that splits a float, any other address, and the discrete inputs (FC02).
  For the coil see below.
- SP is stored. In Manual nothing uses it: it is only shown on the faceplate and on the schematic. In
  Auto it is the setpoint of the PID.
- While LIC-101 is in **Auto**, a write that includes OUT (registers 3-4) is refused with exception 3
  (illegal data value) and nothing is written, not even the SP of the same request. Writing SP alone
  (registers 1-2) is accepted. Reading is not affected: OUT is computed by the PID and you read it
  as before.

In Manual, a written OUT takes effect at once on FCV-101 (the stem then moves at the stroke speed).

**The A/M coil.** Coil 1 (protocol address 0) is the Auto/Manual mode of LIC-101, as on the CM50:
`0` (OFF) is Auto and `1` (ON) is Manual. The simulator starts in Manual, so it reads `1` at first.

- FC01 reads it. The request must be exactly coil 1 with a count of 1; any other start or count is
  refused with exception 2 (illegal data address).
- FC05 writes it: `0xFF00` (ON) switches to Manual and `0x0000` (OFF) to Auto. Any other value is
  refused with exception 3 (illegal data value), and any other coil with exception 2. FC15 with one
  coil at coil 1 is accepted too.
- The switch is the same as the A/M button of the faceplate, and it is bumpless: going to Auto starts
  the PID from the present OUT, going to Manual keeps the present OUT (see
  [The PID controller](#the-pid-controller)). Writing the mode already in force changes nothing.
- A coil write is always accepted, also in Auto (unlike a write of OUT). The mode shown on the faceplate
  follows it at once, and the coil reads the mode the faceplate shows.

`examples/loop101.mbproj` has this coil as the tag **LIC-101 A/M** (writable, not plotted). Connect, double-click its **Value** cell and choose `ON` (Manual) or `OFF` (Auto).

## The window

- **Schematic** (centre): the P&ID. The water level in TK-101 is drawn at the true level. The labels show
  the values: LT-101 in % and mA, FCV-101 OUT in % and mA and the stem position, HV-102 opening, pump
  speed, and PV, SP and OUT on LIC-101. The overflow line is coloured while water flows.
- **Show process data** (check box in the bottom bar): adds the level in m, the flows Qin, Qhv and
  overflow in L/min, the pump pressure in bar and the dead time.
- **LIC-101 faceplate** (right): like a panel controller. It has a vertical PV bar with the SP mark,
  and the values PV, SP and OUT with the mA of PV and OUT. The **▲** and **▼** buttons change OUT by
  1 % (5 % with Shift held). Type a value in the OUT or SP box and press Enter. Typed values outside 0 to
  100 are clamped. A client that writes by Modbus changes the same values. The mode button and the
  tuning fields are described in [The PID controller](#the-pid-controller).
- **Modbus panel** (under the schematic; tick **Show Modbus registers** in the bottom bar to show it, hidden by default): the server address and port actually in use
  (or the start error in red), the number of connected clients, and a table of the four values: PV
  (input registers 1-2, read only), SP (holding registers 1-2), OUT (holding registers 3-4) and the A/M
  coil (coil 1), with the 1-based number shown by Modbus Control, the 0-based address, the type, the
  function codes, the access and the current value. The Access of OUT reads "Read only (AUTO)" in Auto. **Help > Modbus
  registers...** shows the same table in a dialog.
- **Sliders** (bottom bar): **HV-102** opening (0 to 100 %) and **P-101 speed** (0 to 100 %). Moving
  HV-102 is the load disturbance of the loop.
- **Speed**: ×1, ×2 or ×5. ×1 is real time: the times that Modbus Control shows are the process
  times. At ×2 or ×5 the process runs faster than the clock: multiply the times of Modbus Control by the
  speed to get process times. The time **t** under the schematic is process time.
- **Pause** / **Run**: stops the simulation. The Modbus server still answers, with frozen values, and
  writes still change SP and OUT.
- **t = ...**: the simulation time since the start or the last reset.
- **File** menu: **Open scenario...**, **Save scenario...**, **Reset** (back to the steady state at
  OUT = SP = 50 %) and **Quit**. **Help > About** shows the version.
- **Status bar**: the server address and the number of connected clients.

## The PID controller

The mode button of the faceplate shows **MAN** (amber) or **AUTO** (green). Click it to switch. The
program starts in Manual.

- **Manual**: OUT is yours, as in the step test. The tuning fields can be edited but are not used.
- **Auto**: the controller computes OUT every 0.1 s of simulation time from SP and PV. The OUT
  buttons and the OUT box are disabled, and Modbus writes to OUT are refused (exception 3). You can
  still change SP, on the faceplate or by Modbus.
- The mode can also be switched by Modbus, with the A/M coil (see [Modbus registers](#modbus-registers)).
- Going from MAN to AUTO is **bumpless**: OUT does not jump, because the controller starts from the
  present OUT. This holds with Ti = 0 too: the controller then keeps a constant bias (set at the
  transfer, like a manual reset) so that OUT = bias + Kp × e. Going back to MAN keeps the present OUT.

The **Tuning** fields (type a value and press Enter; values are clamped to the range):

| Field | Meaning | Range | Default |
|---|---|---|---|
| Kp | Proportional gain, in % of OUT per % of error | 0 to 100 | 1 |
| Ti | Integral time, in seconds. 0 means no integral action. | 0 to 3600 | 30 |
| Td | Derivative time, in seconds. 0 means no derivative action. | 0 to 600 | 0 |
| Action | **Reverse** or **Direct** (see below) | | Reverse |

The controller is the ideal (ISA) form, the same one as the tuning calculator of the Analysis panel:

u = Kp × (e + 1/Ti × ∫e dt + Td × de/dt)

- The error e is SP − PV for **Reverse** action: when PV rises, OUT falls. This is the right action
  for this loop: more water in the tank must close the valve. **Direct** action uses e = PV − SP. With
  it the loop has positive feedback and OUT runs away to 0 or 100 %.
- PV is the transmitter output, noise included. The derivative acts on the PV, not on the error, and
  the PV goes through a first-order filter of Td / 10, so a change of SP does not kick OUT and the noise
  is limited.
- Anti-windup: the integral is frozen while OUT is at 0 or 100 % and the error would push it further.
- OUT is limited to 0 to 100 %. The FCV-101 stem still follows OUT at the stroke speed.

## Scenario files

A scenario (`.mbloop`) is a JSON file with the settings, and optionally the state to start from. All
keys are optional. **Save scenario...** writes the current settings and state. Open one from **File >
Open scenario...** or with `--scenario`. An invalid file is refused with a message that names the
key.

| Key (in `settings`) | Range | Default |
|---|---|---|
| `pumpSpeedPct` | 0 to 100 | 100 |
| `hvOpeningPct` | 0 to 100 | 50 |
| `deadTimeS` | 0 to 30 | 2 |
| `ltNoisePct` | 0 to 5 | 0.2 |
| `ltDampingS` | 0 to 60 | 1 |
| `fcvStrokeS` | 0 to 120 | 5 |
| `speedFactor` | 1, 2 or 5 | 1 |

The optional `pid` object holds the controller: `mode` (`"manual"` or `"auto"`), `kp` (0 to 100), `tiS`
(0 to 3600), `tdS` (0 to 600) and `action` (`"reverse"` or `"direct"`). The defaults are those of the
table in [The PID controller](#the-pid-controller). **Save scenario...** always writes it. With
`"mode": "auto"` the loop starts in Auto, entered bumplessly after the `state` is applied. A scenario
without `pid` (saved by version 1.3.0) loads with the default PID settings, in Manual.

The optional `state` object has `levelPct` (0 to 99), `outPct`, `spPct` and `stemPct` (0 to 100). Without it,
the loop starts at the steady state for OUT = 50 %.

[examples/loop101-step.mbloop](../examples/loop101-step.mbloop) has the default settings at speed ×1, for
the step test below. A teacher can save other scenarios: a longer dead time, more noise, a slower
valve, a smaller pump.

## Exercise: step test with Modbus Control

You measure the process at OUT = 50 %: the K, L and T of a first-order model with dead time. Then you
compare them with the theory. See [Analysis](USER_GUIDE.md#analysis-step-response) in the User Guide
for the panel.

1. Start the simulator with `bin\modbus_loop_sim.exe --scenario examples\loop101-step.mbloop`, run
   from the installation folder (see [Starting it](#starting-it))
   (or start it from the Start menu: the default speed ×1 is right). The level is steady at 50 %.
2. Start Modbus Control. It starts with an empty project, so choose **File > Open example...** and open
   [examples/loop101.mbproj](../examples/loop101.mbproj). The project
   connects to `127.0.0.1:1601`, unit 1, with four tags: **LT-101 PV** (input register 1), **LIC-101 SP**
   (holding register 1), **LIC-101 OUT** (holding register 3) and **LIC-101 A/M** (coil 1). Click
   **Connect**.
3. Wait 10 s. **LT-101 PV** is about 50 % and flat. The Trend shows PV and OUT.
4. In the **Quick write** dock, click **B: 60** to step OUT from 50 to 60 % (**A: 50** goes back).
   The write confirmation appears if it is turned on.
5. Wait until PV is steady, about 2.5 minutes (five time constants). The PV rises to about 72 %.
6. Open the **Analysis** dock. The project already has **LT-101 PV** as the PV and **LIC-101 OUT** as the
   CO. Click **Find step**, or place C1 just before the step and C2 after the PV has settled with
   **Place cursors**. Check the cursors on the Trend.
7. Read K, L and T. Hover over them to see the formulas.

**What to expect.**

- **K** (about 2.2 %/%): ΔPV/ΔCO = (72 − 50) / (60 − 50). It matches the table of settled levels.
- **L**: the dead time of 2 s, plus the delays of the FCV-101 stroke (a 10 % move takes 0.5 s) and of the
  LT-101 damping (2 s). With the default method, Smith, L is about 4 s. The project polls every 100 ms,
  so the Trend resolves it well at ×1.
- **T**: about 32 to 33 s with Smith. The theory says 2 × A × h / Qhv = 28 s at 50 % and 34 s at 72 %,
  and the step covers both. A model fitted on the 63.2 % point gives a value between them.

**Try the other method.** In the **Model** group, choose **0 % / 63.2 % (two points)**. L becomes much
shorter (between about 0.3 and 1.2 s) and it changes when you move C1: it is 1.2 s with C1 10 s before the
step and 0.25 s with C1 0.5 s before it. T is about 35 s. With the Ziegler-Nichols rule, a check appears
in the list. With a 0 % lower point, L is the moment the PV first leaves its starting value. The small
lags after the dead time (the LT-101 damping, the valve stroke) bend the start of the curve but do not
delay that first movement, so they go into T and L comes out too short. Smith uses two points on the
rising part of the curve and is not affected.

**Why does a bigger step give a larger T?** The outflow follows √h, so the tank is slower at high levels.
Step from 50 to 70 % and T is larger than for 50 to 60 %, and K is larger too (2.4 against 2.2 %/%).
Step from 50 to 30 % and both are smaller. A linear process would give the same K and T each time.
When you tune a PID from these numbers, tune for the operating point you will use.

**Load disturbance.** Set OUT back to 50 % and wait for the steady state. Then move the **HV-102** slider from 50 to 60 %.
OUT does not change, and the level falls to about 35 %, because the outlet is larger. The loop has no
regulator action, so the level does not come back. In the Analysis panel, with the CO set to **Typed
ΔCO**, you can measure the response to a disturbance, but there is no ΔCO to give a K.

**Overflow.** Write 80 to **LIC-101 OUT**. The level rises to 99 % and stays there. The overflow line turns
colour, and with **Show process data** you see the overflow flow in L/min. PV stays at 99 % whatever
OUT, so the process gain is zero there. A step test that goes into the overflow gives a wrong K and T.
Keep OUT at 70 % or below with the default settings.

**Other things to try.** Change the dead time, the noise or the stroke time in a scenario file and
repeat the test. Lower the pump speed to 70 %: the same OUT then gives a lower level. Close HV-102 (0 %)
and the tank fills up to the overflow.

## Exercise: closed loop

You put the controller in Auto with the settings that the step test gave, and compare tuning rules.
Do the [step test](#exercise-step-test-with-modbus-control) first, so that K, L and T are in the
Open loop area of the Analysis panel.

**1. Read the tuning.** In the **Open loop** area of the **Analysis** dock, the Tuning group turns K, L and T into settings. With
the numbers of the step test with the Smith method (K 2.2 %/%, L about 4 s, T about 32 s) you get
roughly the values below, with the overshoot (as a percentage of the 10 % step) and the settling time
measured for SP 50 to 60 %. Yours will differ a little. The PV and the OUT of this loop are both in %,
so the Kp of the table (in % per %) can be typed as it is.

| Rule | Controller | Kp (about) | Ti (about) | Overshoot | Settles in |
|---|---|---|---|---|---|
| IMC / lambda, λ = Auto (= L) | PI | 1.9 | 32 s | about 5 % | about 21 s |
| SIMC, λ = Auto (= L) | PI | 1.9 | 32 s | about 6 % | about 22 s |
| Ziegler-Nichols | PI | 3.4 | 13 s | about 60 % | about 42 s |
| Cohen-Coon | PI | 3.4 | 10.5 s | about 73 % | about 52 s |

Do not take these values from a 0 % / 63.2 % model. Its L is too short, and Ziegler-Nichols then gives a
Kp of 12 or more. The limit of this loop with P only is about Kp 8.7, so the loop does not settle: OUT
swings between 0 and 100 %.

**2. Start in Auto.** Set OUT to 50 % and wait for the steady state (PV and SP are both 50 %). On the
faceplate, type Kp and Ti (Td stays 0, Action Reverse), then click **MAN**. It becomes **AUTO**, and
OUT does not move.

**3. Step the setpoint and measure it.** In Modbus Control, open the Trend and set its left axis to 0 to
100: right-click the left Y axis and choose **0 to 100** (see [Y-axis ranges](USER_GUIDE.md#y-axis-ranges)
in the User Guide). The Trend then does not rescale while the loop moves. Plot PV and OUT on that axis.
Then write 60 to **LIC-101 SP**.

With the default values (Kp 1, Ti 30 s) PV rises to 60 % with no overshoot and stays within 0.5 % of
SP after about 30 s. OUT first jumps by Kp × 10 % = 10 %, to 60 %, then falls back and settles at about
55 %, the OUT that holds a level of 60 % with HV-102 at 50 %.

Now measure it in the **Analysis** dock:

- Click **Closed loop**, below the Controller group. In **1. Signals**, choose **LIC-101 SP** as the
  **Setpoint (SP)**. The project has already chosen the PV and the CO (**LIC-101 OUT**).
- Place C1 just before the setpoint change and C2 after PV has settled, with **Place cursors** on the
  Trend. **Find step** is made for open-loop tests, so place the cursors yourself.
- In **3. Test**, choose **Setpoint step** and a **2 %** settling band.
- In **4. Performance**, read **Overshoot**, **Settling time**, **Decay ratio** and **IAE**. With
  Kp 1 and Ti 30 s there is no overshoot, and the decay ratio shows "—", because the response does not
  oscillate. Hover over a value to see how it is defined. **Output max** and **Output travel** tell
  you how hard OUT worked.

**4. Compare the rules.** Return SP to 50 %, then try each pair of values in turn: type Kp and Ti on the
faceplate and write SP to 60 % again. Place the cursors around each step and read the same four numbers
in the Performance table. The settling time depends on the band, so keep the same band for all the
tests. The Ziegler-Nichols PI is the most aggressive: its Kp is larger and its Ti shorter. PV overshoots
by more than half of the step (it peaks near 66 %) and oscillates before it settles, so its decay ratio
is large and its IAE is the worst. Ziegler-Nichols aims at a quarter-amplitude decay and at rejecting
disturbances, not at clean setpoint changes. IMC and SIMC with λ = L overshoot by a few percent. A
larger λ is slower and gentler.

**Predict before you try.** The model of the step test lets the panel predict these responses. In
**5. Prediction**, tick **Predict from the open-loop model**. The **Model** line shows K, L and T
(about 2.2 %/%, 4.0 s and 32.5 s): the panel took them from the step test and keeps them, so you can
move the cursors to the setpoint step. Type the Kp and Ti of the tuning, or choose the rule in the
Tuning group of the **Open loop** area and use **Take tuning** and **Use**. The **Predicted** column
appears beside **Measured**, and the predicted PV and OUT are drawn as dashed curves on the Trend. With
the model K 2.2 %/%, L 4.0 s and T 32.5 s the panel predicts, for the 10 % step and a 2 % band:

| Rule | Controller | Predicted overshoot | Predicted settling time |
|---|---|---|---|
| IMC / lambda, λ = Auto (= L) | PI | about 4.7 % | about 24.5 s |
| Ziegler-Nichols | PI | about 70 % | about 59 s |

Compare them with the measured values of the table above (about 5 % and 21 s for IMC, about 60 % and
42 s for Ziegler-Nichols). The prediction is close for IMC and too pessimistic for Ziegler-Nichols. The
model is a first-order lag with a dead time: it ignores the tank's nonlinearity (T is not the same at 50
and at 60 %) and the small lags of the valve and of the LT-101 damping. The more aggressive the tuning,
the more those details matter, so the prediction and the real loop agree less.

**5. Load disturbance.** With the loop in Auto and PV steady at SP (60 %), move the **HV-102**
slider from 50 to 60 %. The outlet is larger, so PV falls at first. The controller opens the valve,
and the integral action brings PV back to SP: OUT ends at about 66 %, against about 55 % before.
Measure it with the **Disturbance** test: in **3. Test** of the Closed loop area, choose
**Disturbance**, and place C1 just before you move the slider and C2 after PV is back at SP. The table
now shows the **Max deviation** (the dip of PV), the **Time of max deviation** and the **Settling
time** (the band is a percentage of the maximum deviation), with the IAE and the output rows. Compare
the dip in PV and the movement of OUT for different tunings. To predict the dip, tick **Predict from
the open-loop model** and type about −11 in **Load step (CO units)**: a bigger outlet acts like 11 %
less OUT, which is what the controller has to give back at the end.

Now type Ti = 0 (P-only) and repeat from a steady state. PV no longer returns to SP: an **offset**
remains, because a P controller needs an error to give a different OUT. A larger Kp makes the offset
smaller. The offset is (OUT at the new steady state - the bias) / Kp, where the bias is the OUT at the moment
you switched to AUTO. Switching to AUTO with Ti = 0 does not make OUT jump, as explained in
[The PID controller](#the-pid-controller).

**6. The wrong action.** In Auto with Ti = 30 s, set **Action** to **Direct**. The controller now
opens the valve when PV is above SP, which adds water. PV moves away from SP and OUT runs away to 100 %
(PV above SP) or to 0 % (PV below SP) and stays there. Set the Action back to Reverse and click
**AUTO** to return to **MAN**, then set OUT by hand if the level has gone to the overflow or to empty.

**Derivative and noise.** Derivative action reacts to how fast PV changes, and noise changes fast, so
D amplifies it: OUT jitters even when the level is steady, and the valve works for nothing. The
transmitter damping (2 s by default) smooths the noise before the regulator sees it. Set
`ltDampingS` to 0 in a scenario file and add Td: OUT chatters by several %. With the damping back, it
calms down, but the damping also adds a lag to the loop. This is why most level loops are run as PI
(Td = 0).

**Other things to try.** Add Td (for example Td = L / 2) and look at the noise on OUT. Make the dead
time longer in a scenario file and see how the same tuning behaves. Try a setpoint of 80 %: at that
level the process is slower and its gain is higher, so a tuning made at 50 % is less stable.
