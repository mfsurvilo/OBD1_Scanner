# OBD1 Scanner — FW ↔ Front-End Protocol

**Protocol version: 1** · Status: living document · Last updated 2026-07-01

This is the single source of truth for the contract between the ESP32 firmware
(`firmware/`) and any front end (`pwa_app/`, `flask_app/`, future UIs). If the
firmware and a UI disagree, they disagree with *this file* — fix the doc and
both sides, don't quietly diverge.

## The value pipeline

```
raw byte ──[FW: hardcoded formula]──▶ engineering ──[app: user calibration]──▶ displayed
             engineering = raw·hw_scale + hw_offset      displayed = engineering·cal_slope + cal_offset
```

- **Hardcoded formula** (`hw_scale`, `hw_offset`) — a fixed property of the SSM1
  protocol per parameter (e.g. `rpm = raw·25`, `°F = raw·1.8 − 147.6`). Baked
  into firmware in `ecu_defs.h` as the `paramMappings[]` table, keyed by name and
  shared across all ECU tables. **Not user-editable.** Source:
  `resources/possible_mapping.txt`. The FW applies this and streams the
  **engineering** value as `params[].value` (and `raw` alongside).
- **User calibration** (`cal_slope` default 1, `cal_offset` default 0) — a trim
  the user applies on top, per parameter. Persisted in FW flash
  (`scaling_config.*`; the NVS keys are still `multiplier`/`offset`). **Applied
  by the app**, not the FW — so tuning is instant and doesn't wear flash. Defaults
  are a no-op, so gauges read correct engineering units out of the box.
- **Poll factor** — the weighted-round-robin poll weight (0 = off). User-editable
  and persisted per param (alongside calibration). On save the FW pushes it to
  the live scheduler; on boot it seeds from the stored value (else a poll-tier
  default).

## Ownership split

| Concern | Owner | Persisted where |
|---|---|---|
| **Hardcoded formula** — `hw_scale`, `hw_offset`, `unit` per param | **Firmware (fixed)** | compiled in `ecu_defs.h` (`paramMappings[]`) |
| **User calibration** — `cal_slope`, `cal_offset` | **User sets; FW stores; app applies** | ESP32 flash (`scaling_config.*`) |
| **Poll factor** — per-param weight (0 = off) | **User sets; FW stores + runs** | ESP32 flash + live scheduler |
| **Presentation** — display label, min/max range, grouping | **Front end** | client-side (localStorage) |

---

## Transports

| Purpose | Transport | Address |
|---|---|---|
| The PWA itself (HTML/CSS/JS/icons) | HTTP GET | `http://<host>/` — served from ESP LittleFS |
| Live data stream (primary) | WebSocket | `ws://<host>:81/` — **root path, no `/ws` suffix** |
| Snapshot poll (fallback) | HTTP GET | `http://<host>:80/data` |
| Calibration / logs / control | HTTP | `http://<host>:80/...` |

`<host>` is `192.168.4.1` (SoftAP `OBD1_Scanner`) or `obd1.local` (mDNS). The
scanner is **AP-only** — the phone connects straight to its access point and
loads the PWA from the device; there is no home-WiFi/station mode.

> ⚠️ **Known drift to fix (2026-07-01):** the current PWA connects to
> `ws://…:<port>/ws` (`pwa_app/js/app.js:378`) and its settings default the port
> to `81` in JS but show `80` in the HTML input (`index.html:262`). Per this
> spec the WS is **port 81, path `/`**. New UI must follow the spec.

---

## Downstream: data snapshot (FW → UI)

The firmware maintains one JSON snapshot and (a) broadcasts it over the
WebSocket whenever it changes, and (b) serves the latest copy at `GET /data`.
On WS connect the current snapshot is sent immediately so the UI populates
without waiting for the next change.

### Schema

```jsonc
{
  "connected": true,          // bool — FW believes it is talking to the ECU
  "timestamp": 123456,        // uint — millis() of last data change (FW uptime)
  "params": [                 // array — one entry per param currently served
    {
      "name":  "EngineSpeed", // string — stable catalog key (see /params)
      "raw":   128,           // number — the ECU byte, before any conversion
      "value": 3200,          // number — ENGINEERING value = raw·hw_scale + hw_offset (FW-applied)
      "unit":  "rpm",         // string — engineering unit
      "time":  123450         // uint   — millis() this param was last read
      // The app shows: value·cal_slope + cal_offset (user calibration, app-applied).
    }
  ],
  "baseHz": 12.5,             // number — est. Hz for a factor-1 param (see polling model)
  "dtc": [                    // array — present only once DTCs are reported
    { "type": "active", "code": "21", "desc": "Coolant temp sensor" }
  ],
  "status": "..."             // string — optional human-readable status line
}
```

