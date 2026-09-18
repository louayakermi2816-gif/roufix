# ROUFIX — IoT & AI retrofit architecture for an automotive cable-stripping machine

Industrial internship project, **COFAT Tunisia (Elloumi Group), June–August 2026**.

ROUFIX is a cable-stripping (*rognage*) machine driven by an ageing Siemens LOGO! 8
PLC: the program was lost on power outages, EMI caused crashes, and no production
data ever left the machine. This repository contains the four-layer replacement
architecture I designed and validated in simulation during the internship:

```
┌─ 4. Diagnostic layer ──── Node-RED → Ollama (llama3.2:1b, local) → schema-constrained JSON diagnosis, fallback procedure
├─ 3. Supervision layer ─── Node-RED: piece/reject counter, OEE (TRS), SPC ±3σ drift detection, 3-tab dashboard, Excel archive
├─ 2. Isolation layer ───── PC817 optocouplers + transistor-driven relays (Proteus ISIS)
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
fault → all outputs are cut **first**, the cause list is published **second**
(`Capot_Ouvert`, `Arret_DCY`, `Defaut_RTH1`, `Defaut_RTH2`, space-separated), and
the start-button state is resynchronised to avoid ghost restarts. While the
condition persists, every heartbeat repeats the `ARRET_SECURITE` state and its
cause, so the supervision layer never misses a fault that happened during a
network gap.

**Telemetry (MQTT, JSON):**
- `roufix/machine1/data` — `{"type_message":"evenement"|"heartbeat","etat":…,"defaut":…,"duree_cycle":ms}`; heartbeat every 10 s
- `roufix/machine1/systeme/statut` — retained `En ligne` / last-will `Hors ligne`
- Degraded mode: with no Wi-Fi or broker the publish path returns immediately and
  counts the lost message; reconnection is attempted every 5 s without blocking the cycle.

Validated in **Wokwi** (`diagram.json`: ESP32 DevKit-C, push-buttons for S1/ST/RT,
slide switches for CP/DCY/RTH1/RTH2, LEDs for the four outputs, Wi-Fi gateway,
each part labelled with its machine-side name).

## 2. Isolation layer — `proteus/`

Galvanic isolation between the 3.3 V ESP32 I/O and the 24 V machine side, so a
fault on the power side cannot reach the controller (the original EMI-crash
failure mode):

- **Inputs** — one PC817 optocoupler per sensor/safety contact; its
  phototransistor output is pulled up, so the ESP32 reads an active-low signal.
- **Outputs** — one channel per contactor (KM1, KM2) and valve (A+, A−):
  ESP32 pin → 220 Ω → PC817 → 2N2222 (1 kΩ base) → 5 V relay coil with
  freewheeling diode, whose NO contact switches the 24 V load, itself protected
  by its own diode.

`ROUFIX_test.pdsprj` is the test bench: the machine side is replaced by
`LOGICSTATE` / `SW-SPST` substitutes on the inputs and by lamps and voltmeters
on the outputs, so every channel can be exercised and measured without the
real machine. `ROUFIX_doc.pdsprj` is the same design cleaned for documentation.

## 3. Supervision layer — `node-red/flows.json`

Single Node-RED flow, MQTT in → classification → counters → OEE / SPC → dashboard
and archive.

- **Fault classification** (`Detection defaut machine`): the four stop causes all
  publish the same `ARRET_SECURITE` state, so the `defaut` field decides. Cover
  open and DCY released are *operating stops*; RTH1 / RTH2 are *machine faults*
  and are the only ones that trigger a diagnosis. Rising-edge detection avoids
  re-triggering on every heartbeat.
- **Piece & reject counter** (`Compteur pieces`): a cycle completed normally is a
  good piece; a cycle interrupted by a safety stop is counted as a reject.
- **OEE / TRS** (`Calcul TRS`) = availability × performance × quality, with
  availability = production time / elapsed shift time, performance =
  (good pieces × theoretical cycle 5.2 s) / production time, and quality =
  good pieces / engaged pieces — **measured**, no longer assumed equal to 1.
- **SPC drift detection** (`Detection derive`): learn a baseline of 10 cycles
  (30–50 recommended in production), freeze mean ± 3σ control limits, then flag
  *high drift* (cycle too long — typical of abrasive-disc wear) or *low drift*
  (abnormally short — clamping/sensor issue).
- **Per-operator shift archive**: the operator enters their name, closes the
  shift from the dashboard, and one row is appended to the `Postes` sheet of
  `roufix_suivi.xlsx` (date, times, operator, good pieces, rejects, engaged
  pieces, production time, mean cycle, rate, theoretical cycle, availability,
  performance, quality, OEE, incidents, AI fallbacks, closing reason). Every
  diagnosis, real or fallback, goes to the `Diagnostics` sheet. The workbook is
  read, extended and rewritten with SheetJS (`xlsx` module declared in the
  `Fusionner classeur` function node).
- **Dashboard** (node-red-dashboard, three tabs):
  - *Pilotage Operateur* — machine state, live AI diagnosis card, operator name
    and shift-close button;
  - *Analyse Management* — OEE table with the four indicators and their trend;
  - *Suivi Responsable* — shift table and diagnosis register read back from the workbook.

## 4. Diagnostic layer — Node-RED → Ollama

On a drift alert or a thermal fault the flow (`Preparer prompt IA`) builds a
prompt for a **local LLM** (`llama3.2:1b` via Ollama's `/api/generate`). The
division of labour was measured, not assumed: the 1-billion-parameter model
returned the same answer for a 58 ms and a 2 608 ms overshoot, so **Node-RED
computes the facts and the severity** (relative deviation ≥ 25 % → *severe*;
both thermal relays tripped → *severe*) and **the model only classifies** within
a maintenance reference frame indexed by fault family (long / short cycle,
thermal fault on M1, M2 or both) and by severity.

The answer is constrained by a JSON schema whose fields are `enum`s taken from
that reference frame — a cause or an action outside it is impossible:

```json
{"causes_probables": ["…", "…"], "action_immediate": "…"}
```

Generation options: `num_predict 130`, `temperature 0.2`, `num_ctx 1024`,
`keep_alive -1` (model kept in RAM, pre-warmed at start-up and every 30 min to
avoid a ~7 s reload). The response is validated (`Extraire diagnostic`); on
timeout (40 s), a stopped service or a malformed answer, a **fallback** node
returns the standard level-1 maintenance procedure instead — the technician is
never left without an instruction, and the SPC facts stay on screen either way.

## Degraded scenarios validated

| Scenario | Behaviour |
|---|---|
| Network / broker loss | cycle continues; publishes are skipped and counted; reconnect every 5 s; last-will marks the machine offline |
| AI service stopped | fallback diagnosis with the standard maintenance procedure; SPC facts still displayed |
| Tool wear | cycle-time drift crosses the +3σ limit → alert → severity computed → LLM classification |
| Safety input during cycle | outputs cut immediately, cause published, piece counted as reject, machine returns to `ATTENTE` |
| Thermal fault RTH1 / RTH2 | classified as a machine fault (not an operating stop) → diagnosis targeted at M1 or M2 |

## Running it

**Firmware / Wokwi (VS Code):** install *Wokwi for VS Code* and `arduino-cli` with
the `esp32:esp32` core, run `build-wokwi.bat`, then `F1 → Wokwi: Start Simulator`
(`wokwi.toml` points to `build/firmware.elf`). Set `ssid`, `password` and
`mqtt_server` at the top of the sketch for real hardware; the defaults use the
Wokwi guest network and the public HiveMQ test broker — use a private Mosquitto
broker in production.

**Node-RED:** install `node-red-dashboard`, import `node-red/flows.json`, point the
MQTT broker node at your broker, and set the workbook path in the
`Preparer archive` and `Archiver diagnostic` function nodes (the flow ships with
a Windows path). The `xlsx` module is declared in the `Fusionner classeur` node
and installed by Node-RED on first deploy. Dashboard at `http://localhost:1880/ui`.

**Ollama:** `ollama pull llama3.2:1b` and keep `ollama serve` running on the
Node-RED host (`http://localhost:11434`).

## Status

Designed and validated in simulation (Wokwi + Proteus + Node-RED + Ollama) during
the internship; deployment on the physical machine is the next step and is not
covered here. Internship report available on request (company data redacted).

## License

MIT — see `LICENSE`.
