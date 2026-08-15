# Tipping Bucket Rain Gauge

Arduino/ESP driver for a cheap tipping bucket rain gauge — specifically the **Zhafira / Evoteknologi "Sensor Curah Hujan Kotak"**, bought because it's affordable and documented, then written from scratch because the QR code on the housing points to a dead link.

Works with any gauge that pulses once per tip: Hall-effect (A3144) or reed switch.

---

## Why this exists

The unit ships with a QR sticker linking to `repo.evoteknologi.com/raingauge_rev3/` for sample code and the schematic. That URL returns 404. The link has been printed on labels since at least May 2024 while the folder behind it moved or was deleted.

The driver itself is trivial — an interrupt and a multiply. What's worth documenting is everything around it: the calibration constant is wrong, the obvious implementation silently loses data, and the failure modes are invisible in the output.

---

## Hardware

| Spec | Value |
|---|---|
| Sensor | A3144 unipolar Hall-effect switch |
| Output | Open-collector, active LOW, one pulse per tip |
| Supply | 3.3 V or 5 V |
| Funnel | 5.4 × 3.6 cm (19.44 cm²) |
| Stated resolution | 0.70 mm/tip — **see Calibration, it's wrong** |
| Cable | 3-wire, ~35 cm |
| Body | PLA+ or ASA (get ASA for outdoors) |

It's a Hall sensor, not a reed switch — the listing states A3144, the cable has three conductors, and there's an indicator LED, which a passive reed can't drive.

---

## Wiring

| Wire | Connect to |
|---|---|
| Black | GND |
| Red | **3V3** |
| Yellow | Interrupt-capable GPIO (D5 on ESP8266, 27 on ESP32) |

**Power at 3.3 V, not 5 V.** The A3144 runs on either, but at 5 V its output swings to 5 V and ESP GPIOs are not 5 V-tolerant. At 3.3 V the signal line is safe by construction.

**Verify polarity before applying power.** The A3144 has no reverse-polarity protection — swap red and black and it dies in about a second, silently, with no visible damage. With the sensor powered but not connected to the MCU, yellow-to-black should read ~3.3 V idle and snap toward 0 V when the magnet passes.

### Pins to avoid

**ESP8266** — `D0`/GPIO16 has no interrupt capability. `D3`, `D4`, `D8` are boot straps; a pull-up on D8 prevents booting entirely. Use `D5`, `D6`, `D7`.

**ESP32** — `GPIO 6–11` are wired to flash. `GPIO 34–39` are input-only with **no internal pull-up**, which an open-collector sensor requires. `0/2/12/15` are straps. Use `27`, `26`, `25`, `33`, `14`, `4`.

**AVR** — only pins `2` and `3` support external interrupts.

---

## Minimal code

```cpp
#define RAIN_PIN D5
const float MM_PER_TIP = 0.735;

volatile unsigned long tips = 0;
volatile unsigned long lastTip = 0;

void IRAM_ATTR onTip() {
  if (millis() - lastTip < 250) return;   // debounce
  lastTip = millis();
  tips++;
}

unsigned long shown = 0;

void setup() {
  Serial.begin(115200);
  pinMode(RAIN_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(RAIN_PIN), onTip, FALLING);
}

void loop() {
  if (tips != shown) {
    shown = tips;
    Serial.print(shown);
    Serial.print(" tips  ");
    Serial.print(shown * MM_PER_TIP, 2);
    Serial.println(" mm");
  }
}
```

---

## Calibration

### The vendor's constant is wrong

The listing publishes its method, which is more than most sellers do. Their figures:

```
100 mL / 70 tips      = 1.4286 mL/tip
1.4286 mL / 19.44 cm² = 0.07349 cm
0.07349 cm × 10       = 0.735 mm
```

The label says **0.70 mm**. They truncated 0.0735 cm to "0.07 cm" before converting. **Their published constant is 4.7% below their own data.**

At 0.70 instead of 0.735, a 2,000 mm wet season reports as ~1,905 mm — 95 mm missing, consistently, in one direction. Systematic bias like this survives averaging and is undetectable after the fact.

(An older revision of the listing quotes a 19.25 cm² funnel with the same 0.70 figure. If bucket geometry changed between hardware revisions and the constant didn't, that's a second reason to measure your own.)

### Calibrate anyway