Notes:
- `params` is keyed by `name` on both sides. **Never** index by array position.
- `raw` is the ECU value pre-scaling. The UI computes the displayed value as
  `raw * scale + offset` using the calibration for that `name` (from `/params`).
  The wire never carries a pre-scaled `value` — see "Persist ≠ apply" above.
- `params` contains only params whose active `factor > 0`. A param set to 0 is
  absent from the snapshot, not present with a stale value.
- `dtc[].type` ∈ `"active" | "stored"`. `code` is the Subaru 2-digit code as a
  string; `desc` is human text.
- A UI must tolerate unknown `name`s and missing optional keys (`dtc`, `status`).

### Parameter catalog (active ECU: `7232A5` — 1992 UK Legacy EJ22)

These are the `name`s the firmware can currently produce (from `ecu_defs.h`).
Units below are the **firmware default**; the UI owns display range/label.

| name | default unit | poll tier |
|---|---|---|
| EngineSpeed | rpm | fast (50 ms) |
| VehicleSpeed | mph/km/h | fast |
| ThrottlePosition | % | medium (100 ms) |
| O2Average | V | medium |
| IgnitionAdvance | ° BTDC | medium |
| EngineLoad | % | medium |
| InjectorPulseWidth | ms | medium |
| KnockCorrection | ° | medium |
| AFCorrection | % | medium |
| BatteryVoltage | V | slow (250 ms) |
| CoolantTemp | °F/°C | slow |
| AirflowSensor | — | slow |
| ISUDutyValve | % | slow |
| AtmosphericPressure | kPa | vslow (500 ms) |
| InputSwitches | bitfield | vslow |
| IOSwitches | bitfield | vslow |

> The catalog changes with the selected ECU (`#define ECU_*` in `ecu_defs.h`).
> UIs should not hardcode this list — read it from `GET /params` (below).

---

## The parameter record

`GET /params` returns one record per param for the active ECU, combining the
fixed hardcoded mapping with the user-editable calibration:

| field | type | editable? | meaning |
|---|---|---|---|
| `name` | string | no | stable catalog key, e.g. `"EngineSpeed"` |
| `addr` | string | no | ECU memory address, e.g. `"0x1338"` |
| `unit` | string | no | engineering unit, e.g. `"rpm"` |
| `hw_scale` | number | no | hardcoded formula multiplier (`paramMappings[]`) |
| `hw_offset` | number | no | hardcoded formula offset |
| `cal_slope` | number | **yes** (default 1) | user calibration multiplier |
| `cal_offset` | number | **yes** (default 0) | user calibration offset |
| `factor` | integer | **yes** (0 = off) | poll weight (live scheduler value) |

- `POST /scaling` body `{scaling:[{name, multiplier, offset, factor}]}` →
  validates, saves to flash, pushes `factor` to the live scheduler, returns
  `"status":"saved"`. (`multiplier` = `cal_slope`.) Errors →
  `{"status":"error","message":"..."}` with 400/500.
- `GET /scaling` → persisted calibration records (per `ScalingConfig.toJson`).
- CORS: `Access-Control-Allow-Origin: *`; preflight `OPTIONS /scaling` → 204.

> ⚠️ **Scope note:** `addr`/`name`/`hw_*` are compile-time `const` in
> `ecu_defs.h`; only `cal_slope`/`cal_offset`/`factor` are editable/persisted.
> Making `addr`/`name` runtime-editable is a later expansion.

### ECU Calibration page (under the home menu)

Shows every param with its hardcoded formula (read-only) plus editable
`cal_slope`, `cal_offset`, and `factor`. **Save** issues one `POST /scaling`
(the only screen that writes to flash) and updates the live gauges immediately.

---

## Logs & diagnostics (HTTP)

| Endpoint | Returns |
|---|---|
| `GET /log` | `{ "ram": "<log text>", "ramUsed": <int> }` (newlines escaped) |
| `GET /log/ram` | RAM log as `text/plain` |
| `GET /log/ram/clear` | `{ "status":"ram_cleared" }` |
| `GET /log/flash` | flash log as `text/plain` |
| `GET /log/flash/clear` | `{ "status":"flash_cleared" }` |

---

## Polling model (factors, not periods)

Each param has an integer **polling factor** that is a *relative weight*, not a
period:

- `factor = 0` → param is **off** (not sampled, absent from snapshot).
- `factor = 1` → the baseline rate.
- `factor = n` → sampled **n× as often** as a factor-1 param.

The ECU link runs at a fixed physical throughput. Let **R** = reads/second the
link can sustain (a function of baud rate + per-read response latency; the FW
knows it best). The FW polls in a weighted round-robin, so with active factors
`f₁…fₖ`:

```
baseHz = R / Σ fᵢ            # rate of a factor-1 param
paramHz(i) = fᵢ · baseHz     # rate of param i
```

