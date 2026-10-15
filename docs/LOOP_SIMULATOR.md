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
| LT-101 | 100 × h / 1 m, through a first-order lag of 2 s, plus a random value between −0.05 and +0.05 %, a new value every 0.1 s of simulation time |

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

Rules, like the CM50:

- Read a float with one request of 2 registers. Reading holding registers 1-4 in one request also works.
- A write of SP or OUT must be a FC16 request on a pair (start at 1 or 3, an even count). A value
  outside 0 to 100, or a value that is not a number, is refused with exception 3 (illegal data value).
- Anything else is refused with exception 2 (illegal data address): FC06, a request that starts in the
  middle of a float, a count that splits a float, any other address, and the coils and discrete inputs.
- SP is stored. In Manual nothing uses it: it is only shown on the faceplate and on the schematic. In
  Auto it is the setpoint of the PID.
- While LIC-101 is in **Auto**, a write that includes OUT (registers 3-4) is refused with exception 3
  (illegal data value) and nothing is written, not even the SP of the same request. Writing SP alone
  (registers 1-2) is accepted. Reading is not affected: OUT is computed by the PID and you read it
  as before.

In Manual, a written OUT takes effect at once on FCV-101 (the stem then moves at the stroke speed).

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
- Going from MAN to AUTO is **bumpless**: OUT does not jump, because the controller starts from the
  present OUT. The exception is a controller with no integral action (Ti = 0): its OUT is
  Kp × e, so OUT jumps to that value when you press the button. Going back to MAN keeps the present OUT.

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
`"mode": "auto"` the loop starts in Auto, entered bumplessly after the `state` is applied.

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
2. Start Modbus Control and open [examples/loop101.mbproj](../examples/loop101.mbproj). The project
   connects to `127.0.0.1:1601`, unit 1, with three tags: **LT-101 PV** (input register 1), **LIC-101 SP**
   (holding register 1) and **LIC-101 OUT** (holding register 3). Click **Connect**.
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
  LT-101 damping (2 s). This is about 4 to 5 s. The project polls every 100 ms, so the Trend resolves it
  well at ×1.
- **T**: about 32 s. The theory says 2 × A × h / Qhv = 28 s at 50 % and 34 s at 72 %, and the step covers
  both.
  A model fitted on the 63.2 % point gives a value between them.

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
Analysis panel.

**1. Read the tuning.** In the **Analysis** dock, the Tuning group turns K, L and T into settings. With
the numbers of the step test (K about 2.2 %/%, L about 4 to 5 s, T about 28 to 34 s) you get roughly
the values below. Yours will differ a little. The PV and the OUT of this loop are both in %, so the
Kp of the table (in % per %) can be typed as it is.

| Rule | Controller | Kp (about) | Ti (about) |
|---|---|---|---|
| IMC / lambda, λ = Auto (= L) | PI | 1.6 | 31 s |
| SIMC, λ = Auto (= L) | PI | 1.6 | 31 s |
| Ziegler-Nichols | PI | 2.8 | 15 s |

**2. Start in Auto.** Set OUT to 50 % and wait for the steady state (PV and SP are both 50 %). On the
faceplate, type Kp and Ti (Td stays 0, Action Reverse), then click **MAN**. It becomes **AUTO**, and
OUT does not move.

**3. Step the setpoint.** In Modbus Control, write 60 to **LIC-101 SP**. Open the Trend and set its
left axis to 0 to 100: right-click the left Y axis and choose **0 to 100** (see
[Y-axis ranges](USER_GUIDE.md#y-axis-ranges) in the User Guide). The Trend then does not rescale while
the loop moves. Plot PV and OUT on that axis.

With the default values (Kp 1, Ti 30 s) PV rises to 60 % with no overshoot and stays within 0.5 % of
SP after about 30 s. OUT first jumps by Kp × 10 % = 10 %, to 60 %, then falls back and settles at about
55 %, the OUT that holds a level of 60 % with HV-102 at 50 %.

**4. Compare the rules.** Return SP to 50 %, then try each pair of values in turn and write SP to 60 %
again. Look at the overshoot (how far PV goes above 60 %) and at the settling time (the time until PV
stays within ±0.5 % of 60 %). In general the Ziegler-Nichols PI is the most aggressive: its Kp is
larger and its Ti shorter, so PV overshoots and may oscillate before it settles, while IMC and SIMC
with λ = L give a smoother response. A larger λ is slower and gentler. The cursors and the
Analysis panel can measure the times for you.

**5. Load disturbance.** With the loop in Auto and PV steady at SP (60 %), move the **HV-102**
slider from 50 to 60 %. The outlet is larger, so PV falls at first. The controller opens the valve,
and the integral action brings PV back to SP: OUT ends at about 66 %, against about 55 % before.
Compare the dip in PV and the movement of OUT for different tunings.

Now type Ti = 0 (P-only) and repeat from a steady state. PV no longer returns to SP: an **offset**
remains, because a P controller needs an error to give a different OUT. A larger Kp makes the offset
smaller. Note also that switching to AUTO with Ti = 0 makes OUT jump, as explained in
[The PID controller](#the-pid-controller).

**6. The wrong action.** In Auto with Ti = 30 s, set **Action** to **Direct**. The controller now
opens the valve when PV is above SP, which adds water. PV moves away from SP and OUT runs away to 100 %
(PV above SP) or to 0 % (PV below SP) and stays there. Set the Action back to Reverse and click
**AUTO** to return to **MAN**, then set OUT by hand if the level has gone to the overflow or to empty.

**Other things to try.** Add Td (for example Td = L / 2) and look at the noise on OUT. Make the dead
time longer in a scenario file and see how the same tuning behaves. Try a setpoint of 80 %: at that
level the process is slower and its gain is higher, so a tuning made at 50 % is less stable.
