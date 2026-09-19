# E-Ink Weather Display — Specification

**Status:** Draft for approval
**Date:** 2026-09-18
**Authors:** cbrown + Claude
**Version:** 0.1

---

## 0. How to read this document

This spec is the **single source of truth** for what gets built. Every requirement has a
stable ID (`FR-x`, `NFR-x`, `HW-x`, `IF-x`) so that tests can cite it and so that "done"
can be checked rather than assumed. Where a requirement depends on a fact that is not
verified, it is marked:

- **[VERIFIED]** — grounded in a primary source quoted in §12, or in the repo's own
  datasheets/demo code.
- **[ASSUMED]** — a reasonable engineering choice, documented so it can be challenged.
- **[OPEN]** — genuinely unresolved; carries an explicit question and a fallback plan.

Nothing in this document may be implemented on the strength of an unmarked claim.

---

## 1. Goal

Build a two-part system that puts a configurable, beautiful weather dashboard on a
5.76″ e-ink panel driven by an ESP32:

1. **Firmware** on the Good Display **ESP32-M1** board that fetches weather + Home
   Assistant data, renders the dashboard to the panel, and serves a device config API,
   OTA, and its own web UI.
2. **A web configuration app** where the user designs the layout visually — drag,
   resize, bind widgets to data sources, set alert rules, define multiple pages — and
   saves/loads configurations as local files.

**Primary intent (this phase):** display these specific values, well:

- `sensor.upstairs_hallway_temperature` (Home Assistant)
- `sensor.64b708cfe0fc_sensor_2_temperature_f` — outside coop temperature (Home Assistant)
- Current conditions and forecast for the user's location (OpenWeatherMap)
- A location picked precisely on a map, plus a zip code for the general area

…but the system must be **fully general**: any data source, any size, any position on
screen, all configured through the web UI without touching firmware.

**Non-goal for this phase:** colour/multi-panel support, touch input, frontlight (this
panel is purely reflective), and any cloud service other than OpenWeatherMap.

---

## 2. Hardware platform

### 2.1 Board: Good Display ESP32-M1

**[VERIFIED]** The board is an **ESP32-WROOM-32D** (ESP32-D0WD, dual-core, 4 MB flash,
**~320 KB usable SRAM, no PSRAM**) SPI e-paper driver board. Published outside
dimensions 66 × 48 × 1.0 mm (modeled 48 × 66 mm). It carries: USB-C (via **CH340C**
USB-serial), 3.7 V Li-ion connector (SH1.0) with **LTC4054** charger, power switch,
microSD socket, a KEY button, RST/BOOT buttons, a frontlight circuit (unused here), touch
FPC connectors (unused here), and the **P5 24-pin e-paper FPC**.

### 2.2 Panel: GDEH0576T81

**[VERIFIED]** 920 (H) × 680 (V) px, **198 dpi**, active area 117.668 × 86.972 mm,
outline 125.4 × 99.5 × 0.9 mm, 28.08 g. Controller **SSD2677**. Purely reflective — no
frontlight, no touch. Native orientation **landscape**.

- **Operating temperature 0…50 °C**; storage −25…60 °C. **[VERIFIED]**
- **600 mAs** typical per panel update; **1–3 µA** deep-sleep current. **[VERIFIED]**

