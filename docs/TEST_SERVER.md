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
| `--host <address>` | Address to listen on. Default `0.0.0.0` (all interfaces). |
| `--port <port>` | TCP port. Default 1502. `0` lets the OS choose; the chosen port is on the `LISTENING` line. |
| `--unit <id>` | Modbus unit id, 0..247. Default 1. |
| `--tick <ms>` | Simulation tick period, 1..60000 ms. Default 50. |
| `--delay <ms>` | Delay every reply by this many ms (fault injection). 0..600000. |
| `--drop <percent>` | Chance (0..100) per request to drop the client instead of replying. |
| `-v`, `--verbose` | Log every request and connection to stdout. |

Command-line options override the same settings in the configuration file. Positional arguments
are not accepted.

Exit codes: 0 for a normal stop, 1 when the server cannot start (for example the port is in use),
2 for an invalid command line or configuration file.

The server listens on all network interfaces by default and has no authentication. Use
`--host 127.0.0.1` to accept connections from this PC only.

## Register map

Without a configuration file the server has:

- Holding registers HR 0..199 (`holdingCount` 200). All are read/write and start at 0.
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
| IR 10 | lag | follows HR 100, tau 2 s |

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
| `lag` | first-order lag following the holding register `source`: `y += (in - y) * (1 - exp(-dt / tauS))`; `y` starts at `offset` |

All kinds except `counter` are rounded to the nearest integer and clamped to int16. The counter is
uint16. At the default 50 ms tick the counter rises by 20 per second.

## Configuration file

`--config` takes a JSON file. Every key is optional; missing keys keep the defaults above.
The same content is in [`server/server.example.json`](../server/server.example.json).

```json
{
  "host": "0.0.0.0",
  "port": 1502,
  "unitId": 1,
  "holdingCount": 200,
  "inputCount": 64,
  "tickMs": 50,
  "delayMs": 0,
  "dropPercent": 0,
  "verbose": false,
  "signals": [
    { "input": 0,  "kind": "sine",     "amplitude": 1000, "offset": 0, "periodS": 5 },
    { "input": 1,  "kind": "triangle", "amplitude": 1000, "offset": 0, "periodS": 10 },
    { "input": 2,  "kind": "counter" },
    { "input": 3,  "kind": "square",   "amplitude": 1000, "offset": 0, "periodS": 4 },
    { "input": 4,  "kind": "noise",    "amplitude": 100,  "offset": 0 },
    { "input": 5,  "kind": "sawtooth", "amplitude": 1000, "offset": 0, "periodS": 8 },
    { "input": 10, "kind": "lag",      "source": 100, "tauS": 2 }
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
| `delayMs`, `dropPercent`, `verbose` | 0 or more; 0..100; true/false | Fault injection and logging. |
| `signals` | array | Each entry has `input` (IR address), `kind`, and `amplitude`, `offset`, `periodS`, `tauS` or `source` where they apply. |

If `signals` is present it replaces the default bindings completely. Each binding's `input` must be
below `inputCount`. A `lag` binding's `source` must be below `holdingCount`. `periodS` and `tauS`
must be greater than 0.

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

## Stopping

Ctrl+C (or closing the console window) stops the server within one tick, closes all sockets, and
returns exit code 0.
