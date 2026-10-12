# Test server (modbus_test_server)

`modbus_test_server.exe` is a console Modbus TCP server for trying out Modbus Control without a
device. It serves holding and input registers, some of which hold simulated signals, and it can
inject delays and dropped connections. The integration tests start it too.

## Running it

```bat
build\msvc-debug\bin\modbus_test_server.exe --port 1502
```

At start-up it prints a banner, the unit id, the register ranges, the bound signals and the
`LISTENING <port>` line. Stop it with Ctrl+C.

## Command line

| Option | Meaning |
|---|---|
| `-h`, `--help` | Show the help. |
| `--version` | Show the version and the libmodbus version. |
| `--config <file>` | JSON configuration file (see below). |
| `--host <address>` | Address to listen on. Default `127.0.0.1` (local connections only, because the server has no authentication). Use `--host 0.0.0.0` to accept remote clients. |
| `--port <port>` | TCP port. Default 1502. `0` lets the OS choose; the chosen port is on the `LISTENING` line. |
| `--unit <id>` | Modbus unit id, 0..247. Default 1. |
| `--tick <ms>` | Simulation tick period, 1..60000 ms. Default 50. |
| `--delay <ms>` | Delay every reply by this many ms (fault injection). 0..600000. |
| `--drop <percent>` | Chance (0..100) per request to drop the client instead of replying. |
| `-v`, `--verbose` | Log every request and connection to stdout. |
| `--log-writes` | Log only write requests, with time stamps, registers in hex and 32-bit values decoded in every byte order (see below). |

Command-line options override the same settings in the configuration file. Positional arguments
are not accepted.

Exit codes: 0 for a normal stop, 1 when the server cannot start (for example the port is in use),
2 for an invalid command line or configuration file.

The server listens on all network interfaces by default and has no authentication. Use
`--host 127.0.0.1` to accept connections from this PC only.

## Register map

Without a configuration file the server has:

- Holding registers HR 0..199 (`holdingCount` 200). All are read/write and start at 0, except the
  initial values listed below.
- Input registers IR 0..63 (`inputCount` 64). Unbound ones stay at 0.
- Coils and discrete inputs are not mapped.

Addresses outside these ranges return the Modbus exception "illegal data address" (code 2).

Default signal bindings:

| Input register | Signal | Parameters |
|---|---|---|
| IR 0 | sine | amplitude 1000, period 5 s |
| IR 1 | triangle | amplitude 1000, period 10 s |
| IR 2 | counter | starts at 0, +1 per tick |
| IR 3 | square | amplitude 1000, period 4 s |
| IR 4 | noise | amplitude 100 |
| IR 5 | sawtooth | amplitude 1000, period 8 s |
| IR 10 | lag | follows HR 100, tau 2 s, dead time 1 s |
| IR 20-21 | sine, float32 ABCD | amplitude 123.456, period 5 s |
| IR 22-23 | sine, float32 CDAB | amplitude 123.456, period 5 s |
| IR 24-25 | sawtooth, int32 ABCD | amplitude 100000, period 8 s |

Default initial holding values (written when the server starts; they stay writable):

| Holding register | Value | Purpose |
|---|---|---|
| HR 110-111 | float32 CDAB, 52.3 | a CM50-like setpoint, for trying encoding detection |
| HR 120-121 | int32 ABCD, 100 | a 32-bit value that is ambiguous when read in the wrong order |

Writing HR 100 changes the lag output on IR 10, so a setpoint step shows as a first-order response.

## Signals

Each signal is evaluated once per tick. With `phase = (t mod period) / period` and `offset`
defaulting to 0:

| Kind | Value |
|---|---|
| `constant` | `offset` |
| `sine` | `offset + amplitude * sin(2 * pi * phase)` |
| `triangle` | rises from `offset - amplitude` at phase 0 to `offset + amplitude` at phase 0.5, then falls back |
| `sawtooth` | `offset - amplitude + 2 * amplitude * phase` (rises, then wraps) |
| `square` | `offset + amplitude` for the first half of the period, `offset - amplitude` for the second half |
| `counter` | starts at `offset`, +1 per tick, wraps from 65535 to 0 (`amplitude` and `periodS` are ignored) |
| `noise` | `offset` plus a uniform random value in [-amplitude, +amplitude]; seeded per binding, so the sequence repeats between runs |
| `lag` | first-order lag following the holding register `source` with a dead time `deadTimeS`: `y += (in(t - deadTimeS) - y) * (1 - exp(-dt / tauS))`; `y` starts at `offset` |

