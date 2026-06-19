/*
 * 20x Gateron Cherry 2-Pin -> Adafruit FEATHER M4 EXPRESS
 * -------------------------------------------------------
 * Vollwertiger USB-Gamepad-Controller mit zwei emulierten Analog-Sticks.
 *
 *   D-Pad (Hat):       UP / LEFT / DOWN / RIGHT
 *   Bumpers:           L1 / R1 / L2 / R2
 *   Action-Buttons:    1 / 2 / 3 / 4   (HID A / B / X / Y)
 *   Left  Stick (gp.x / gp.y):     UP / LEFT / DOWN / RIGHT
 *   Right Stick (gp.z / gp.rz):    UP / LEFT / DOWN / RIGHT
 *
 * KONZEPT ANALOG-STICKS:
 *   Red Switches sind digital -> nur on/off. Echte Analogwerte gibt es
 *   nicht. Wir halten intern pro Achse einen float (-127..+127), rampen
 *   beim Druecken Richtung Maximum und decayen beim Loslassen zur Mitte.
 *   gp.x/y/z/rz sind int8_t -> am Schluss casten.
 *
 * VERKABELUNG (Pin1 -> GPIO, Pin2 -> gemeinsame GND-Schiene):
 *
 *   D-Pad:        UP=5    LEFT=6    DOWN=9    RIGHT=10
 *   Bumpers:      L1=11   R1=12     L2=13     R2=A0
 *   Buttons:      Btn1=A1 Btn2=A2   Btn3=A3   Btn4=A4
 *   Left Stick:   UP=A5   LEFT=MOSI DOWN=MISO RIGHT=SCK
 *   Right Stick:  UP=0    LEFT=1    DOWN=SDA  RIGHT=SCL
 *
 * Damit ist der Feather M4 voll belegt. Serial1 (D0/D1) und I2C (SDA/SCL)
 * stehen NICHT mehr zur Verfuegung. Debug bleibt ueber USB-Serial (`Serial`),
 * das funktioniert unabhaengig von D0/D1.
 */

#include "Adafruit_TinyUSB.h"

// ---------- HID Setup ----------
uint8_t const desc_hid_report[] = {
  TUD_HID_REPORT_DESC_GAMEPAD()
};
Adafruit_USBD_HID usb_hid;
hid_gamepad_report_t gp;

// ---------- Konstanten ----------
const uint16_t DEBOUNCE_MS  = 5;

// Stick-Tuning (Einheiten/ms, Skala -127..+127)
const float    RAMP_PER_MS  = 0.6f;
const float    DECAY_PER_MS = 1.0f;
const int8_t   STICK_MAX    = 127;

const uint8_t  NUM_STICKS   = 2;   // 0 = links, 1 = rechts

// ---------- D-Pad (Hat) ----------
enum DirBit : uint8_t { DIR_UP = 1, DIR_LEFT = 2, DIR_DOWN = 4, DIR_RIGHT = 8 };

struct Dir {
  uint8_t pin;
  uint8_t dirBit;
  int lastReading, stableState;
  unsigned long lastChange;
};

Dir dirs[4] = {
  {  5, DIR_UP,    HIGH, HIGH, 0 },
  {  6, DIR_LEFT,  HIGH, HIGH, 0 },
  {  9, DIR_DOWN,  HIGH, HIGH, 0 },
  { 10, DIR_RIGHT, HIGH, HIGH, 0 },
};

// ---------- Bumpers + Action-Buttons ----------
struct Btn {
  uint8_t  pin;
  uint32_t mask;
  const char* name;
  int lastReading, stableState;
  unsigned long lastChange;
};

