# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Antscan automates antenna gain-pattern measurements: a pan-tilt head positions the antenna, a Rohde & Schwarz receiver (VISA) measures, and a browser UI configures the scan and plots results live. No tests exist.

## Commands

Run everything from the repo root (paths in the Makefile and data output are root-relative).

- Build server: `make` (produces `server/antscan`); `make clean`
- Run server from the repo root: `./server/antscan` (options: `--ws-ip`, `--ws-port`, `--allow-host`, `--head-ip`, `--head-port`, `--receiver`; websocket default `127.0.0.1:10024`). It also serves `./client` over HTTP on the same port, so open `http://127.0.0.1:10024/`; websocket connections are only accepted for Host `localhost`/`127.0.0.1`/`--ws-ip` (plus `--allow-host NAME`, repeatable, needed when opening the UI via a LAN hostname) and a browser's Origin must equal that Host.
- Lint client: `npm install && npm run lint` (eslint:recommended, `client/` only, vendors ignored; remaining errors are cross-file globals like `Plotly` since scripts aren't modules)
- Diagrams: `design/*.plantuml` render into `out/design/` (PlantUML, SVG committed)

Server needs libs: `rsvisa`, `websockets`, `uv`, `json-c`, `pthread` (see `LIBS` in `Makefile`). The client is static HTML/JS (`client/index.html`) with no build step.

## Architecture

Three tiers, wired as in `design/komponentendiagramm.plantuml` and `design/sequenzdiagramm.plantuml`:

**Browser** (`client/scripts/index.js` UI + WebSocket client, `client/scripts/plotly.js` plotting; vendored libs under `client/scripts/vendors/`) ⇄ **WebSocket** ⇄ **C server** (`server/`) ⇄ **hardware** (`head.c` pan-tilt head, `receiver.c` VISA receiver).

- `server/antscan.c`: libwebsockets + libuv main loop. `handle_client_request` parses the client's **JSON array**; element positions are fixed by `jarray_index_e` and commands by `server_command_e` in `antscan.h`. Any protocol change must be made in both `antscan.h` and `client/scripts/index.js`. Server→client messages go through `app_message` / `app_status` / `app_done` / `app_measurement_point` / `app_progress`; the client reacts to `cmd` (`status`, `done`, `point`, ...), never to message text. Request validation is duplicated in `createDataFrame` (client) and `parse_calibration` (server): keep both in sync. These are thread-safe: they enqueue into a 64-entry ring (`ws_send`) and wake the lws service thread with `lws_cancel_service`; never call `lws_service` from another thread. The server also keeps the accepted calibration frame, every point and the last progress of the current scan (guarded by `msg_lock`, points stored in the same lock hold as the ring enqueue); a new client gets them replayed per session (`replay_step`: `config`, `points` chunks, `progress`) before it joins the ring, so a reload or second browser resyncs. Slow head/receiver operations (pan/tilt/home/check/receiver init) run in a one-at-a-time job thread (`start_job`, `job_busy`) so the service loop never blocks.
- Scan state machine (`scan_status_e`: STOPPED → CALIBRATED → RUNNING ⇄ PAUSED, see `design/zustandsdiagramm.plantuml`). `CALIBRATE_ANTENNA` takes the whole scan config, allocates the per-frequency `Frequency[]`, inits the receiver and opens CSV files; `SCAN_START` is only honored once CALIBRATED (reference antenna measured, then the antenna under test swapped in).
- `server/loop.c`: `measurement_loop` runs in its own `uv_thread` (pinned to core 2): elevation outer loop, azimuth inner loop, per step `set_pan/tilt_position` → `measure()` → `get_data()` per frequency + reference gain → CSV + `app_measurement_point`. Pause/stop use `lock_pause_measurement` / `cond_resume_measurement` / `measurement_thread_exit` globals shared (via `extern`) with `antscan.c`. Output: `server/data/*.csv` (gitignored).
- Head and receiver calls return 0 / -1 (3 s socket timeouts, 60 s position wait); jobs report a danger message, while `measurement_loop` wraps each call in `do_op`: up to 3 retries 2 s apart (reconnecting the head socket via `head_reconnect` or the VISA session via `reopen_receiver`, never `init_receiver`, which resets the instrument), then `scan_pause()` and a wait for the operator to resume, which repeats the failed step. `server/head.c`: raw TCP protocol to the pan-tilt head (`--head-ip/--head-port`, default `192.168.1.7:8899`). `server/receiver.c`: VISA over TCPIP (`--receiver`, default `TCPIP::192.168.1.128`).

## Simulation vs. real hardware

Hardware calls are guarded by `#ifdef REAL`. `server/real.h` holds `//#define REAL` (commented out = simulation: no head/receiver access, `loop.c` emits a synthetic Gaussian lobe). Uncomment it to build against real hardware; every file that needs it includes `real.h`.

## Versioning

Semver; `package.json` `version` is the single source. The Makefile passes it to the server as `ANTSCAN_VERSION` (`--version`, startup log, `version` message to the UI footer). To release: bump `package.json` and `package-lock.json`, commit, `git tag vX.Y.Z`.

## Gotchas

- `-std=c18` with `-D_GNU_SOURCE`. `rsvisa` (R&S VISA) must be installed to compile/link; it is needed even in simulation mode.
- `server/*.o` and `server/antscan` are gitignored build artifacts.
- Azimuth sector 360 is clamped to 359 in both `file_init` and `measurement_loop`.
