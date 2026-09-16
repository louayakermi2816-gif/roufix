# ROUFIX — IoT & AI retrofit architecture for an automotive cable-stripping machine

Industrial internship project, **COFAT Tunisia (Elloumi Group), June–August 2026**.

ROUFIX is a cable-stripping (*rognage*) machine driven by an ageing Siemens LOGO! 8
PLC: the program was lost on power outages, EMI caused crashes, and no production
data ever left the machine. This repository contains the four-layer replacement
architecture I designed and validated in simulation during the internship:

```
┌─ 4. Diagnostic layer ──── Node-RED → Ollama (llama3.2, local) → strict-JSON diagnosis, fallback procedure
├─ 3. Supervision layer ─── Node-RED: piece counter, OEE (TRS), SPC ±3σ drift detection, dashboard
├─ 2. Isolation layer ───── optocouplers + opto-isolated relays (Proteus ISIS)
└─ 1. Control layer ─────── ESP32 firmware: safety-first state machine, MQTT telemetry, degraded modes
```

Each layer is independent: the machine keeps running with no network, no
broker and no AI service — the upper layers only add visibility and assistance.

## 1. Control layer — `firmware/roufix_esp32/roufix_esp32.ino`

Non-blocking ESP32 firmware (no `delay()` in the cycle), hardware mapping frozen
after Proteus validation.

| Inputs | Pin | Outputs | Pin |
|---|---|---|---|
| S1 start button (debounced, 50 ms) | 32 | KM1 abrasive-disc motor contactor | 26 |
| CP safety cover | 33 | KM2 cable-drive motor contactor | 19 |
| ST cylinder end-of-stroke (clamp) | 25 | A+ clamp cylinder extend | 21 |
| RT cylinder end-of-stroke (return) | 27 | A− clamp cylinder retract | 23 |
| DCY cycle enable | 5 | | |
| RTH1 / RTH2 thermal relays | 34 / 35 | | |

**Cycle state machine:** `ATTENTE → FIXATION (A+, KM2) → TEMPORISATION (3 s stripping) → RETOUR (A−) → ATTENTE`,
with a one-shot *event layer* (published once per transition) and a *continuous
layer* (sensor polling, output driving).

**Safety block with absolute priority:** cover open, cycle-stop, or either thermal
fault → all outputs are cut **first**, the cause list is published **second**, and
the start-button state is resynchronised to avoid ghost restarts.

**Telemetry (MQTT, JSON):**
- `roufix/machine1/data` — `{"type_message":"evenement"|"heartbeat","etat":…,"defaut":…,"duree_cycle":ms}`; heartbeat every 10 s
- `roufix/machine1/systeme/statut` — retained `En ligne` / last-will `Hors ligne`
- Degraded mode: with no Wi-Fi or broker the publish path returns immediately; reconnection is attempted every 5 s without blocking the cycle.

Validated in **Wokwi** (`diagram.json`: ESP32 DevKit-C, push-buttons, slide switches for
sensors/safety, LEDs for the four outputs, Wi-Fi gateway).

## 2. Isolation layer — `proteus/`

`ROUFIX_test.pdsprj` / `ROUFIX_doc.pdsprj` (Proteus 8 ISIS): galvanic isolation
between the 3.3 V ESP32 I/O and the 24 V machine side — optocouplers on the inputs,
opto-isolated relay modules on the contactor and valve outputs — so a fault on the
power side cannot reach the controller (the original EMI-crash failure mode).

## 3. Supervision layer — `node-red/flows.json`

- **Piece counter & shift archive** from cycle events, with manual reset.
- **OEE / TRS** = availability × performance × quality, where availability =
  production time / elapsed shift time, performance = (pieces × theoretical cycle
  5.2 s) / production time, and quality = 1 (no reject sensor yet — stated as an assumption).
- **SPC drift detection** on cycle time: learn a baseline of N cycles, freeze
  mean ± 3σ control limits, then flag *high drift* (cycle too long — typical of
  abrasive-disc wear) or *low drift* (abnormally short — clamping/sensor issue).
- **Dashboard** (node-red-dashboard): four gauges (availability, performance,
  quality, OEE), pieces produced, machine state, communication status.

## 4. Diagnostic layer — Node-RED → Ollama

On a drift alert the flow builds a prompt for a **local LLM** (`llama3.2` via
Ollama's `/api/generate`) that embeds the machine architecture and the SPC
numbers, and constrains the answer to a fixed JSON schema:

```json
{"anomalie": "...", "causes_probables": ["...", "..."], "action_immediate": "..."}
```

The response is validated (`Extraire diagnostic`); on timeout (40 s), a stopped
service or a malformed answer, a **fallback** node returns the standard level-1
maintenance procedure instead — the technician is never left without an
instruction.

## Degraded scenarios validated

| Scenario | Behaviour |
|---|---|
| Network / broker loss | cycle continues; publishes are skipped; reconnect every 5 s; last-will marks the machine offline |
| AI service stopped | fallback diagnosis with the standard maintenance procedure |
| Tool wear | cycle-time drift crosses the +3σ limit → alert → LLM diagnosis |
| Safety input during cycle | outputs cut immediately, cause published, machine returns to `ATTENTE` |

## Running it

**Firmware / Wokwi (VS Code):** install *Wokwi for VS Code* and `arduino-cli` with
the `esp32:esp32` core, run `build-wokwi.bat`, then `F1 → Wokwi: Start Simulator`
(`wokwi.toml` points to `build/firmware.elf`). Set `ssid`, `password` and
`mqtt_server` at the top of the sketch for real hardware; the defaults use the
Wokwi guest network and the public HiveMQ test broker — use a private Mosquitto
broker in production.

**Node-RED:** install `node-red-dashboard`, import `node-red/flows.json`, point the
MQTT broker node at your broker. Dashboard at `http://localhost:1880/ui`.

**Ollama:** `ollama pull llama3.2` and keep `ollama serve` running on the
Node-RED host (`http://localhost:11434`).

## Status

Designed and validated in simulation (Wokwi + Proteus + Node-RED + Ollama) during
the internship; deployment on the physical machine is the next step and is not
covered here. Internship report available on request (company data redacted).

## License

MIT — see `LICENSE`.
