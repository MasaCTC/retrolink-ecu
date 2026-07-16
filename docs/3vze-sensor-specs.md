# 1995 Toyota Pickup 3VZ-E DLX — Sensor Specifications

Reference document for RetroLink ECU breakout project.
Sources: Toyota FSM excerpts, TCCS Training Manual, DIYAutoTune, 4crawler.com, forum cross-references.
Confidence levels noted — verify low/medium items on the bench.

---

## Coolant Temperature Sensor (ECT/THW)
- **Type:** 2-wire NTC thermistor (resistance drops as temp rises)
- **Part:** 89422-20010 (superseded by 89422-35010)
- **Pins:** THW (signal to ECU), E2 (sensor ground)
- **ECU pull-up:** ~5V reference through internal resistor (value unconfirmed — measure with sensor unplugged)
- **Note:** Separate from dash gauge sender — use the 2-pin ECU sensor, not the single-wire gauge unit

### Resistance vs Temperature (composite, high confidence)
| Temp °C | Resistance (Ω) |
|---------|----------------|
| -20     | ~15,000        |
| 0       | ~5,500–6,020   |
| 20      | ~2,200–2,500   |
| 40      | ~1,100         |
| 60      | ~550           |
| 80      | ~300–335       |
| 100     | ~230           |
| 120     | ~120–130 (est) |

### Estimated Voltage at ECU (assuming ~2.2kΩ pull-up, 5V ref)
| Temp °C | Voltage |
|---------|---------|
| -20     | ~4.4V   |
| 0       | ~3.6V   |
| 20      | ~2.7V   |
| 40      | ~1.7V   |
| 60      | ~1.0V   |
| 80      | ~0.6V   |
| 100     | ~0.5V   |
| 120     | ~0.3V   |

---

## Throttle Position Sensor (TPS)
- **Type:** 4-pin linear potentiometer + idle contact switch
- **Part:** 89452-12040
- **Pins:** VC (5V ref), VTA (wiper/signal), IDL (idle switch), E2 (ground)
- **Reference voltage:** 5V (spec: 4–6V)

### Voltage
| Condition       | VTA–E2 Voltage |
|-----------------|----------------|
| Closed (idle)   | 0.1–1.0V       |
| Wide open (WOT) | ~3.5V (FSM spec floor, may read higher) |

### Resistance
| Measurement     | Condition     | Value           |
|-----------------|---------------|-----------------|
| VTA–E2          | Closed        | 200–800 Ω       |
| VTA–E2          | WOT           | 3.3–10 kΩ       |
| VC–E2           | Full track    | 4.0–9.0 kΩ      |
| IDL–E2          | Closed (idle) | 0–100 Ω         |
| IDL–E2          | Off-idle      | Open / Infinite  |

### IDL Switch Voltage
| Condition    | IDL–E2 Voltage |
|--------------|----------------|
| Closed (idle)| ~0V            |
| Open (off-idle)| 8–14V       |

---

## Air Flow Meter (VAF — Vane Air Flow)
- **Type:** Mechanical flap/vane + potentiometer (NOT MAP, NOT Karman vortex)
- **Connector:** 7-pin: FC, E1, (unused), VC, E2, VS, THA
- **IAT sensor (THA) is integrated into the VAF housing**

### Signals
| Signal | Function                                    | Voltage Range          |
|--------|---------------------------------------------|------------------------|
| VC–E2  | 5V reference                                | 4–6V                   |
| VS–E2  | Airflow signal (wiper)                      | ~0.2–0.5V (no flow) → ~2.3–2.8V (idle) → 3.7–4.3V (full open) |
| FC/E1  | Fuel pump relay switch (closes when plate opens) | Switch contact    |
| THA–E2 | Intake air temp (NTC thermistor)            | See IAT below          |

---

## Intake Air Temperature (IAT/THA)
- **Location:** Built into VAF housing (not standalone)
- **Type:** NTC thermistor, same family as ECT
- **Approximate:** ~2–3 kΩ at 20°C, ~0.5–3.4V at 20°C, ~0.2–1.0V at 60°C
- **Confidence:** Medium — verify on bench

---