> **HW-1** The firmware shall treat **0 °C as a hard lower bound for reliable
> operation**, and shall surface a warning state rather than render garbage if the
> controller's reported temperature is outside its operating range.
>
> **[MEASURED 2026-09-18 — the guard needs a fourth state.]** On this hardware the
> SSD2677's internal sensor does **not** return live ambient temperature: `0x40` TSC
> reads a constant **−15 °C**, `0x43` TSR a constant **−1 °C**, and the datasheet's TSE
> offset register (`0x41`) moves **neither** across all 8 codes. This is not a transport
> fault — the same read path returns the datasheet-correct `0x07` from REV (`0x70`) and
> `0x00` from an unused register, and REV is the documented power-on value. The panel's
> I²C external-sensor lines (`TSCL`/`TSDA`, pins 6/7) reach GPIO22/21, but GPIO21 doubles
> as the microSD `CS` and no external sensor is fitted.
>
> Consequence: **an unreadable sensor must not be treated as cold.** Folding it into
> `THERMAL_TOO_COLD` (the plan's first draft) makes the guard block *every* render
> forever on any unit that reports a constant — which is exactly what this bench unit
> does. The guard therefore returns `THERMAL_UNKNOWN` for an absent/unusable source, and
> the caller renders and logs. `THERMAL_TOO_COLD`/`TOO_HOT` are reserved for readings
> that actually came from a live source.

### 2.3 Pin map — the hardware contract

**[VERIFIED]** Decoded from the schematic (`code/ESP32-M1-SCH-20260307.pdf`) and
cross-checked against both the vendor demo and Espressif's `pins_arduino.h`. The
schematic labels module pins with Arduino *analog* aliases, which is why the table looks
unusual.

| Function | Arduino alias | GPIO |
|---|---|---|
| EPD **BUSY** | A14 | **13** |
| EPD **RES / RST** | A15 | **12** |
| EPD **D/C** | A16 | **14** |
| EPD **CS** | A17 | **27** |
| EPD **SCLK** | — | **18** |
| EPD **SDI / MOSI** | — | **23** |
| microSD **CS** (`SPI_CS_SD`) | — | **21** |
| GT30 font chip **CS** (`SPI_CS_GT30`) | A13 | **15** |
| **VBAT sense** (`VREF`, R8 300K / R9 1M divider) | A19 | **26** |
| KEY button (`KEY1`) | A18 | **25** |
| USB serial TX / RX | — | 1 / 3 |

> **HW-2** The e-paper, microSD and (if populated) the GT30 font chip **share one SPI
> bus**. Firmware shall arbitrate the bus with per-device chip-selects and shall never
> drive two CS lines low simultaneously.

> **HW-3 Battery/USB auto-detection.** The user requires automatic detection of
> battery vs. mains. **No dedicated USB-present or charge-status pin is confirmed
> available** — the `CHRG` net's destination GPIO could not be confirmed from the
> schematic, and `CHP_PU`/`SENSOR_VP`/`SENSOR_VN` turned out to be the module's own pin
> names, not board nets. **[VERIFIED: absence of confirmation]**
>
> Primary method: read the **VBAT sense network** — `VREF` = VBAT × 1 M/(300 K + 1 M) =
> **VBAT × 0.769**, on **GPIO26**. Interpretation:
> - VBAT ≥ ~4.15 V and slowly rising → on USB/charging.
> - VBAT ≤ ~4.10 V and falling → on battery.
> - **[ASSUMED]** these thresholds; they are Li-ion-typical but must be measured on the
>   real unit and made configurable.
>
> **Critical caveat [VERIFIED]:** GPIO26 is **ADC2_CH9**, and on classic ESP32 **ADC2 is
> unusable while WiFi is active** (Espressif documents this). Therefore the battery
> reading **must be taken before WiFi is brought up** (or after it is torn down), and
> the design must never read the ADC mid-session while connected. This constraint shapes
> the boot sequence (§5) and is a genuine architectural driver, not a detail.

> **HW-4** Firmware shall **probe at runtime** whether the GT30 font chip responds, and
> shall not depend on it (see HW-6). **[VERIFIED 2026-09-18 ON HARDWARE: the schematic
> silk reads `FONT - GT30L32S4W (Reserve)`, but the chip IS POPULATED on the ESP32-M1.**
> The probe reads the 8×16 ASCII glyph for `'A'` at `0x1DD990` and matches it
> byte-for-byte against LibDriver's reference. The chip has **no read-ID command** (only
> `0x03` read and `0x0B` fast-read), so presence is established by reading known content —
> a floating MISO cannot reproduce a real glyph. Data returns on **SPI_MISO = GPIO19**,
> wired to the GT30's pin 2 (`SO`).]

> **HW-5** Panel driving shall **preserve the vendor's exact undocumented init
> register sequence**. **[VERIFIED]** The public SSD2677 datasheet (Rev 1.1) documents
> only ~20 commands; the vendor's `EPD_init()` writes `0x00 PSR`, `0x30`, `0x62 HTOTAL`,
> `0x65 GSST`, `0xE0`, `0xE6`, `0xE7`, `0xE9`, **none of which appear in the datasheet**.
> They are vendor-proprietary. The firmware must reuse them verbatim, may not
> "clean up" the driver from the datasheet, and must be able to reproduce the vendor
> demo's output exactly as a regression baseline.

### 2.4 Framebuffer format

**[VERIFIED]** The vendor demo's image arrays are `unsigned char gImage_N[78200]` =
920 × 680 / 8, **1 bit per pixel, MSB-first** (78,200 bytes = 76.4 KiB). The demo then
expands 1 bpp → **2 bpp (4 grey levels)** before writing register `0x10` (DTM), which the
SSD2677 datasheet confirms is a 2-bit-per-pixel buffer.

> **HW-6** The rendering pipeline's canonical on-wire format shall be **1 bpp, 78,200
> bytes, MSB-first, row-major**. A 4-grey 2 bpp buffer (152.8 KiB) exceeds comfortable
> headroom alongside WiFi/TLS buffers on a 320 KB part **[ASSUMED: must be measured]**;
> 4-grey output, if used, shall be produced via the demo's 1→2 bpp expansion so only one
> 1 bpp buffer need exist in RAM.

### 2.5 SPI and transfer performance

> **HW-7** **[VERIFIED: the SSD2677 datasheet's entire SPI timing table reads "TBD"]**
> There is **no published maximum SPI clock**. The vendor demo uses **bit-banged
> software SPI**, which is very slow for a 78,200-byte frame.
>
> Plan: bring up the driver on **software SPI first** (proven, matches the demo), then
> move to **hardware SPI** as a measured, incremental optimization with a known-good
> reference image and a visible failure signal. The hardware-SPI clock is **[OPEN]** and
> shall be a configurable constant tuned against real hardware, defaulting conservatively.

---

## 3. System architecture

### 3.1 Two deliverables, one contract

```
 ┌────────────────────────┐         ┌───────────────────────────────┐
 │  WEB CONFIG APP        │         │  ESP32 FIRMWARE               │
 │  (browser, offline-cap.)│        │  (ESP32-M1)                   │
 │                        │         │                               │
 │  • 920×680 layout canvas│  HTTP  │  • config API (REST)          │
 │  • drag / resize       │ <─────> │  • NVS persistent storage     │
 │  • data binding        │  /api   │  • data sources: OWM, HA      │
 │  • alert rules         │         │  • renderer                   │
 │  • pages               │  Files  │  • EPD driver                 │
 │  • SAVE / LOAD files   │ <─────> │  • web UI (same app, built-in)│
 └────────────────────────┘         └───────────────────────────────┘
        (also served BY the device — user chose "both")
```

### 3.2 Rendering architecture — the load-bearing decision

The user chose: **static pre-render + on-device values**, while explicitly asking to
*"retain the ability to do on-device easily if this becomes preferable in the future."*

> **FR-1 — Renderer abstraction.** The system shall define a **renderer interface** with
> (at least) two implementations behind it:
> 1. **Composite renderer (primary):** the web app pre-renders the *static* layer
>    (background, frames, labels, icons, static text) to a 1 bpp 78,200-byte bitmap,
>    uploads it once; the firmware stores it and blits it each refresh, then draws only
>    the *dynamic value fields* on top.
> 2. **Full on-device renderer (deferred):** the firmware consumes the layout spec and
>    renders everything itself, including fonts and text layout.
>
> No code outside the renderer interface may assume which implementation is active.

> **FR-2 — Field model.** A layout is a set of **widgets**, each with a geometry
> (x, y, w, h), a **data binding**, and a **rendering role** (static or dynamic). Widgets
> marked dynamic must be re-drawn on every refresh from current data.

> **FR-3 — Value drawing.** On-device dynamic value drawing shall support at minimum:
> numeric and text values, alignment (left/centre/right, top/middle/bottom), a
> configurable font size, and a "fit to box" mode that shrinks text to fit its widget
> rather than overflowing. **[ASSUMED: this is the minimum for the listed use case.]**

### 3.3 Fonts

The user requires fonts that *"look extremely good"* and pointed at the board's GT30
font chip.

> **HW-6 / FR-4** The GT30L32S4W is marked **"(Reserve)"** on the schematic — a
> reserve/DNP footprint — so it **cannot be assumed present**. The firmware shall:
> 1. **Probe** for it at boot and cache the result;
> 2. **Use it only if present** and only for glyphs it actually provides;
> 3. **Always retain a built-in flash-resident font path** that works with the chip
>    absent.

> **FR-4a — Font strategy (DECIDED). [VERIFIED: library characteristics]**
> **Pre-rendered 1-bit bitmap font atlases, generated at build time, baked into the
> firmware and used by the web app's preview.** Rationale and specifics:
> - **Why not a runtime font engine:** the panel is 1-bit (HW-6) at 198 dpi, where
>   **anti-aliasing is impossible** — grey fringes become visible dots. The crispest
>   result comes from glyphs **hinted and rasterised for the exact pixel size** once, at
>   build time, then blitted. A runtime TTF rasteriser burns RAM/flash and produces
>   softer output. **[ASSUMED — strong engineering judgment, not a datasheet claim.]**
> - **Why the atlas approach beats a generic library:** `U8g2` **[VERIFIED: widely used
>   for monochrome displays; Unicode-capable]** has excellent built-in fonts but pulls in
>   a rendering model the vendor's undocumented EPD init does not need. Baking our own
>   atlases keeps the firmware free of a display library that could fight the vendor
>   sequence (HW-5) and lets the **exact same atlas** drive the web-app preview, which is
>   what makes NFR-4 ("preview matches panel bit-for-bit") achievable.
> - **Atlas generation:** a build step converts chosen faces (Inter / IBM Plex / Roboto
>   class — clean, high-x-height, superb at small sizes **[ASSUMED]**) into 1-bit atlases
>   for a fixed ladder of sizes tuned to 198 dpi (e.g. body ≈ 18–22 px, value ≈ 56–72 px).
>   Threshold, do **not** dither, for text — dithering makes type look dirty.
> - **The GT30 chip, if populated, is an accelerator only** — never a dependency.
>
> **FR-4b** Only glyphs actually used by a configuration need to be present, so atlases
> may be **subset per configuration** at push time to save flash. **[ASSUMED: defer until
> flash pressure is measured; full ASCII+latin-1 first.]**

### 3.4 Data sources — pluggable

> **FR-5** The firmware shall define **one data-source interface** with **two
> interchangeable implementations**: **Home Assistant REST** and **MQTT**. The user
> chose "both, pluggable" and both shall exist; REST ships first. Swapping must be a
> config change, not a code change.

> **FR-5a — HA REST, and the key optimisation. [VERIFIED]**
> - Auth is `Authorization: Bearer <long-lived access token>`, obtained from the HA user
>   profile page. `GET /api/states/<entity_id>` returns one entity as
>   `{entity_id, state, last_changed, last_updated, attributes{...}}`; a **404 means the
>   entity does not exist** — which gives the web app a clean way to *validate* an entity
>   id the user types (FR-23).
> - **`GET /api/states` returns *every* entity in one array.** That is one round-trip but
>   the payload is large (hundreds of entities) and would be a needless memory spike on a
>   320 KB part. **Use it only in the web app** (for entity discovery), never on-device.
> - **The on-device design: `POST /api/template`.** HA will render a template server-side
>   and return the result as plain text. So the device sends **one small POST** naming
>   exactly the entities its current layout needs, and receives **one small response** —
>   e.g. `{{ states('sensor.upstairs_hallway_temperature') }}|{{ states('sensor.64b708cfe0fc_sensor_2_temperature_f') }}`
>   ⇒ `68.4|41.2`. This is strictly better than N per-entity GET calls and far better than
>   pulling `/api/states`. **The device derives the template from its own layout's bound
>   entities, so adding a widget needs no firmware change.** JSON output is also possible
>   via a template that emits a JSON object, if structured values are preferable.
> - HA weather forecasts are available via
>   `POST /api/services/weather/get_forecasts?return_response` with
>   `{"entity_id": "...", "type": "daily"}` **if** the user later wants HA-sourced
>   forecasts alongside OpenWeatherMap. **[Not required for this phase.]**
>
> **FR-5b — Timestamp hygiene.** HA `state` values are **strings**, and a sensor can be
> `"unavailable"`/`"unknown"`. The data layer shall therefore validate, not cast blindly,
> and shall carry `last_updated` through so the layout can show and the alert logic can
> detect **stale** data. **[ASSUMED — this is the main correctness trap in consuming HA.]**

> **FR-5c — MQTT, and its real cost. [VERIFIED]** An HA entity's state is **not**
> published to MQTT by default. The user must enable the **`mqtt_statestream`**
> integration (plus the `mqtt` integration, i.e. a broker such as Mosquitto). Its
> documented topic form is **`base_topic/domain/entity/state`** — e.g.
> `homeassistant/sensor/upstairs_hallway_temperature/state` — with optional
> `publish_attributes` and `publish_timestamps` adding sibling topics
> (`.../last_updated`, and one topic per attribute).
> - Values are **JSON-serialised before publishing**, so a string state arrives quoted
>   (`"on"`). The parser must account for this.
> - **`mqtt_statestream` is flagged in HA's own docs as a *legacy* integration**
>   ("community maintained"). **[VERIFIED]** The spec shall therefore **not depend on it
>   as the only path** — which is exactly why REST ships first and MQTT is the pluggable
>   alternative (FR-5). The setup guide shall state the HA-side configuration the user
>   must add.
> - **Deep-sleep fit [ASSUMED, engineering judgment]:** MQTT needs a persistent broker
>   session, and a device that sleeps has to reconnect each wake — so MQTT's push
>   advantage largely evaporates in battery mode and it is **most useful in always-on
>   mode**. This informs FR-5's "REST first" ordering and shall be stated in the docs
>   rather than discoverered later.

> **FR-6 (OpenWeatherMap)** The provider shall support **both** OpenWeatherMap products —
> **One Call API 3.0** and the legacy free **Current Weather + 5-day/3-hour Forecast**
> — selectable by **config toggle**, and shall **auto-detect from the API key where
> possible** (probe One Call 3.0; fall back on the documented not-subscribed response).

> **FR-6a [VERIFIED]** The primary endpoint is
> `https://api.openweathermap.org/data/3.0/onecall?lat={lat}&lon={lon}&exclude={part}&units={units}&appid={KEY}`.
> - `lat`/`lon` **required**; `units` optional (`standard`|`metric`|`imperial`;
>   **`imperial` gives °F and mph**, which is what the user's entities use).
> - Returns **current weather, minute (1 h), hourly (48 h), daily (8 d), and
>   government weather alerts** — so **alerts come free with the same single call**,
>   which is why One Call 3.0 is the preferred product (FR-14).
> - **One Call 3.0 requires the separate "One Call by Call" subscription, which includes
>   1,000 calls/day free but requires a card on file.** **[VERIFIED]** At a 10–15 minute
>   refresh that is ~96–144 calls/day, comfortably inside the free tier. The firmware
>   shall nonetheless **count and cap daily calls** and surface the count in `/api/status`,
>   so a bug cannot silently burn the quota.
> - The free legacy endpoints (Current Weather `data/2.5/weather`, 5-day/3-hour forecast
>   `data/2.5/forecast`) require no card but carry **no official alerts** — the exact
>   trade-off FR-7 handles.

> **FR-7** Official severe-weather alerts depend on One Call 3.0. If the configured
> product does not provide them, the alert feature shall degrade explicitly (state that
> official alerts are unavailable) rather than silently showing nothing.

### 3.5 Power and sleep

> **FR-8** Firmware shall support **both** modes, user-selectable / auto-detected
> (HW-3): **deep-sleep battery mode** and **always-on mains mode**. Battery detection
> happens **before WiFi init** (HW-2 caveat).
>
> Auto-detection is **inferred** and therefore fallible: **[VERIFIED — schematic]** this
> board has no confirmed USB-present or charge-status pin (the VBUS net and the LTC4054's
> `CHRG` output exist, but neither destination GPIO resolves), so the mode is derived from
> the VBAT divider alone. The configuration shall therefore carry an explicit
> **`powerMode`** of `'auto' | 'always-on' | 'battery'` (default `'auto'`) so the user can
> override a wrong guess. Detection shall fail **toward battery**, because sleeping too
> eagerly costs a slower refresh while failing to sleep costs the entire battery. The
> residual limitation is inherent to inferring from one signal: a pack held **flat** at
> or above the threshold is indistinguishable from mains, so a full cell whose voltage
> is not measurably falling will read as USB. That case is what the `powerMode` override
> exists for; the trend term only separates the two when the pack is actually drooping.

> **FR-9** In deep-sleep mode the device shall wake on a configurable interval, and the
> boot path shall be ordered: **read VBAT (ADC2, WiFi off) → connect WiFi → fetch →
> render → update panel → sleep**, with the panel update and sleep guaranteed even if a
> fetch fails (§5).

### 3.6 E-ink lifetime

The user explicitly requires proper lifetime preservation *and* demo-like speed/quality.

> **FR-10** **[VERIFIED — panel datasheet §7.5]** The panel **must be refreshed at least
> every 24 hours** or "Ghosting"/"Image Sticking" may occur. The firmware shall enforce
> a **daily full refresh** regardless of the configured update interval.

> **FR-11** The firmware shall implement a **partial-refresh-with-periodic-full-refresh**
> strategy: mostly partial updates for speed and low flicker, with a full refresh after a
> configurable number of partials **[ASSUMED default 5, per the vendor demo's comment]**
> and at the daily boundary (FR-10).

> **FR-12** The firmware shall **always issue the deep-sleep command** after an update
> and shall re-initialise before every full update, per the vendor's documented
> requirements **[VERIFIED]**. It shall not leave the panel powered between updates.

> **FR-13** The firmware shall read the controller's internal temperature (command
> `0x40`, which returns °C directly per the datasheet) and apply the vendor's
> temperature-compensated waveform selection, preserving the demo's behaviour.
>
> **[MEASURED 2026-09-18]** `0x40` on this unit returns a constant −15 °C regardless of
> ambient, so the waveform it selects is a *fixed* cold-temperature LUT (the `<=5 °C`
> entry, value 232), not a temperature-tracking one. The vendor demo has the same
> exposure and simply never noticed. This is safe — the LUT ladder is monotone and the
> cold entry is a valid waveform — but it is **not** compensation, and the code must not
> claim it is. Revisit if a working ambient source (external I²C sensor, or a live
> internal reading on a different unit) becomes available. See HW-1.

### 3.7 Alert bars

> **FR-14** The system shall support **two** alert mechanisms, both per the user's choice:
> 1. **User-defined threshold rules** per bound field (e.g. *temp_f > 100 → red bar*,
>    *wind_mph > 25 → amber bar*), editable in the web UI, working for **any** bound
>    source including Home Assistant entities.
> 2. **OpenWeatherMap official severe-weather alerts** rendered when present (subject to
>    FR-7).
>
> Alert rendering shall be a first-class widget type with configurable severity levels,
> colours (mapped to grey levels on a mono panel), and text.

### 3.8 Paging

> **FR-15** The system shall support **multiple pages**, each with its own layout,
> bindings, and refresh interval. **[VERIFIED: user chose scheduled auto-rotation]** The
> device shall cycle pages automatically on a configurable schedule.

> **FR-16** Paging must be optional; a single-page configuration shall behave identically
> to today's requirement.

### 3.9 Initial layout (this phase's deliverable)

> **FR-17** The project shall ship a **default layout** implementing the user's stated
> intent: upstairs hallway temperature, outside coop temperature, current conditions,
> forecast, and location/zip display — with sensible alert rules pre-configured.

---

## 4. Web configuration app

### 4.1 Delivery

> **FR-18 [VERIFIED: user chose "both"]** The app shall be buildable two ways from one
> codebase:
> 1. **Local dev:** `npm run dev` for rich, fast editing against a device on the LAN.
> 2. **Embedded:** the production build is flashed into the ESP32 and served by the
>    device itself at its IP, with no install.

> **FR-19** Both modes shall share the same API client contract so behaviour is identical.

> **FR-19a — Framework (DECIDED): vanilla TypeScript + Vite, no UI framework.**
> Rationale:
> - The app is **one screen with one canvas** plus a property panel. A component
>   framework (React/Vue/Svelte) adds a build/runtime dependency and bundle weight for
>   little benefit here, and bundle weight matters because this build must also be
>   **served from an ESP32's 4 MB flash** (FR-18).
> - The hard part is canvas interaction (drag, resize, snapping), not component
>   composition. **A small purpose-built interaction layer over a `<canvas>` is more
>   predictable than fighting a DOM-based drag library** for pixel-exact 1-bit output.
> - **Vite** gives fast dev with HMR (`npm run dev`) and a small production build with
>   an easy single-file asset step for embedding in firmware. **[VERIFIED: Node 26 /
>   npm 11 present locally.]**
> - Escape hatch recorded: because the state layer is plain TS + a typed config model
>   (§IF-1), a framework could be introduced later without rewriting the model.
>
> **[ASSUMED — this is a judgment call, flagged deliberately.]** If the user prefers
> React, React + `react-rnd` is the natural alternative; nothing else in the spec depends
> on this choice.

### 4.2 Editor requirements

> **FR-20** The editor shall present a **fixed 920 × 680 pixel canvas** representing the
> panel exactly, with zoom/pan, and shall render a truthful 1-bit preview (what you see
> is what the panel shows).

> **FR-21** Widgets shall support **drag**, **resize** (edge/corner handles), **grid
> snapping**, **alignment guides**, and a **z-order** control.

> **FR-22** A **property inspector** shall expose, per widget: geometry, data binding,
> static/dynamic role, font/size/alignment, formatting (units, decimals, prefixes),
> and alert rules.

> **FR-23** The app shall support **data binding** to: OWM current fields, OWM forecast
> fields, OWM alerts, and arbitrary Home Assistant entities (chosen by entity id, with a
> discovery/validation step against the configured HA instance).

> **FR-24** The app shall include a **map picker** for precise lat/lon, and a **zip code**
> field for general location.

> **FR-25** The app shall manage **multiple pages** and their rotation schedule.

> **FR-26** The app shall **save and load configurations as local files**, and push a
> configuration to the device. **[VERIFIED: user requirement]**
>
> **FR-26a — Format: download/upload JSON, not the File System Access API. [VERIFIED]**
> MDN classes `showSaveFilePicker()`/`showOpenFilePicker()` as **"Limited availability …
> not Baseline … Experimental"**, requiring a **secure context (HTTPS)** and transient
> user activation. That rules it out as the primary mechanism for a device served over
> plain HTTP on a LAN IP. **Design:** a versioned `.json` config downloaded/uploaded via
> a normal anchor/`<input type=file>`, which works everywhere; optionally *enhance* with
> the File System Access API **only when `window.showSaveFilePicker` exists** (progressive
> enhancement, never required). `localStorage` holds working state and recent configs.
>
> **FR-26b** A saved config file shall be self-describing: it embeds `schemaVersion` and
> a `generator`/`createdAt` stamp so a future version can migrate it or refuse it clearly
> rather than silently mis-reading it.

> **FR-27** The app shall show a **live or on-demand preview** rendered with real fetched
> data before pushing to the device.

---

## 5. Firmware behaviour

### 5.1 Boot / wake sequence

> **FR-28** The boot path shall be, in order:
> 1. Initialise NVS, load config.
> 2. **Read VBAT sense (GPIO26) with WiFi off** → decide power mode (FR-8, HW-3).
> 3. Initialise panel and render the *last known* state **or** a "connecting" state
>    (so a network failure never leaves a stale/blank panel).
> 4. Bring up WiFi (station; captive portal if unconfigured).
> 5. Fetch all enabled sources (with per-source timeout and retry).
> 6. Render the current page and update the panel (partial or full per FR-11).
> 7. Persist any needed state; deep-sleep if in battery mode.

> **FR-29** **Every** failure mode shall leave the panel showing the last good image
> plus a visible, discreet error indicator — never a blank panel or a wedged device.

### 5.2 Provisioning

> **FR-30 [VERIFIED: user chose Option 1 + BLE]** The device shall support:
> 1. **Captive-portal provisioning** on first boot / unconfigured state — a WiFi AP where
>    the user enters WiFi credentials, the OWM API key, the HA URL and token, and
>    location. Values persist in **NVS**.
> 2. **The official ESP BLE Provisioning mobile app**, current version with full update
>    support.
>
> No credentials shall ever be committed to git.

### 5.3 Config API

> **FR-31** The device shall expose a REST API for the web app: read config, write
> config, upload the static bitmap, upload per-page layouts, trigger a refresh, read
> status (last fetch, last refresh, battery voltage, RSSI, firmware version, errors).
> Exact endpoints in §6.

### 5.4 OTA

> **FR-32** The device shall support **OTA firmware update**, authenticated, with
> rollback protection (a failed new image shall fall back to the previous working one).

### 5.5 Diagnostics

> **FR-33** The device shall expose: firmware version, uptime, free heap (minimum
> watermark), WiFi RSSI, last N errors, panel refresh counts (partial/full), and battery
> voltage history — enough to diagnose a field failure remotely.

---

## 6. Interfaces (exact contracts)

> **IF-1 Config document (JSON).** The canonical configuration is a single versioned JSON
> document owned by the web app and stored in device NVS. It shall carry a `schemaVersion`
> and be forward/backward compatible by explicit migration. Full schema in
> `docs/specs/config-schema.md` (to be written with the implementation).

> **IF-2 Static bitmap transfer.** 1 bpp, 78,200 bytes, MSB-first, row-major.
>
> **IF-2a — Mechanism (DECIDED): chunked HTTP upload, with the SD card as an offline
> path.**
> - Primary: `POST /api/bitmap` with a **chunked** body (e.g. 4 KB per request, with an
>   offset/index and a finalising commit), so a 76.4 KiB upload never needs to be buffered
>   whole on a part with ~320 KB SRAM. The device streams each chunk to flash (a dedicated
>   raw partition or a filesystem file), verifies a checksum, then atomically marks the new
>   bitmap active. **An interrupted upload must leave the previous bitmap intact.**
> - Secondary (offline / no-network): the same file can be written to the **microSD**,
>   whose socket is already present and case-accessible. This exercises HW-2's bus
>   arbitration and gives a way to update a device that will not join the network.
> - **[ASSUMED: chunk size and storage target to be confirmed once flash partitioning is
>   designed; the requirement is the streaming + atomic-swap behaviour, not the exact
>   number.]**

> **IF-3 Data-source interface.** `fetch(source) -> {value, unit, timestamp, error}`
> semantics, identical for OWM, HA-REST and MQTT, so widgets are source-agnostic.

> **IF-4 REST endpoints.** `GET/PUT /api/config`, `POST /api/bitmap`, `POST /api/refresh`,
> `GET /api/status`, `POST /api/provision`, `POST /api/ota` — final naming and payloads
> fixed during implementation, documented in `docs/specs/api.md`.

---

## 7. Non-functional requirements

> **NFR-1 Reliability.** The device shall run for weeks unattended and recover
> automatically from WiFi loss, DNS failure, TLS failure, and API errors, without a power
> cycle. This is the user's explicit complaint about the vendor demo.

> **NFR-2 Memory.** Firmware shall operate within the ESP32-WROOM-32D's ~320 KB SRAM with
> **no PSRAM**, maintaining a documented minimum free-heap watermark under worst-case
> (TLS + WiFi + render) load. A full 1 bpp framebuffer is 76.4 KiB.

> **NFR-3 Power.** Deep-sleep current shall approach the panel's 1–3 µA floor with the
> panel and radios off; the firmware shall not leak power in sleep.

> **NFR-4 Display quality.** Text shall be crisp at 198 dpi with no anti-aliasing
> artifacts; the preview in the web app shall match the panel output bit-for-bit.

> **NFR-5 Lifetime.** Panel refresh strategy shall satisfy FR-10…FR-13.

> **NFR-6 Testability.** All pure logic (data model, layout maths, alert evaluation,
> formatting, config migration, bitmap generation) shall be **unit-testable on the host
> without hardware**. Hardware paths shall be isolated behind thin interfaces.

> **NFR-7 No silent assumptions.** Every requirement carries a test that fails when the
> requirement is not met — "done" is defined by the tests, not by assertion.

---

## 8. Repository layout

**[VERIFIED: user chose the current directory]**

```
eink_weather/                  <- git root
  code/                        <- working dir
    ESP32-M1-SCH-*.pdf         (reference, unchanged)
    SSD2677(Rev1.1)N_driver_ic.pdf
    GDEH0576T81_Arduino_demo_code/   (reference, unchanged)
    firmware/                  <- NEW  PlatformIO project
    webapp/                    <- NEW  web config app
    docs/
      specs/                   <- NEW  this spec + schema/api docs
      plans/                   <- NEW  implementation plan
```

---

## 9. Tech stack

### 9.1 Firmware framework — DECIDED: ESP-IDF

**[VERIFIED]** The decision is **`framework = espidf`** in PlatformIO. Evidence:

1. **The user's crash report is real and documented.** The Arduino ESP32 TLS failures are
   not anecdote — they are a well-known class of bug with a known mechanism. Multiple
   primary issue reports on `espressif/arduino-esp32` show exactly this signature:
   - **#566** — *"Any HTTPS connection results in a stack overflow error … `***ERROR*** A
     stack overflow in task loopTask has been detected`"* the moment `client.connect()`
     runs.
   - **#1025** — *"Latest pull causing core panic and stackoverflows"*, stack overflow in
     `loopTask`.
   - **#1260** — *"SSL Handshake still an issue … Guru Meditation Error: Core 1 panic'ed
     … Stack canary watchpoint triggered (loopTask)"* during the TLS handshake.
   - **#211** — crash/panic after a TLS handshake fails, backtrace through
     `start_ssl_client` → `WiFiClientSecure::connect` → `loopTask`.
   **Root cause [ASSUMED, well-supported]: the Arduino `loopTask` runs with a small
   default stack (~8 KB) and the TLS handshake needs more.** Arduino's `loop()` model
   gives the developer no natural place to size that stack. This is precisely why the
   vendor demo appears to "crash in WiFi/SSL/webserver".

2. **PlatformIO's official ESP32 platform does not support the current Arduino core.**
   **[VERIFIED]** `platformio/platform-espressif32` issue **#1225**: *"Arduino 3.x is not
   officially supported by PlatformIO"* — the official platform remains on Arduino core
   2.x. The community workaround is the **`pioarduino` fork**
   (`https://github.com/pioarduino/platform-espressif32`), currently tracking **Arduino
   3.3.11 / ESP-IDF 5.5.5**. So an Arduino-based build would mean either depending on a
   third-party fork or pinning to a 2.x core — i.e. deliberately building on the older
   stack that has the crash bugs. **ESP-IDF, by contrast, is natively and fully supported
   by PlatformIO with no fork.**

3. **Every requirement maps to a first-party ESP-IDF component. [VERIFIED]**
   - TLS + HTTPS + certificate bundle → `esp-tls` / `esp_http_client` (+ baked CA bundle).
   - MQTT → `mqtt` client component.
   - HTTP server for the config API + device-served web UI → `esp_http_server`.
   - OTA with rollback → `esp_https_ota` + the standard dual-OTA partition table.
   - **BLE provisioning for the official app** → the `wifi_provisioning` component, which
     is *the* component Espressif's own **ESP BLE Provisioning** app (App Store / Play
     Store, "IDF v3.2 and later") is designed to talk to. This is a direct match for the
     user's requirement — no library hunting.
   - Deep sleep → `esp_sleep`.
   - Persistent config → `nvs_flash`.
   - JSON → `cJSON` (bundled).

4. **The crash class is structurally avoided, not just avoided by luck.** In ESP-IDF the
   firmware controls task creation and therefore the **stack size of every task**. The
   network work runs in a task sized for TLS, so the #566/#1260 failure mode cannot occur
   by default. This directly addresses NFR-1 and the user's stated complaint.

5. **The driver work is framework-neutral.** **[VERIFIED]** Because the vendor's EPD init
   registers are undocumented (HW-5), the firmware must *reuse the vendor's byte
   sequence* rather than adopt a third-party display library. So the usual "Arduino has
   more libraries" argument does not apply here — there is no library that helps.

6. **Testability [VERIFIED]** matches NFR-6/NFR-8 directly: ESP-IDF ships the **Unity**
   test framework and a **pytest**-driven on-device test runner, and its components can
   be built for the host, enabling the off-device unit tests the spec requires.

> **FR-35** Firmware shall be a PlatformIO project using `framework = espidf`, targeting
> the `esp32dev` board (ESP32, 4 MB flash, 320 KB RAM **[VERIFIED: matches `pio boards`]**).
> The ESP-IDF version shall be pinned explicitly in `platformio.ini` for reproducibility.
>
> **[VERIFIED: `pio pkg show platformio/framework-espidf`]** versions available to
> PlatformIO are `4.60100.0` (= ESP-IDF **6.0.1**, latest), `4.60000.0` (6.0.0),
> `3.50503.0` (**5.5.3**), `3.50400.0` (5.4.0), … Pin to the newest **5.5.x** line
> (`3.50503.0`) for this project's first release — it is mature and well inside
> component support — and treat 6.0.x as a later, deliberately-tested upgrade. **The
> pinned version is a requirement, not a footnote: an unpinned toolchain is exactly how
> a project silently changes behaviour between builds.**

> **FR-36 (counterargument, recorded deliberately)** ESP-IDF has a steeper learning curve
> and a smaller third-party-library ecosystem than Arduino. This is accepted because
> (a) the needed functionality is all first-party, (b) the display driver must be
> vendor-derived anyway, and (c) ecosystem convenience is what produced the instability
> the user is trying to escape. **If Arduino convenience is ever genuinely needed, the
> escape hatch is `framework = arduino, espidf`** — Arduino-as-a-component under ESP-IDF
> **[VERIFIED: supported by PlatformIO]**, which preserves ESP-IDF's task control while
> allowing specific Arduino libraries. This is the documented fallback, not the default.

> **FR-37** Web app: modern JS toolchain (Node 26 / npm 11 available locally
> **[VERIFIED: local environment]**). Framework choice **[OPEN — §11 Q5]**.


---

## 10. Testing strategy (defined before code)

> **NFR-8** Every requirement below shall have an automated test before it is considered
> implemented (TDD). Tests are grouped:
>
> 1. **Host unit tests (firmware, pure logic)** — run on the developer machine with no
>    hardware. Cover: config schema + migration, data-source parsing (recorded OWM/HA/MQTT
>    fixtures), alert-rule evaluation, unit/format logic, widget geometry, 1 bpp bitmap
>    encoding, page-rotation scheduling, VBAT→voltage maths.
> 2. **Hardware-in-the-loop tests (on device, manual/scripted)** — panel init produces the
>    vendor reference image; partial vs. full refresh; sleep current; WiFi reconnect
>    soak; OTA rollback; provisioning.
> 3. **Web app tests** — component tests for widgets/editor, plus end-to-end tests for
>    drag/resize/bind/save/load/push.
> 4. **Contract tests** — the web app and firmware agree on the config document and API
>    (shared fixtures, so schema drift fails a test rather than a device).
>
> **NFR-9** A **golden-image test** shall pin the firmware's rendering of the default
> layout: a known config + known fixture data ⇒ an exact expected 78,200-byte bitmap.
> Any rendering regression fails loudly.

---

## 11. Open questions

| # | Question | Status / fallback |
|---|---|---|
| Q1 | Font strategy | **RESOLVED — pre-rendered 1-bit atlases** (FR-4a); GT30 as optional accelerator only. |
| Q2 | Config save/load mechanism | **RESOLVED — download/upload JSON** (FR-26a); File System Access API as optional progressive enhancement only. |
| Q3 | Static bitmap transfer | **RESOLVED — chunked HTTP with atomic swap** (IF-2a); microSD as offline path. |
| Q4 | Firmware framework | **RESOLVED — ESP-IDF** (§9.1). Escape hatch: `framework = arduino, espidf`. |
| Q5 | Web app framework | **RESOLVED — vanilla TS + Vite** (FR-19a); React + react-rnd is the recorded alternative. |
| Q6 | GT30 font chip CS GPIO | **RESOLVED 2026-09-18 — GPIO15 (`A13`)**. Resolved by rendering the schematic at 3000 DPI and aligning the `SPI_CS_GT30` wire's x-position against the module's bottom-pin numbers, then cross-checking the alias: the pin-23 wire carries `IO15` / `A13` / `T3` / `SPI_CS_GT30`, and `pins_arduino.h` confirms `A13 = 15`. The same method reproduces the already-verified BUSY pin (A14=13), which is the control. The chip remains **DNP/reserve** — the probe still returns 0 when absent. |
| Q7 | Exact VBAT thresholds for battery/USB discrimination | **Open by design** — default 4.15 V / 4.10 V, configurable; measure on the real unit (HW-3). |
| Q8 | Is the `CHRG` charge-status net usable, and on which GPIO? | **Open** — fallback: ignore it, rely on the VBAT divider (HW-3). |
| Q9 | Chunk size + storage target for the bitmap | **Open** — resolve during flash-partition design; the streaming/atomic requirement stands (IF-2a). |
| Q10 | Font faces and the exact size ladder for 198 dpi | **Open** — pick during implementation against a printed proof; candidates in FR-4a. |

**Questions genuinely needing the user (asked 2026-09-18, resolved above):**
rendering split (→1), app hosting (→both), power (→both + VBAT detect), alerts (→both),
HA method (→both pluggable), repo layout (→`code/`), secrets (→captive portal + BLE),
paging (→scheduled rotation), project path (→`code/`), OWM product (→both + toggle).

---

## 12. Sources and evidence

- Panel datasheet: `docs/GDEH0576T81_display.pdf` (rev 1.0, 2025-04-11, 17 pp) — geometry
  §1, FPC pinout §1.5, electrical §2, optical §3, precautions/lifetime §7.
- Controller datasheet: `code/SSD2677(Rev1.1)N_driver_ic.pdf` (Rev 1.1, Aug 2023, 19 pp)
  — command table §4.5 pp.7-11, OTP/VCOM pp.9-11, AC timing §9.1 p.15 (all TBD).
- Board schematic: `code/ESP32-M1-SCH-20260307.pdf` (Altium, 2026-05-12) — pin nets,
  VBAT divider, GT30 "(Reserve)" annotation.
- Vendor demo: `code/GDEH0576T81_Arduino_demo_code/` — init sequence, 1 bpp 78,200-byte
  image arrays, temperature LUT selection, sleep discipline.
- Mechanical reconstruction + accuracy caveats: `GoodDisplay_ESP32_M1_RESEARCH*.md`,
  `GoodDisplay_ESP32_M1_reconstructed_v2/`.
- Framework evidence (§9.1):
  - Arduino-ESP32 TLS stack-overflow reports: `espressif/arduino-esp32` issues
    **#566, #1025, #1260, #211**.
  - PlatformIO Arduino-core-3.x support gap: `platformio/platform-espressif32`
    issue **#1225**; community fork `pioarduino/platform-espressif32` (Arduino 3.3.11 /
    ESP-IDF 5.5.5).
  - PlatformIO Espressif32 docs — `framework = arduino | espidf`, and Arduino-as-component.
  - ESP BLE Provisioning app listings (App Store / Google Play) — "BLE based Wi-Fi
    Provisioning from IDF v3.2 and later".
- **Still outstanding (§§ 4.2, 6, 11 Q1/Q2/Q3/Q5):** web-app framework, font strategy,
  config file format, OpenWeatherMap / HA REST-vs-MQTT specifics, bitmap-transfer
  mechanism.