Btn btns[8] = {
  { A1, (uint32_t)1 << GAMEPAD_BUTTON_A,   "Btn1(A)", HIGH, HIGH, 0 },
  { A2, (uint32_t)1 << GAMEPAD_BUTTON_B,   "Btn2(B)", HIGH, HIGH, 0 },
  { A3, (uint32_t)1 << GAMEPAD_BUTTON_X,   "Btn3(X)", HIGH, HIGH, 0 },
  { A4, (uint32_t)1 << GAMEPAD_BUTTON_Y,   "Btn4(Y)", HIGH, HIGH, 0 },
  { 11, (uint32_t)1 << GAMEPAD_BUTTON_TL,  "L1",      HIGH, HIGH, 0 },
  { 12, (uint32_t)1 << GAMEPAD_BUTTON_TR,  "R1",      HIGH, HIGH, 0 },
  { 13, (uint32_t)1 << GAMEPAD_BUTTON_TL2, "L2",      HIGH, HIGH, 0 },
  { A0, (uint32_t)1 << GAMEPAD_BUTTON_TR2, "R2",      HIGH, HIGH, 0 },
};

// ---------- Stick-Switches (2 Sticks * 4 Richtungen = 8 Eintraege) ----------
struct StickKey {
  uint8_t pin;
  uint8_t stickIdx;   // 0 = links, 1 = rechts
  int8_t  axis;       // 0 = X, 1 = Y
  int8_t  dir;        // +1 oder -1
  const char* name;
  int lastReading, stableState;
  unsigned long lastChange;
};

StickKey stickKeys[8] = {
  // ---- Linker Stick (gp.x / gp.y) ----
  { A5,   0, 1, -1, "L Stick UP",    HIGH, HIGH, 0 },
  { MOSI, 0, 0, -1, "L Stick LEFT",  HIGH, HIGH, 0 },
  { MISO, 0, 1, +1, "L Stick DOWN",  HIGH, HIGH, 0 },
  { SCK,  0, 0, +1, "L Stick RIGHT", HIGH, HIGH, 0 },
  // ---- Rechter Stick (gp.z / gp.rz) ----
  { 0,    1, 1, -1, "R Stick UP",    HIGH, HIGH, 0 },  // D0 / RX
  { 1,    1, 0, -1, "R Stick LEFT",  HIGH, HIGH, 0 },  // D1 / TX
  { SDA,  1, 1, +1, "R Stick DOWN",  HIGH, HIGH, 0 },
  { SCL,  1, 0, +1, "R Stick RIGHT", HIGH, HIGH, 0 },
};

// ---------- Zustand ----------
uint8_t  activeDirs    = 0;
uint32_t activeButtons = 0;

float    stickX[NUM_STICKS] = { 0, 0 };
float    stickY[NUM_STICKS] = { 0, 0 };
int8_t   targetX[NUM_STICKS] = { 0, 0 };
int8_t   targetY[NUM_STICKS] = { 0, 0 };

unsigned long lastTickMs = 0;

// ---------- Helfer ----------
uint8_t hatFromDirs(uint8_t d) {
  bool u = d & DIR_UP, dn = d & DIR_DOWN, l = d & DIR_LEFT, r = d & DIR_RIGHT;
  if (u && dn) { u = false; dn = false; }
  if (l && r)  { l = false; r = false; }
  if (u && r)  return GAMEPAD_HAT_UP_RIGHT;
  if (u && l)  return GAMEPAD_HAT_UP_LEFT;
  if (dn && r) return GAMEPAD_HAT_DOWN_RIGHT;
  if (dn && l) return GAMEPAD_HAT_DOWN_LEFT;
  if (u)       return GAMEPAD_HAT_UP;
  if (dn)      return GAMEPAD_HAT_DOWN;
  if (l)       return GAMEPAD_HAT_LEFT;
  if (r)       return GAMEPAD_HAT_RIGHT;
  return GAMEPAD_HAT_CENTERED;
}

template <typename T>
bool debounceUpdate(T &k, unsigned long now) {
  int reading = digitalRead(k.pin);
  if (reading != k.lastReading) {
    k.lastChange = now;
    k.lastReading = reading;
  }
  if ((now - k.lastChange) > DEBOUNCE_MS && reading != k.stableState) {
    k.stableState = reading;
    return true;
  }
  return false;
}

float approach(float current, float target, float step) {
  if (current < target) { current += step; if (current > target) current = target; }
  else if (current > target) { current -= step; if (current < target) current = target; }
  return current;
}