All kinds except `counter` are rounded to the nearest integer and clamped to int16 (for `int32` and `float32` see Value formats). The counter is
uint16. At the default 50 ms tick the counter rises by 20 per second.

## Value formats

Each binding has a `format` that sets how its value is stored in the input registers:

| `format` | Registers | Stored as |
|---|---|---|
| `int16` (default) | 1 | rounded to nearest, clamped to -32768..32767, two's complement |
| `int32` | 2 | rounded to nearest, clamped to -2147483648..2147483647, two's complement |
| `float32` | 2 | IEEE-754 single precision, not rounded |

A 2-register value occupies `input` and `input + 1`. The `byteOrder` key gives the order of the four
bytes A (most significant) to D across the two registers. It applies only to the 2-register formats:

| `byteOrder` | IR n | IR n+1 |
|---|---|---|
| `ABCD` (default) | A B | C D |
| `CDAB` | C D | A B |
| `BADC` | B A | D C |
| `DCBA` | D C | B A |

For example, float32 123.456 is 0x42F6E979. `ABCD` stores 0x42F6 at `input` and 0xE979 at
`input + 1`; `CDAB` stores 0xE979 and then 0x42F6.

The `counter` kind with `int16` is the raw uint16 counter, so it wraps at 65535 rather than being
clamped at 32767. The `format` and `byteOrder` keys are optional and can be combined with any kind.

## Configuration file

`--config` takes a JSON file. Every key is optional; missing keys keep the defaults above.
The same content is in [`server/server.example.json`](../server/server.example.json).

```json
{
  "host": "127.0.0.1",
  "port": 1502,
  "unitId": 1,
  "holdingCount": 200,
  "inputCount": 64,
  "tickMs": 50,
  "delayMs": 0,
  "dropPercent": 0,
  "verbose": false,
  "logWrites": false,
  "signals": [
    { "input": 0,  "kind": "sine",     "amplitude": 1000, "offset": 0, "periodS": 5 },
    { "input": 1,  "kind": "triangle", "amplitude": 1000, "offset": 0, "periodS": 10 },
    { "input": 2,  "kind": "counter" },
    { "input": 3,  "kind": "square",   "amplitude": 1000, "offset": 0, "periodS": 4 },
    { "input": 4,  "kind": "noise",    "amplitude": 100,  "offset": 0 },
    { "input": 5,  "kind": "sawtooth", "amplitude": 1000, "offset": 0, "periodS": 8 },
    { "input": 10, "kind": "lag",      "source": 100, "tauS": 2, "deadTimeS": 1 },
    { "input": 20, "kind": "sine",     "amplitude": 123.456, "offset": 0, "periodS": 5, "format": "float32", "byteOrder": "ABCD" },
    { "input": 22, "kind": "sine",     "amplitude": 123.456, "offset": 0, "periodS": 5, "format": "float32", "byteOrder": "CDAB" },
    { "input": 24, "kind": "sawtooth", "amplitude": 100000, "offset": 0, "periodS": 8, "format": "int32", "byteOrder": "ABCD" }
  ],
  "holding": [
    { "address": 110, "value": 52.3, "format": "float32", "byteOrder": "CDAB" },
    { "address": 120, "value": 100,  "format": "int32",   "byteOrder": "ABCD" }
  ]
}
```

Keys:

| Key | Range | Notes |
|---|---|---|
| `host`, `port`, `unitId` | port 1..65535, unitId 0..247 | The command line also accepts `--port 0` (let the OS choose). |
| `holdingCount` | 1..65536 | Holding registers HR 0..count-1. |
| `inputCount` | 1..65536 | Input registers IR 0..count-1. |
| `tickMs` | 1 or more | Simulation tick. |
| `delayMs`, `dropPercent`, `verbose`, `logWrites` | 0 or more; 0..100; true/false; true/false | Fault injection and logging. |
| `signals` | array | Each entry has `input` (IR address), `kind`, and `amplitude`, `offset`, `periodS`, `tauS`, `deadTimeS` or `source` where they apply, and optionally `format` and `byteOrder` (see Value formats). |
| `holding` | array | Initial holding register values, see below. Each entry has `address` (HR), `value`, and optionally `format` (`int16` default, `uint16`, `int32`, `uint32`, `float32`) and `byteOrder` (default `ABCD`, used by the 32-bit formats). |

