// ---------------------------------------------------------------
// Hardware-Test ohne Display/Encoder
//
// Aufbau minimal: Teensy, MCP4821 + TL072 (CV3-Stufe, Gate-Treiber),
//                 LED an Pin 15, Taster an Pin 22 (gegen GND)
//
// LED Pin 15 : blinkt kurz einmal pro Sekunde ("lebt") – Test der LED
// Taster 22  : kurz  = naechster CV-Wert (+5 / +2,5 / 0 / −2,5 / −5 V, im Kreis),
//                      LED leuchtet dabei 0,3 s
//              lang (0,6 s) = Gate pulsen (2 Hz) an/aus; waehrend des Pulsens
//                      zeigt die LED das Gate an
//              Funktioniert auch OHNE USB (Versorgung ueber 7805).
//
// Serieller Monitor (nur mit USB, 115200 Baud):
//   1 … 5  CV1–3 direkt waehlen
//   g      Gate an/aus (Dauerpegel zum Messen)
//   p      Gate pulsen (2 Hz) an/aus – zum Hoeren an der Huellkurve
// Pitch steht fest auf C-2, Level auf 80.
// ---------------------------------------------------------------
#include <Arduino.h>
#include "../output.h"

const uint8_t PIN_GATE = 14;
const uint8_t PIN_LED  = 15;
const uint8_t PIN_BTN  = 22;

const int8_t  CV_VALUES[] = { 50, 25, 0, -25, -50 };   // Zehntel-Volt
const uint8_t CV_COUNT    = sizeof(CV_VALUES);

uint8_t  cvIndex   = 2;
bool     gateOn    = false;
bool     pulsing   = false;
uint32_t lastPulse = 0;
uint32_t ledUntil  = 0;      // LED-Rueckmeldung nach Tastendruck

void apply() {
  int8_t cv[3] = { CV_VALUES[cvIndex], CV_VALUES[cvIndex], CV_VALUES[cvIndex] };
  outStep(pitchToCode(24), 80, cv);
  digitalWriteFast(PIN_GATE, gateOn);
  Serial.printf("CV1-3 = %+.1f V   Gate %s%s\n", CV_VALUES[cvIndex] / 10.0,
                gateOn ? "AN " : "aus", pulsing ? " (pulst)" : "");
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_GATE, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BTN, INPUT_PULLUP);
  outBegin();
  delay(1000);   // Zeit fuer den seriellen Monitor (ohne USB egal)
  Serial.println("Hardware-Test: 1-5 = CV, g = Gate, p = Gate pulsen, Taster 22 = naechster CV-Wert");
  apply();
}

void loop() {
  uint32_t now = millis();

  // --- serielle Befehle (nur mit USB) ---
  if (Serial.available()) {
    char c = Serial.read();
    if (c >= '1' && c <= '5') { cvIndex = c - '1'; apply(); }
    else if (c == 'g')        { pulsing = false; gateOn = !gateOn; apply(); }
    else if (c == 'p')        { pulsing = !pulsing; gateOn = false; apply(); }
  }

  // --- Taster: entprellt; kurz = naechster CV-Wert, lang = Gate pulsen ---
  static bool     btnLast   = false;
  static uint32_t btnChange = 0;
  static bool     longDone  = false;
  bool btn = digitalReadFast(PIN_BTN) == LOW;
  if (btn != btnLast && now - btnChange > 20) {
    btnChange = now;
    btnLast   = btn;
    if (btn) {
      longDone = false;                       // Druck beginnt
    } else if (!longDone) {                   // kurz losgelassen
      cvIndex  = (cvIndex + 1) % CV_COUNT;
      ledUntil = now + 300;
      Serial.print("Taster kurz -> ");
      apply();
    }
  }
  if (btn && btnLast && !longDone && now - btnChange >= 600) {
    longDone = true;                          // lang gehalten
    pulsing  = !pulsing;
    gateOn   = false;
    Serial.print("Taster lang -> ");
    apply();
  }

  // --- Gate pulsen ---
  if (pulsing && now - lastPulse >= 250) {
    lastPulse = now;
    gateOn = !gateOn;
    digitalWriteFast(PIN_GATE, gateOn);
  }

  // --- LED: beim Pulsen = Gate, sonst Rueckmeldung Taster / kurzes Blinken pro Sekunde ---
  bool led = pulsing ? gateOn : ((now < ledUntil) || (now % 1000 < 50));
  digitalWriteFast(PIN_LED, led);
}