So adding params or raising factors *lowers* `baseHz` — the UI surfaces the
tradeoff by displaying `baseHz` (see "base polling frequency" in the UI). The FW
reports `baseHz` in every snapshot; it also reports `R` once (in `/params`, as
`readsPerSec`) so the UI can **estimate `baseHz` locally and instantly** as the
user changes views, before the subscription round-trips.

**Startup state:** on boot every factor is `0` — nothing is polled. Polling
begins only when a connected UI sends a subscription.

**View presets (UI-side):** each view maps to a set of factors; entering the
view sends that subscription, so only what's on screen is sampled and it polls
fast. Example — *General Vehicle Data*:

```jsonc
{ "EngineSpeed": 3, "VehicleSpeed": 3, "CoolantTemp": 1,
  "BatteryVoltage": 1, "IntakeAirTemp": 1 }   // everything else → 0
```

*(Assumes IntakeAirTemp = 1; confirm — your note listed 5 params but 4 factors.)*

---

## Upstream: front-end drives the active set  — ⚠️ PROPOSED, not yet implemented

Today the WebSocket is **receive-only** (`wifi_server.cpp:247`). Per-param poll
factors are currently set via `POST /scaling` (persisted, applied to the live
scheduler) — good enough for the calibration page. The WS `subscribe` below is a
**future** optimization for fast, transient, view-driven switching (enter a view
→ poll only its params, no flash write). It is the proposed design — **build the
UI against it via the mock, then implement it in firmware.**

### 1. Discover the catalog — ✅ implemented (`GET /params`)

```jsonc
GET /params  →
{
  "ecu": "7232A5",
  "name": "1992 UK Legacy EJ22",
  "params": [
    { "name":"EngineSpeed", "addr":"0x1338", "unit":"rpm",
      "hw_scale":25.0, "hw_offset":0.0,     // hardcoded (read-only)
      "cal_slope":1.0, "cal_offset":0.0,    // user calibration (editable)
      "factor":10 }                          // live poll weight (editable via POST /scaling)
    // ...one record per catalog entry
  ]
}
```

### 2. Subscribe (UI → FW, WebSocket text frame)

```jsonc
{ "cmd": "subscribe",
  "factors": { "EngineSpeed": 3, "VehicleSpeed": 3, "CoolantTemp": 1 } }
```

- Names present set that factor; **names absent are set to 0** (a subscribe is
  the complete active set, not a delta). Send `{}` to stop all polling.
- Other proposed commands: `{ "cmd":"clearDTC" }`, `{ "cmd":"ping" }`.
- Unknown `cmd` ⇒ FW ignores and (optionally) logs.

### 3. Global scope; FW acknowledges

**Decided (2026-07-01):**

- **Scope is global, not per-connection.** One active set for the whole device;
  the most recent `subscribe` wins for all ≤4 clients. A late-joining client
  sees the current set until it sends its own. UIs must treat the active set as
  **shared state** — a second phone's subscribe changes what the first receives.
- **The FW confirms every subscription** with an ACK frame:

  ```jsonc
  { "ack": "subscribe",
    "active": { "EngineSpeed": 3, "VehicleSpeed": 3, "CoolantTemp": 1 },
    "baseHz": 8.9,             // recomputed for the new active set
    "rejected": [ ]            // names not in the current ECU catalog
  }
  ```

  The UI reconciles against `active`/`rejected` rather than assuming its request
  applied verbatim (a `name` may be unknown to the current ECU).

---

## Versioning

- The snapshot **should** carry the protocol version once the upstream channel
  lands, e.g. add `"proto": 1` at the top level of `/data` and the WS snapshot.
- Bump the version on any breaking change to the snapshot schema. Additive
  fields (new optional keys) do **not** bump the version; removals/renames do.

---

## Machine-readable schema & conformance

The shapes above are also defined as JSON Schema and enforced at runtime:

- **`protocol/schema.json`** — canonical `$defs` for `Snapshot`, `ParamSample`,
  `Dtc`, `ParamsResponse`, `ParamDescriptor`, `SubscribeCommand`, `SubscribeAck`.
  This file and this doc must agree; changing one means changing the other.
- **`protocol/validate.js`** — dependency-free ES-module validator (no build
  step). `import { validateSnapshot } from '../protocol/validate.js'`.

Any front-end mock transport (e.g. `pwa_app`'s `simulate*` functions, or a
future `MockTransport`) MUST emit messages that pass these validators — same
keys, same `params[]` keyed by `name`. Both `MockTransport` and
`WebSocketTransport` should run every inbound snapshot through `validateSnapshot`
in dev, so the mock **cannot** drift from the real shape. If the UI works
against the validated mock, swapping in the real WebSocket is a transport change
only, never a data-shape change. That equivalence is the whole point of this
layer.

Additive fields are allowed (validators don't reject unknown keys) — matching
the versioning rule. Only removals/renames are breaking.
