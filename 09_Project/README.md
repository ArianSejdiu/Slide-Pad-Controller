# 20-Switch Controller mit zwei emulierten Analog-Sticks

Vollwertiges USB-Gamepad fuer den **Adafruit Feather M4 Express**: D-Pad, vier Schultertasten, vier Action-Buttons und **zwei** emulierte Analog-Sticks aus jeweils vier Switches. Damit ist der Feather M4 voll belegt (alle GPIO in Verwendung).

## Konzept Analog-Sticks

Gateron Red sind digitale Switches - sie kennen nur "gedrueckt" oder "nicht gedrueckt". Echte Analog-Auslenkung gibt es nicht. Wir halten intern pro Stick und pro Achse einen `float` (-127..+127) und animieren ihn:

- **Beim Druecken** ramped der Wert mit `RAMP_PER_MS` (0.6 Einheiten/ms) in Richtung Maximum (`+127` oder `-127`).
- **Beim Loslassen** zerfaellt der Wert mit `DECAY_PER_MS` (1.0 Einheiten/ms) zurueck zur Mitte (`0`).
- Gegenrichtungen (UP+DOWN) heben sich auf -> Ziel = 0, Wert decayed.

Das Spiel sieht damit eine sanft beschleunigende Auslenkung statt eines harten Sprungs. Linker Stick liefert `gp.x`/`gp.y`, rechter Stick `gp.z`/`gp.rz` (HID-Standard fuer Dual-Stick-Gamepads).

> **Hinweis zu Wertebereichen**: TinyUSB-Sticks sind `int8_t` (-127..+127), nicht `int16_t`. Im Code wird intern als float gerechnet und am Schluss gecastet.

## Ist -127 / +127 wie ein echter Controller?

Funktional ja, in der Aufloesung schlechter als z.B. ein Xbox-Stick.

| Eigenschaft               | Dieser Stick (TinyUSB default) | Xbox (XInput)              | PS4/PS5 (HID)           |
|---------------------------|--------------------------------|----------------------------|-------------------------|
| Wertebereich              | -127 ... +127 (`int8_t`)       | -32768 ... +32767 (`int16_t`) | 0 ... 255, Mitte ~128 (`uint8_t`) |
| Stufen pro Achse          | 256                            | 65 536                     | 256                     |
| Vom OS als Stick erkannt  | Ja                             | Ja                         | Ja                      |

Aus Sicht des Spiels ist `-127 = ganz links` und `+127 = ganz rechts` **aequivalent zu Vollausschlag eines echten Sticks**. Unterschiede in der Praxis:

1. **Aufloesung**: 256 Stufen statt 65 000. Beim feinen Aimen in Shootern merkbar, beim Auto-Spiel / Slow-Walk / Kamera-Drehen unsichtbar.
2. **Kurve / Deadzone**: ueber `RAMP_PER_MS` und `DECAY_PER_MS` selbst designbar.
3. **Diagonalen**: 8 Endrichtungen pro Stick, aber waehrend des Rampings entstehen alle Zwischenwerte (`(X=-54, Y=-54)` etc.).

## Pinbelegung Feather M4 Express (20 Switches - voll!)

Visuelle Uebersicht: `feather_m4_pinmap.svg`. Offizielles Pinout: https://learn.adafruit.com/adafruit-feather-m4-express-atsamd51/pinouts

| Gruppe         | Funktion       | Pin   | HID-Mapping        |
|----------------|----------------|-------|--------------------|
| D-Pad          | UP             | 5     | Hat                |
|                | LEFT           | 6     | Hat                |
|                | DOWN           | 9     | Hat                |
|                | RIGHT          | 10    | Hat                |
| Bumpers        | L1             | 11    | Button TL          |
|                | R1             | 12    | Button TR          |
|                | L2             | 13    | Button TL2  (LED blinkt mit, egal) |
|                | R2             | A0    | Button TR2         |
| Action-Buttons | Btn 1 (A)      | A1    | Button A           |
|                | Btn 2 (B)      | A2    | Button B           |
|                | Btn 3 (X)      | A3    | Button X           |
|                | Btn 4 (Y)      | A4    | Button Y           |
| **Linker Stick** | UP           | A5    | `gp.y` -          |
|                | LEFT           | MOSI  | `gp.x` -          |
|                | DOWN           | MISO  | `gp.y` +          |
|                | RIGHT          | SCK   | `gp.x` +          |
| **Rechter Stick** | UP          | 0 (RX)| `gp.rz` -         |
|                | LEFT           | 1 (TX)| `gp.z`  -         |
|                | DOWN           | SDA   | `gp.rz` +         |
|                | RIGHT          | SCL   | `gp.z`  +         |
| **GND**        | -              | GND   | Pin 2 aller Switches |

Pin 2 aller 20 Switches geht auf eine gemeinsame GND-Schiene zurueck zum `GND`-Pin.

### Was ist mit Serial1 und I2C?

Beides ist **weg**: D0/D1 = Right-Stick-UP/LEFT, SDA/SCL = Right-Stick-DOWN/RIGHT. Debug-`Serial` lauft weiterhin ueber **USB-CDC**, also unabhaengig von D0/D1 - `Serial.print(...)` im Serial Monitor funktioniert wie gewohnt.

Falls du spaeter doch I2C brauchst (OLED, BLE-Modul, ...): GPIO-Expander **MCP23017** ueber I2C anschliessen, dann 16 zusaetzliche GPIOs - allerdings muesste der rechte Stick dann auf Expander-Pins umziehen.

## Software-Setup

1. Werkzeuge -> Board: **Adafruit Feather M4 Express**.
2. Werkzeuge -> USB Stack: **TinyUSB**.
3. Bibliothek `Adafruit TinyUSB Library` installieren.
4. `gateron_analog_stick.ino` hochladen.

## Test

- **Windows**: `joy.cpl` -> Eigenschaften -> Test. Beide Achsenkreuze sichtbar, 12 Buttons, Hat.
- **Web**: https://hardwaretester.com/gamepad zeigt linken und rechten Stick separat.
- **Serial Monitor (115200 Baud)** zeigt `PRESS L Stick UP`, `RELEASE R Stick RIGHT` etc.

## Tuning

In der `.ino` ganz oben:
```cpp
const float RAMP_PER_MS  = 0.6f;   // wie schnell der Stick auf Vollausschlag geht
const float DECAY_PER_MS = 1.0f;   // wie schnell er nach dem Loslassen zurueckkehrt
```

- **Snappiger** (fast wie digital): `RAMP_PER_MS = 2.0f`, `DECAY_PER_MS = 4.0f`.
- **Weicher** (Slow-Walk-Feeling): `RAMP_PER_MS = 0.2f`, `DECAY_PER_MS = 0.4f`.

Die Werte gelten fuer beide Sticks gleichermassen. Falls du pro Stick unterschiedliches Verhalten willst (z.B. linker Stick weich fuer Bewegung, rechter Stick snappy fuer Kameraaim), die Konstanten zu Arrays machen und in `updateSticks()` indizieren.

## Bekannte Stolpersteine

- `TUD_HID_REPORT_DESC_GAMEPAD not declared` -> USB Stack auf TinyUSB stellen.
- Geraet wird unter Windows mit altem Namen erkannt -> Geraete-Manager: alten HID-Eintrag loeschen, USB neu einstecken.
- Falls Code-Aenderungen nicht mehr hochladbar (Board reagiert nicht): doppelt-Reset druecken (roter LED-"Atemmodus"), dann Upload.
