# Antscan

Antenna gain pattern measurement automation using the **gain transfer (gain comparison) method**, see [How to measure antenna gain](https://www.mictronics.de/posts/HowToMeasureAntennaGain/). A pan-tilt head turns the antenna under test, a Rohde & Schwarz spectrum analyzer with tracking generator (VISA/SCPI) measures S21, and a browser UI configures the scan and plots the result live.

## How it works

```
Browser (client/)  <-- WebSocket + HTTP -->  C server (server/)  --> pan-tilt head (TCP)
                                                                  --> receiver (VISA over TCP/IP)
```

Setup: a fixed auxiliary transmit antenna is connected to the tracking generator output, the receiving antenna to the analyzer input. Both must be in the far field, with matching polarization and aligned for maximum signal.

Measurement procedure:

1. **Calibrate** with the reference antenna (known gain) in place and aligned. The analyzer performs a transmission (S21) normalization, so the reference reads 0 dB, and one data file per measurement frequency is opened. Enter the reference antenna's gain for each measurement frequency.
2. Swap in the **antenna under test** at exactly the same position and alignment and press **Start**.
3. The server sweeps elevation (outer loop) and azimuth (inner loop), reads the S21 marker level at every measurement frequency and adds the reference gain: `G_AUT = G_ref + S21_relative`. Each result goes to the CSV file and is streamed to the browser.

Any shift of position or alignment between steps 1 and 2 adds to the measurement error, and ground reflections should be avoided (absorbers or an anechoic room).

The scan can be paused, resumed and stopped. Pan, tilt, home and the head's self-check are available when no scan is running.

Diagrams of components, sequence, states and message formats are in `design/` (PlantUML sources, SVG renders in `out/design/`).

## Requirements

- Linux, gcc (C18), make
- Libraries: `libwebsockets`, `libuv`, `json-c`, `pthread`, and the R&S VISA library `rsvisa` (headers `rsvisa/visa.h`). `rsvisa` is needed to build even in simulation mode.
- Node.js >= 13.5 only for linting the client

## Build

```sh
make          # builds server/antscan
make clean
```

Hardware access is compiled in only if `REAL` is defined. `server/real.h` ships with `//#define REAL` (commented out), which builds a **simulation**: no head or receiver is contacted and every measurement returns a constant. Uncomment it for real measurements and rebuild.

## Run

Start the server from the repository root (data and web files are located relative to it):

```sh
./server/antscan
```

Then open <http://127.0.0.1:10024/>. The server serves `client/` and the WebSocket on the same port.

| Option | Default | Meaning |
|---|---|---|
| `--ws-ip IP` | `127.0.0.1` | Address the server listens on |
| `--ws-port PORT` | `10024` | HTTP/WebSocket port |
| `--allow-host NAME` | | Additional host name the UI may be opened with, repeatable |
| `--head-ip IP` | `192.168.1.7` | Pan-tilt head |
| `--head-port PORT` | `8899` | Pan-tilt head |
| `--receiver RESOURCE` | `TCPIP::192.168.1.128` | VISA resource of the receiver |

The server only accepts WebSocket connections whose `Host` is `localhost`, `127.0.0.1` or the `--ws-ip` address (plus any `--allow-host`) and, for browsers, whose `Origin` matches that host. To use the UI from another computer, listen on a reachable address and pass the name you type in the browser:

```sh
./server/antscan --ws-ip 0.0.0.0 --allow-host lab-pc.local
```

The connection is unauthenticated and controls hardware and file writes, so only do this on a trusted network.

## Scan settings

| Field | Rules |
|---|---|
| File name | up to 24 characters of `A-Z a-z 0-9 _ -`; empty uses a timestamp |
| Azimuth angle | 0..360, even; the scan is centered on 0° (e.g. 60 gives -30°..+30°) |
| Azimuth resolution | 2..360, must divide the azimuth angle |
| Elevation start / stop | -90..90, start <= stop |
| Elevation resolution | 2..90, must divide the elevation range |
| Start / stop frequency | 1..7000 MHz, span of the receiver sweep |
| Measurement frequencies | comma separated MHz, unique, inside the span |
| Reference gain | gain of the reference antenna in dBi (-100..100), comma separated, one per measurement frequency |

A full circle (360°) is measured as 0..359°.

## Output

One CSV file per measurement frequency in `server/data/` (ignored by git):
`<name>_<frequency>MHz.csv`, or `scan_DD_MM_YYYY_HH_MM_SS_<frequency>MHz.csv`.

```
START_FREQ:800;STOP_FREQ:3000;MEASURE_FREQ:2400;REF_GAIN:1.500000;
TILT/PAN;-10;0;10;
0;-9.0;-8.5;-9.2;
10;-8.8;-8.1;-9.0;
```

The first line holds the settings, the second the azimuth angles, then one row per elevation with the antenna under test gain in dBi (S21 level plus reference gain).

## Development

- `npm install && npm run lint` lints the client (`eslint:recommended`).
- Server and client talk JSON over the WebSocket. Requests are arrays whose positions and command numbers are defined by `jarray_index_e` / `server_command_e` in `server/antscan.h` and mirrored in `client/scripts/index.js`; replies are `{"cmd": ..., "data": ...}` objects. Input is validated on both sides (`createDataFrame` in the client, `parse_calibration` on the server), keep them in sync.
- More architecture notes for contributors are in `CLAUDE.md`.

## License

GPL-3.0-or-later, see [LICENSE](LICENSE). Author: Michael Wolf, <https://www.mictronics.de>.