3D-printed buckets vary in volume and pivot friction. The printed number describes someone's prototype, not your unit.

**Formula**

```
mm_per_tip = (V_mL / N) / A_cm² × 10
```

`V_mL` = volume poured · `N` = tips counted · `A_cm²` = funnel area · `×10` converts cm to mm

**Procedure**

1. Set `MM_PER_TIP` to `1.0` so the sketch reports raw tips.
2. Caliper the funnel **at the catching edge** — the rim where water either enters or splashes off. If the print has rim width or a chamfer, effective area differs from the nominal drawing.
3. Drip **500 mL through slowly — 30 minutes minimum.** IV set, pinhole bottle, or a barely-open tap.
4. Read `N` off the serial monitor.
5. Compute. Repeat twice more. Spread should be under 3%; if not, check for pivot friction and confirm the gauge is level.

**Why 500 mL, not 100** — counting resolution. At 70 tips, ±1 tip is ±1.4% uncertainty. At ~350 tips it's ±0.3%. Same afternoon, four times the precision.

**Why slow** — pour fast and water carries over the divide mid-tip, so you count fewer tips for the same volume and compute a value that's too high. This is the dominant error source, larger than any arithmetic.

That same carry-over happens in real storms, which means **every tipping bucket under-reads in heavy rain**. If storm accuracy matters, characterise it: repeat at a deliberately fast drip rate and compare. A rate-dependent correction curve is a legitimate result.

---

## Three things that will bite you

**1. Polling loses data.** A tip pulse lasts milliseconds. A sketch with `delay()` in `loop()` and a `digitalRead()` for the gauge misses most tips and reports plausible-looking but badly low rainfall. Use the interrupt, and never gate it behind a timer.

**2. The printed constant is wrong.** See above. Use 0.735 as a starting point, your own measurement as the real value.

**3. Log tip counts, not millimetres.** If your calibration changes later, raw counts let you recompute your entire history. Store only mm and that history is permanently baked at the wrong constant. Millimetres are a view; tips are the record.

---

## Debounce

The debounce sets a ceiling on measurable rain rate:

```
max_mm_per_hr = mm_per_tip × (3600000 / debounce_ms)
```

At 0.735 mm/tip and 250 ms that's a 10,584 mm/hr ceiling. Peak instantaneous tropical rainfall is 50–150 mm/hr — orders of magnitude of headroom.

Prefer a generous debounce. Too short and mechanical rebound double-counts, inflating rainfall. That error is invisible and survives averaging, which makes it much worse than the negligible risk of clipping a real tip. 250–400 ms is sensible.

---

## Troubleshooting

| Symptom | Cause |
|---|---|
| Rejected edges climb while bucket is still | Floating input or dead pull-up. Add 10 kΩ signal→3V3; ESP internal pull-up (~45 kΩ) is marginal over a 35 cm cable |
| Rejected edges only when tipping | Normal — magnet crossing threshold plus bucket settling |
| 20 hand-tips reports 22+ | Bounce train outrunning the debounce. Raise to 250–400 ms |
| 20 hand-tips reports 2–3 | You're polling somewhere, or `delay()` is in `loop()` |
| Zero tips, LED never flashes | A3144 is unipolar — if the magnet was reinstalled backwards it will never trigger. Flip it |
| Zero tips, LED flashes fine | Sensor is good; wiring or pin choice is the problem |

**Bench test before deploying:** reset, then hand-tip exactly 20 times, slowly. You want exactly 20. Anything else, fix it before it goes outside.

The LED is the most useful diagnostic on the unit — it flashes on magnet detection independently of your code, which cleanly separates "sensor is fine, software is wrong" from "sensor isn't seeing the magnet."

---

## Field notes

- **Mount dead level.** A tilt biases one bucket side and skews counts.
- **Mesh the funnel** against insects and leaf litter. A clogged gauge reads as zero rain, indistinguishable from a dry day. Cross-check against a second gauge or nearby station if the deployment matters.
- **PLA warps in tropical sun.** Use ASA outdoors.
- **Mark magnet orientation** before ever removing it. A reversed magnet means permanent silent zero.
- **Running two gauges?** Calibrate both in the same session, same water, same drip rate. Then field disagreement is real inter-unit variance rather than two different procedures — which turns a nuisance into a usable agreement statistic.

---

## License

MIT. Not affiliated with Evoteknologi.