If `signals` is present it replaces the default bindings completely. Each binding's `input` must be
within `inputCount` (a 2-register format also uses `input + 1`). A `lag` binding's `source` must be below `holdingCount`. `periodS` and `tauS`
must be greater than 0. `deadTimeS` (lag only, default 0) must be from 0 to 60 seconds; the lag input is taken that many seconds late, so a step on the source shows up on the output after the dead time and then rises with time constant `tauS`.

### Initial holding values

`holding` sets the value of holding registers when the server starts. Each `address` is a holding
register (zero-based, like everywhere else); a 2-register format also uses `address + 1`. The value
is encoded like the signal formats, and the registers stay writable afterwards, so a lag binding
whose `source` is one of these registers sees the initial value at once.

| `format` | Registers | Value range |
|---|---|---|
| `int16` (default) | 1 | -32768..32767, whole number |
| `uint16` | 1 | 0..65535, whole number |
| `int32` | 2 | -2147483648..2147483647, whole number |
| `uint32` | 2 | 0..4294967295, whole number |
| `float32` | 2 | finite, within the single-precision range |

`byteOrder` works as in Value formats and is ignored for the 16-bit formats. Entries must fit below
`holdingCount`, must not overlap each other, and may only use the keys above. Unknown keys and
strings are rejected with the message `invalid config: "holding[n].<key>" ...`.

```json
"holding": [
  { "address": 110, "value": 52.3, "format": "float32", "byteOrder": "CDAB" },
  { "address": 120, "value": 100,  "format": "int32",   "byteOrder": "ABCD" },
  { "address": 5,   "value": 65535, "format": "uint16" }
]
```

If `holding` is present it replaces the default initial values completely (`"holding": []` starts
every holding register at 0). Without the key the defaults stay, except that default entries that do
not fit below a smaller `holdingCount` are dropped, so older configuration files still load.

## Fault injection

- `--delay <ms>` (`delayMs`): every reply waits this long. The server handles one request at a time,
  so the delay also holds up other clients. If the delay is longer than the Timeout set in the
  connection bar, the affected tags show quality **Timeout**, and the status bar reports the read
  failure.
- `--drop <percent>` (`dropPercent`): for each request, with this chance, the server closes that
  client's connection without replying. Expected on the client: the LED turns orange
  (**Reconnecting**), the tags show quality **Disconnected**, and the application retries with
  back-off until the server answers again.

## Output

Start-up output ends with:

```
LISTENING 1502
```

This line is printed after the server is listening, and the number is the port actually bound.
Scripts (and the integration tests) can read it when `--port 0` is used.

With `-v` each request is logged as one line, for example:

```
[12] unit=1 fc=0x03 read addr=0 count=10
[12] unit=1 fc=0x06 write addr=100 value=500
[12] disconnected (closed or error)
```

The number in brackets is the socket. The server accepts at most 60 clients at a time.

### Checking writes with `--log-writes`

`--log-writes` logs only the write requests, so polling does not hide them. Each line has a time
stamp and the registers in hex. A write of two registers is also decoded as a float in all four
byte orders, so you can see which order the client sent:

```
12:08:53.539 [576] unit=1 fc=0x10 write addr=110 count=2 values=0x42F7,0x0000 | float ABCD=123.5 CDAB=2.40224596e-41 BADC=-3.93478746e+33 DCBA=8.86993902e-41
```

Here the client wrote 123.5 in `ABCD` order. With **FC06 only**, a 32-bit value arrives as two
single-register writes; two consecutive FC06 writes to neighbouring addresses are decoded
together on an extra line:

```
12:08:53.541 [576] unit=1 fc=0x06 write addr=120 value=0x0000 (0)
12:08:53.543 [576] unit=1 fc=0x06 write addr=121 value=0x42F7 (17143)
12:08:53.543 [576]   FC06 pair addr=120-121 values=0x0000,0x42F7 | float ABCD=2.40224596e-41 CDAB=123.5 BADC=8.86993902e-41 DCBA=-3.93478746e+33
```

`addr` is always the zero-based protocol address (register 6639 in a 1-based manual is
`addr=6638`). Holding registers 0..199 are plain storage, so a value written there reads back
unchanged: a writable float32 holding tag at, say, address 110 lets you test writes end to end.

## Stopping

Ctrl+C (or closing the console window) stops the server within one tick, closes all sockets, and
returns exit code 0.