## Oxygen Sensor (O2/HO2S)
- **Type:** Narrowband zirconia, heated (4-wire)
- **Count:** 1 sensor (49-state federal); 2 sensors (California spec)
- **Voltage:** Rich ≈ 0.8–0.9V, Lean ≈ 0.1–0.2V, Crossover ≈ 0.45V

### Pins (4-wire heated)
| Pin | Wire Color   | Function       |
|-----|--------------|----------------|
| 1   | Pink/Green   | Heater ground  |
| 2   | Red/White    | Heater B+      |
| 3   | Black        | Signal         |
| 4   | Brown        | Sensor ground  |

---

## Knock Sensor
- **Type:** Piezoelectric resonant, tuned ~7.6 kHz
- **Count:** 1 (single sensor for V6, in valley under intake manifold)
- **Output:** AC millivolt signal proportional to vibration — no valid DC resistance spec
- **Torque:** ~33 ft-lb
- **Note:** Do NOT use ohmmeter resistance readings for diagnosis — use oscilloscope

---

## Vehicle Speed Sensor (VSS)
- **Type:** Electronic (3-wire: power/ground/signal square wave)
- **Location:** Transmission/transfer-case output shaft
- **Part:** 83181-35051
- **Pulses/rev:** ~4 (forum estimate — verify with oscilloscope)
- **Confidence:** Medium for type/location, Low for pulse specs

---

## Ignition System (Distributor + Igniter)
- **Distributor pickup coils:** 3 magnetic reluctor pickups
  - NE (white) — crankshaft position
  - G1 (red) — cylinder identification
  - G2 (black) — cylinder identification
  - G- (green) — common ground
- **Pickup resistance:** 125–250 Ω each (G1–G-, G2–G-, NE–G-)
- **IGT:** ~5V pulse from ECU to igniter (~20% duty cycle)
- **IGF:** 5V reference pulled to ground each coil fire (spark confirmation)
- **Fail-safe:** After 8–11 missed IGF confirmations, ECU cuts injectors

---

## Oil Pressure
- **DLX trim:** On/off warning-light switch (not variable sender)
- **SR5 gauge sender:** Separate part 83520-35031
- **Actuation threshold:** ~3.5–7 psi (unconfirmed for 3VZ-E specifically)

---

## EGR System
- **Type:** Vacuum-modulated EGR valve + VSV + EGR gas temperature sensor (NTC)
- **EGR temp sensor resistance (forum-sourced, medium confidence):**
  - 50°C: ~64–97 kΩ
  - 100°C: ~11–16 kΩ
  - 150°C: ~2–4 kΩ
- **Diagnostic code:** 71 = insufficient EGR flow

---

## Fuel Injectors
- **Type:** High-impedance (no ballast resistor needed)
- **Resistance:** 13.4–14.2 Ω (FSM-confirmed)
- **Part:** Denso 23250-65020
- **Drive:** Direct ECU low-side switching

---

## ECU Discrete Inputs/Outputs
| Signal | Function                           | Spec              |
|--------|------------------------------------|--------------------|
| STA    | Starter signal                     | 6–12V cranking     |
| N1/NSW | Neutral/Park switch (A/T)          | Closed in P/N      |
| A/C    | A/C clutch signal                  | Compressor voltage |
| SPD    | Vehicle speed to combo meter       | 6–12V pulse        |
| STP    | Brake/stop-light switch            | 8–14V applied      |
| 4WD    | 4WD engaged                        | Discrete input     |
| BATT   | Constant power via EFI main relay  | 10–14V             |
| C9     | Circuit opening relay (fuel pump)  | Closed during crank or AFM plate open |
| HT     | O2 sensor heater control           | ECU output         |
| TE1/TE2| Diagnosis/check terminals          | —                  |

---

## OBD-I Trouble Codes (relevant)
| Code | Meaning                    |
|------|----------------------------|
| 22   | ECT sensor circuit         |
| 24   | IAT sensor circuit         |
| 25   | Air-fuel ratio lean        |
| 26   | Air-fuel ratio rich        |
| 27   | Sub O2 sensor (CA only)    |
| 41   | TPS circuit malfunction    |
| 52   | Knock sensor circuit       |
| 71   | EGR system malfunction     |
