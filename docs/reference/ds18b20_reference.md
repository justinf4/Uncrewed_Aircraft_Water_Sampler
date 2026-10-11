# DS18B20 Temperature Sensor Reference

Manufacturer: Maxim Integrated (now Analog Devices, acquired 2021)
Datasheet: DS18B20 Rev 3 (document 19-7487)

---

## Specifications

| Parameter | Value |
|-----------|-------|
| Temperature range | -55°C to +125°C |
| Accuracy | ±0.5°C from -10°C to +85°C |
| Resolution (default) | 12-bit (0.0625°C per step) |
| Resolution (programmable) | 9-bit (0.5°C), 10-bit (0.25°C), 11-bit (0.125°C), 12-bit (0.0625°C) |
| Conversion time | 9-bit: 94 ms / 10-bit: 188 ms / 11-bit: 375 ms / **12-bit: 750 ms** |
| Protocol | 1-Wire (Maxim single-wire) |
| Supply voltage | 3.0 V – 5.5 V (normal mode) or parasite power from data line |
| ROM code | 64-bit unique ID: family byte `0x28` + 48-bit serial + CRC |
| Pull-up resistor | 4.7 kΩ typical on DQ line (reduce to 2.2–3.3 kΩ for long cables) |
| Package (bare) | TO-92 (3-pin through-hole) |
| Package (field) | Waterproof stainless probe with pre-attached 3-wire cable (third-party assembly) |

---

## The 85°C Power-On Default — What It Is and Why

When the DS18B20 first powers on, the internal scratchpad temperature register is loaded with
the fixed value **`0x0550` = 85°C**. This is documented in the datasheet (Table 2, Scratchpad
Memory, Power-on Reset column).

**Why 85°C?** Maxim chose it deliberately:
- It is the upper boundary of the guaranteed accuracy range, making it recognizable
- It is a physically plausible temperature (not obviously garbage), but unusual enough as an
  ambient reading to be treated as a sentinel
- It has a valid 1-Wire CRC, so bad code that doesn't check for it will silently accept it

If `getTempC()` is called before the first conversion completes, the register still holds
`0x0550` and the library returns 85.0°C. This is not a sensor fault — it is correct behavior
for an unconverted register.

**Fix in this project (two layers):**

1. Warmup in `setup()` — discards the default before the system is ready:
```cpp
while (ds.selectNext()) ds.getTempC();  // trigger first conversion
delay(800);                              // wait full 12-bit window
while (ds.selectNext()) ds.getTempC();  // discard again to be safe
```

2. Runtime guard in the sample read — rejects 85.0°C if it ever appears:
```cpp
if (fabsf(c - 85.0f) < 0.5f) {
    Serial.println("[TEMP] WARN: 85C default detected, discarding");
    c = NAN;
}
```
`NAN` is written as `0.00` in the CSV via the existing isnan() handler.

---

## DS18B20 vs DS18S20 vs DS1820 — Differences

| Feature | DS1820 | DS18S20 | DS18B20 |
|---------|--------|---------|---------|
| Status | Discontinued | Replacement for DS1820 | Current, actively sold |
| Family code | `0x10` | `0x10` | **`0x28`** |
| Resolution | Fixed 9-bit (0.5°C) | Fixed 9-bit (software trick to ~0.1°C) | Programmable 9–12 bit |
| Configuration register | No | No | Yes (byte 4) |

The DS1820 and DS18S20 share the same 1-Wire family code and are software-compatible.
The DS18B20 has a different family code and is a separate device — **this project uses DS18B20**.

---

## Counterfeit Sensors — A Real Problem

A large fraction of DS18B20 sensors sold on budget marketplaces (AliExpress, eBay) since
approximately 2018 are counterfeit. Signs of fakes:

- Report only 9–10 bit resolution regardless of configuration register setting
- Accuracy significantly worse than ±0.5°C (up to ±2°C or more)
- May have wrong family code (not `0x28`)
- Parasite power may not work reliably

**Mitigation:** Source from authorized distributors (Digi-Key, Mouser, Arrow). Verify the
ROM family code byte is `0x28` at startup.

---

## Field Deployment Notes

### Waterproof probe version
The waterproof stainless steel probe commonly sold with DS18B20 sensors is a **third-party
assembly** — not an official Maxim product. Key considerations:

- Cable adds resistance (VDD line) and capacitance (DQ line). For probes > 1 m, reduce the
  pull-up resistor from 4.7 kΩ to ~2.2–3.3 kΩ
- The stainless steel mass has a longer thermal time constant than a bare TO-92 package.
  Allow a few seconds of stabilization time after deploying, especially in water
- The cable entry/epoxy seal is often the IP rating weak point — verify your specific probe

### Multiple sensors on one bus (this project uses two: surface + depth)
- Issue `SKIP_ROM` + `CONVERT_T` to start conversions on all sensors simultaneously ✓
- Read each sensor individually with `MATCH_ROM` + its 64-bit address ✓
- **Never** use `SKIP_ROM` + `READ_SCRATCHPAD` with multiple sensors — causes bus collision
- This project identifies sensors by their hardcoded 64-bit addresses (`SURFACE_ADDR`, `DEPTH_ADDR`)

### Parasite power mode
The sensors in this project are powered from VDD (3-wire connection). Parasite power (2-wire,
steals power from DQ line) works but requires a strong pull-up MOSFET circuit during conversion
and cannot be used for simultaneous multi-sensor conversion. VDD-powered is the reliable choice.

### 1-Wire timing and interrupts
1-Wire bit timing is precise (~15–60 µs slots). The OneWire library disables interrupts during
bit transactions to prevent ISR preemption from causing read failures. This is handled
automatically — no action needed.

### Negative temperature raw register parsing
Temperature is stored as a 16-bit two's complement signed integer. Always treat it as
`int16_t`, never `uint16_t`. The matmunk DS18B20 library handles this correctly.

---

## Sensor Addresses in This Project

Addresses are hardcoded in `src/main.cpp`:

```cpp
const uint8_t SURFACE_ADDR[8] = {40, 156, 235, 111, 0, 0, 0, 114};
const uint8_t DEPTH_ADDR[8]   = {40, 122, 122, 116, 0, 0, 0,  63};
```

The first byte (`40` = `0x28`) confirms both sensors are genuine DS18B20 (family code `0x28`).

If sensors are ever replaced, run a bus scan to find the new addresses:
```cpp
while (ds.selectNext()) {
    uint8_t addr[8]; ds.getAddress(addr);
    for (int i = 0; i < 8; i++) { Serial.print(addr[i]); Serial.print(","); }
    Serial.println();
}
```
Then update `SURFACE_ADDR` and `DEPTH_ADDR` in `main.cpp`.

---

## Library Used

**matmunk/DS18B20** — `lib/DS18B20/`
- Depends on **PaulStoffregen/OneWire** — `lib/OneWire/`
- API: `ds.selectNext()`, `ds.getAddress()`, `ds.getTempC()`
- Download links: `docs/libraries.md`