void recomputeStickTargets() {
  int8_t sx[NUM_STICKS] = { 0, 0 };
  int8_t sy[NUM_STICKS] = { 0, 0 };
  for (auto &k : stickKeys) {
    if (k.stableState == LOW) {
      if (k.axis == 0) sx[k.stickIdx] += k.dir;
      else             sy[k.stickIdx] += k.dir;
    }
  }
  for (uint8_t i = 0; i < NUM_STICKS; i++) {
    targetX[i] = (sx[i] > 0) ? 1 : (sx[i] < 0 ? -1 : 0);
    targetY[i] = (sy[i] > 0) ? 1 : (sy[i] < 0 ? -1 : 0);
  }
}

void updateSticks(unsigned long now) {
  unsigned long dt = now - lastTickMs;
  if (dt == 0) return;
  lastTickMs = now;

  for (uint8_t i = 0; i < NUM_STICKS; i++) {
    float tx = targetX[i] * (float)STICK_MAX;
    float ty = targetY[i] * (float)STICK_MAX;
    float rx = (targetX[i] != 0) ? RAMP_PER_MS : DECAY_PER_MS;
    float ry = (targetY[i] != 0) ? RAMP_PER_MS : DECAY_PER_MS;
    stickX[i] = approach(stickX[i], tx, rx * dt);
    stickY[i] = approach(stickY[i], ty, ry * dt);
  }
}

void sendGamepad() {
  memset(&gp, 0, sizeof(gp));
  gp.hat     = hatFromDirs(activeDirs);
  gp.buttons = activeButtons;
  // Linker Stick
  gp.x       = (int8_t)stickX[0];
  gp.y       = (int8_t)stickY[0];
  // Rechter Stick
  gp.z       = (int8_t)stickX[1];
  gp.rz      = (int8_t)stickY[1];
  usb_hid.sendReport(0, &gp, sizeof(gp));
}

// ---------- Lifecycle ----------
void setup() {
  for (auto &d : dirs)       pinMode(d.pin, INPUT_PULLUP);
  for (auto &b : btns)       pinMode(b.pin, INPUT_PULLUP);
  for (auto &k : stickKeys)  pinMode(k.pin, INPUT_PULLUP);

  usb_hid.setPollInterval(2);
  usb_hid.setReportDescriptor(desc_hid_report, sizeof(desc_hid_report));
  usb_hid.begin();
  while (!TinyUSBDevice.mounted()) delay(1);

  Serial.begin(115200);  // USB-CDC, unabhaengig von D0/D1
  lastTickMs = millis();
}

void loop() {
  unsigned long now = millis();
  bool buttonsChanged = false;
  bool stickKeysChanged = false;

  for (auto &d : dirs) {
    if (debounceUpdate(d, now)) {
      if (d.stableState == LOW) activeDirs |=  d.dirBit;
      else                      activeDirs &= ~d.dirBit;
      buttonsChanged = true;
    }
  }

  for (auto &b : btns) {
    if (debounceUpdate(b, now)) {
      if (b.stableState == LOW) {
        activeButtons |=  b.mask;
        Serial.print("PRESS  "); Serial.println(b.name);
      } else {
        activeButtons &= ~b.mask;
        Serial.print("RELEASE "); Serial.println(b.name);
      }
      buttonsChanged = true;
    }
  }

  for (auto &k : stickKeys) {
    if (debounceUpdate(k, now)) {
      stickKeysChanged = true;
      Serial.print(k.stableState == LOW ? "PRESS  " : "RELEASE ");
      Serial.println(k.name);
    }
  }
  if (stickKeysChanged) recomputeStickTargets();

  // Stick-Position laeuft jeden Loop weiter (auch waehrend Decay)
  int8_t oldX0 = (int8_t)stickX[0], oldY0 = (int8_t)stickY[0];
  int8_t oldX1 = (int8_t)stickX[1], oldY1 = (int8_t)stickY[1];
  updateSticks(now);
  bool sticksMoved =
       (int8_t)stickX[0] != oldX0 || (int8_t)stickY[0] != oldY0
    || (int8_t)stickX[1] != oldX1 || (int8_t)stickY[1] != oldY1;

  if ((buttonsChanged || sticksMoved) && usb_hid.ready()) {
    sendGamepad();
  }
}
