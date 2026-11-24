#include <Arduino.h>
#include <U8g2lib.h>
#include "input.h"
#include "pattern.h"
#include "sequencer.h"

#include "storage.h"
#include "scale.h"

#define DEBUG_HITS   // Schlaege im seriellen Monitor ausgeben (Zeile loeschen = aus)

// Display-Treiber: Steckbrett = 0,96" SSD1306, Endausbau = 1,3" SH1106
#define OLED_SSD1306
#ifdef OLED_SSD1306
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
#else
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
#endif

EncoderInput enc1(3, 2), enc2(6, 5);   // DT, CLK (getauscht fuer CW = +)
Button       btn1(4), btn2(7);

Pattern pattern;

// --- Clock / Reset ---
const uint8_t PIN_CLOCK     = 20;
const uint8_t PIN_RESET     = 21;
const uint8_t PIN_RESET_BTN = 22;

Button btnReset(PIN_RESET_BTN);

const uint32_t BLINK_MS = 250;   // Playhead-Punkt in der Step-Leiste
bool blinkOn = true;

// --- UI-Zustand ---
enum UiMode : uint8_t { MODE_STEP, MODE_MENU };

UiMode  mode     = MODE_STEP;
uint8_t editStep = 0;       // 0-basiert, Anzeige +1
Param   curParam = P_PIT;
uint8_t menuItem = 0;
bool    dirty    = true;    // Display neu zeichnen?

// Kurzmeldung im Menue-Kopf (z.B. "Slot 07 gespeichert")
char     statusMsg[24] = "";
uint32_t statusUntil   = 0;
const uint32_t STATUS_MS = 1500;

void showStatus(const char* msg, uint32_t ms = STATUS_MS) {
  strncpy(statusMsg, msg, sizeof(statusMsg) - 1);
  statusMsg[sizeof(statusMsg) - 1] = '\0';
  statusUntil = millis() + ms;
  dirty = true;
}

// Ueberschreiben bestaetigen: erster Druck "scharf schalten", zweiter speichert
const uint32_t CONFIRM_MS   = 3000;
uint8_t        confirmSlot  = 0;      // 0 = nichts angefragt
uint32_t       confirmUntil = 0;

void cancelConfirm() {
  if (confirmSlot != 0) {   // offene Rueckfrage -> Meldung ausblenden
    confirmSlot = 0;
    statusUntil = 0;
    dirty = true;
  }
}

// --- Globales Menue ---

enum MenuItem : uint8_t { M_STEPS, M_CLKDIV, M_DIR, M_SWING, M_SCALE, M_ROOT, M_TRANS,
                          M_SAVE, M_LOAD, M_COUNT };

const char* const MENU_LABELS[M_COUNT] = { "Steps", "Clock-Teiler", "Richtung", "Swing",
                                           "Skala", "Grundton", "Transpose",
                                           "Speichern", "Laden" };
const uint8_t MENU_ROWS = 5;   // sichtbare Zeilen, Liste scrollt

uint8_t saveSlot = 1;
uint8_t loadSlot = 1;
const char* const DIR_NAMES[4]         = { "Vorw.", "Rueckw.", "PingPong", "Zufall" };

const uint8_t CLKDIV_VALUES[] = { 1, 2, 3, 4, 6, 8, 12, 16 };
const uint8_t CLKDIV_COUNT    = sizeof(CLKDIV_VALUES);

uint8_t clkDivIndex(uint8_t div) {
  for (uint8_t i = 0; i < CLKDIV_COUNT; i++)
    if (CLKDIV_VALUES[i] == div) return i;
  return 0;
}

void menuChange(uint8_t item, int d) {
  switch (item) {
    case M_STEPS:
      pattern.length = constrain((int)pattern.length + d, 1, MAX_STEPS);
      if (editStep >= pattern.length) editStep = pattern.length - 1;
      break;
    case M_CLKDIV: {
      int i = constrain((int)clkDivIndex(pattern.clockDiv) + d, 0, CLKDIV_COUNT - 1);
      pattern.clockDiv = CLKDIV_VALUES[i];
      break;
    }
    case M_DIR:
      pattern.direction = (Direction)constrain((int)pattern.direction + d, 0, 3);
      break;
    case M_SWING:
      pattern.swing = constrain((int)pattern.swing + d, 50, 75);
      break;
    case M_SCALE:
      pattern.scale = constrain((int)pattern.scale + d, 0, SCALE_COUNT - 1);
      break;
    case M_ROOT:
      pattern.root = ((int)pattern.root + d % 12 + 12) % 12;   // C…B im Kreis
      break;
    case M_TRANS:
      pattern.transpose = constrain((int)pattern.transpose + d, -24, 24);
      break;
    case M_SAVE:
      saveSlot = constrain((int)saveSlot + d, SLOT_MIN, SLOT_MAX);
      break;
    case M_LOAD:
      loadSlot = constrain((int)loadSlot + d, SLOT_MIN, SLOT_MAX);
      break;
  }
}

// Speichern / Laden ausfuehren
void menuExecute(uint8_t item) {
  char msg[24];
  if (!storageReady()) {
    showStatus("Keine SD-Karte");
    return;
  }
  if (item == M_SAVE) {
    bool armed = confirmSlot == saveSlot && millis() < confirmUntil;
    if (storageUsed(saveSlot) && !armed) {           // belegt -> erst nachfragen
      confirmSlot  = saveSlot;
      confirmUntil = millis() + CONFIRM_MS;
      snprintf(msg, sizeof(msg), "Slot %02d ersetzen?", saveSlot);
      showStatus(msg, CONFIRM_MS);
      return;
    }
    cancelConfirm();
    bool ok = storageSave(saveSlot, pattern);
    snprintf(msg, sizeof(msg), ok ? "Slot %02d gespeichert" : "Fehler Slot %02d", saveSlot);
    Serial.printf("SAVE Slot %02d ok=%d  Steps %d  Teiler %d  Richtung %d  Transpose %d\n",
                  saveSlot, ok, pattern.length, pattern.clockDiv, pattern.direction, pattern.transpose);
  } else {
    bool ok = storageLoad(loadSlot, pattern);
    if (ok && editStep >= pattern.length) editStep = pattern.length - 1;
    snprintf(msg, sizeof(msg), ok ? "Slot %02d geladen" : "Slot %02d leer/Fehler", loadSlot);
    Serial.printf("LOAD Slot %02d ok=%d  Steps %d  Teiler %d  Richtung %d  Transpose %d\n",
                  loadSlot, ok, pattern.length, pattern.clockDiv, pattern.direction, pattern.transpose);
  }
  showStatus(msg);
}

void menuDefault(uint8_t item) {
  switch (item) {
    case M_STEPS:
      pattern.length = 16;
      if (editStep >= pattern.length) editStep = pattern.length - 1;
      break;
    case M_CLKDIV: pattern.clockDiv  = 1;       break;
    case M_DIR:    pattern.direction = DIR_FWD; break;
    case M_SWING:  pattern.swing     = 50;      break;
    case M_SCALE:  pattern.scale     = 0;       break;
    case M_ROOT:   pattern.root      = 0;       break;
    case M_TRANS:  pattern.transpose = 0;       break;
  }
}

void menuValueText(uint8_t item, char* buf, size_t len) {
  switch (item) {
    case M_STEPS:  snprintf(buf, len, "%d", pattern.length); break;
    case M_CLKDIV: snprintf(buf, len, "/%d", pattern.clockDiv); break;
    case M_DIR:    snprintf(buf, len, "%s", DIR_NAMES[pattern.direction]); break;
    case M_SWING:
      if (pattern.swing == 50) snprintf(buf, len, "aus");
      else                     snprintf(buf, len, "%d%%", pattern.swing);
      break;
    case M_SCALE:  snprintf(buf, len, "%s", SCALES[pattern.scale].name); break;
    case M_ROOT:   snprintf(buf, len, "%s", ROOT_NAMES[pattern.root]); break;
    case M_TRANS:  snprintf(buf, len, "%+d", pattern.transpose); break;
    case M_SAVE:   snprintf(buf, len, "%02d %s", saveSlot, storageUsed(saveSlot) ? "belegt" : "frei"); break;
    case M_LOAD:   snprintf(buf, len, "%02d %s", loadSlot, storageUsed(loadSlot) ? "belegt" : "leer"); break;
    default:       buf[0] = '\0';
  }
}

// --- Wert eines Parameters als Text (fuer die Anzeige) ---
void formatParam(const Step& s, Param p, char* buf, size_t len) {
  switch (p) {
    case P_PIT: {
      // angezeigt wird der gespielte Ton; * = gespeicherter Ton liegt ausserhalb der Skala
      uint8_t q = quantize(s.pitch, pattern.scale, pattern.root);
      noteName(q, buf);
      if (q != s.pitch) { buf[3] = '*'; buf[4] = '\0'; }
      break;
    }
    case P_GAT:
      if (s.tie) snprintf(buf, len, "---");
      else       snprintf(buf, len, "%3d%%", s.gate);
      break;
    case P_LVL: snprintf(buf, len, "%3d", s.level); break;
    case P_TIE: snprintf(buf, len, s.tie ? " on" : "off"); break;
    case P_PRB: snprintf(buf, len, "%3d%%", s.prob); break;
    case P_RAT: snprintf(buf, len, "x%d", s.ratchet); break;
    case P_RPR: snprintf(buf, len, "%3d%%", s.ratProb); break;
    case P_CND: condText(s.cond, buf, len); break;
    case P_RMP:
      if (s.ramp == 0) snprintf(buf, len, "off");
      else             snprintf(buf, len, "%+d%%", s.ramp);
      break;
    case P_SLD:
      if (s.slide == 0) snprintf(buf, len, "off");
      else              snprintf(buf, len, "%3d%%", s.slide);
      break;
    case P_CV1: case P_CV2: case P_CV3: {
      int v = s.cv[p - P_CV1];
      snprintf(buf, len, "%c%d.%d", v < 0 ? '-' : '+', abs(v) / 10, abs(v) % 10);
      break;
    }
    default: buf[0] = '\0';
  }
}

// --- Eingabe verarbeiten ---
void handleStepInput() {
  int d1 = enc1.delta();
  if (d1) {
    editStep = constrain((int)editStep + d1, 0, pattern.length - 1);
    dirty = true;
  }

  // Beschleunigung nur fuer Pitch und CV (grosse Wertebereiche)
  bool accel = (curParam == P_PIT || curParam == P_RMP || curParam >= P_CV1);
  int d2 = accel ? enc2.deltaAccel() : enc2.delta();
  if (d2) {
    Step& s = pattern.steps[editStep];
    if (curParam == P_PIT && pattern.scale != 0)        // Skala aktiv: nur Skalentoene
      s.pitch = scaleStep(s.pitch, d2, pattern.scale, pattern.root);
    else
      setParam(s, curParam, getParam(s, curParam) + d2);
    dirty = true;
  }

  switch (btn1.update()) {
    case BTN_SHORT:
      pattern.steps[editStep].on = !pattern.steps[editStep].on;
      dirty = true;
      break;
    case BTN_LONG:
      mode = MODE_MENU;
      dirty = true;
      break;
    default: break;
  }

  switch (btn2.update()) {
    case BTN_SHORT:                     // naechster Parameter
      curParam = (Param)((curParam + 1) % P_COUNT);
      dirty = true;
      break;
    case BTN_LONG: {                    // ganzen Step auf Standardwerte
      Step def;
      initStep(def);
      noInterrupts();                   // Sequencer liest den Step evtl. gerade
      pattern.steps[editStep] = def;
      interrupts();
      dirty = true;
      break;
    }
    default: break;
  }
}

void handleMenuInput() {
  int d1 = enc1.delta();          // Encoder 1 waehlt den Eintrag
  if (d1) {
    cancelConfirm();
    menuItem = constrain((int)menuItem + d1, 0, M_COUNT - 1);
    dirty = true;
  }

  int d2 = enc2.delta();
  if (d2) {
    cancelConfirm();
    menuChange(menuItem, d2);
    dirty = true;
  }

  if (btn1.update() == BTN_SHORT) {
    cancelConfirm();
    statusUntil = 0;
    mode = MODE_STEP;
    dirty = true;
  }

  switch (btn2.update()) {
    case BTN_SHORT:                     // Standardwert bzw. Speichern/Laden
      if (menuItem == M_SAVE || menuItem == M_LOAD) menuExecute(menuItem);
      else                                          menuDefault(menuItem);
      dirty = true;
      break;
    case BTN_LONG:                      // alle Eintraege auf Standardwert
      for (uint8_t i = 0; i < M_COUNT; i++) menuDefault(i);
      dirty = true;
      break;
    default: break;
  }
}

void handleInput() {
  if (mode == MODE_STEP) handleStepInput();
  else                   handleMenuInput();
}

// --- Step-Leiste: 16 Steps pro Seite, Seite folgt dem Edit-Step ---
void drawStepBar() {
  const uint8_t PER_PAGE = 16;
  const uint8_t CELL     = 8;     // 16 x 8 px = 128 px
  const int     Y        = 55;    // Oberkante der Kaestchen

  uint8_t first = (editStep / PER_PAGE) * PER_PAGE;

  for (uint8_t i = 0; i < PER_PAGE; i++) {
    uint8_t idx = first + i;
    if (idx >= pattern.length) break;

    int x = i * CELL + 1;
    if (pattern.steps[idx].on) u8g2.drawBox(x, Y, 6, 6);     // an
    else                       u8g2.drawFrame(x, Y, 6, 6);   // stumm

    if (pattern.steps[idx].tie) u8g2.drawBox(x + 6, Y + 2, 2, 2);  // Legato: Bruecke zum naechsten

    if (idx == editStep) u8g2.drawHLine(x, Y + 8, 6);       // gewaehlter Step

    if (idx == playhead && blinkOn) {                       // Playhead: Punkt in der Mitte
      u8g2.setDrawColor(2);                                 // XOR: sichtbar auf voll und leer
      u8g2.drawBox(x + 2, Y + 2, 2, 2);
      u8g2.setDrawColor(1);
    }
  }
}

// --- Anzeige ---
void drawStepScreen() {
  const Step& s = pattern.steps[editStep];
  char buf[24];

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);

  // Kopfzeile
  snprintf(buf, sizeof(buf), "STEP %02d/%02d %s",
           editStep + 1, pattern.length, s.on ? "" : "OFF");
  u8g2.drawStr(0, 9, buf);

  // Playhead rechts oben: Dreieck + Nummer
  int8_t ph = playhead;
  if (ph >= 0) {
    u8g2.drawTriangle(104, 1, 104, 9, 108, 5);
    snprintf(buf, sizeof(buf), "%02d", ph + 1);
    u8g2.drawStr(116, 9, buf);
  }

  // Seitenanzeige: zwei kleine Kaestchen, aktive Seite gefuellt
  uint8_t page = paramPage(curParam);
  for (uint8_t i = 0; i < 2; i++) {
    if (i == page) u8g2.drawBox(88 + i * 6, 3, 4, 4);
    else           u8g2.drawFrame(88 + i * 6, 3, 4, 4);
  }

  // Parameter der aktuellen Seite in 2 Spalten x 4 Zeilen
  uint8_t firstP = page * PARAMS_PER_PAGE;
  for (uint8_t p = firstP; p < P_COUNT && p < firstP + PARAMS_PER_PAGE; p++) {
    uint8_t col = (p - firstP) % 2;
    uint8_t row = (p - firstP) / 2;
    int x = col * 66;
    int y = 21 + row * 10;

    char val[8];
    formatParam(s, (Param)p, val, sizeof(val));
    snprintf(buf, sizeof(buf), "%s %4s", PARAMS[p].label, val);

    if (p == curParam) {                 // gewaehlter Parameter invertiert
      u8g2.drawBox(x, y - 8, 61, 10);
      u8g2.setDrawColor(0);
      u8g2.drawStr(x + 1, y, buf);
      u8g2.setDrawColor(1);
    } else {
      u8g2.drawStr(x + 1, y, buf);
    }
  }

  drawStepBar();
  u8g2.sendBuffer();
}

void drawMenuScreen() {
  char buf[24];

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  bool status = millis() < statusUntil;
  u8g2.drawStr(0, 9, status ? statusMsg : "MENU");
  u8g2.drawHLine(0, 11, 128);

  uint8_t first = menuItem >= MENU_ROWS ? menuItem - MENU_ROWS + 1 : 0;
  for (uint8_t i = first; i < M_COUNT && i < first + MENU_ROWS; i++) {
    int y = 21 + (i - first) * 10;
    menuValueText(i, buf, sizeof(buf));

    if (i == menuItem) {
      u8g2.drawBox(0, y - 8, 128, 10);
      u8g2.setDrawColor(0);
    }
    u8g2.drawStr(1, y, MENU_LABELS[i]);
    u8g2.drawStr(127 - u8g2.getStrWidth(buf), y, buf);   // Wert rechtsbuendig
    u8g2.setDrawColor(1);
  }

  u8g2.sendBuffer();
}

void drawScreen() {
  if (mode == MODE_STEP) drawStepScreen();
  else                   drawMenuScreen();
}

void setup() {
  Serial.begin(115200);
  btn1.begin();
  btn2.begin();
  u8g2.setBusClock(400000);   // I2C 400 kHz -> schnelleres Zeichnen
  u8g2.begin();
  initPattern(pattern);
  storageBegin();

  seqBegin(&pattern);
  btnReset.begin();
  pinMode(PIN_CLOCK, INPUT);   // Pull-up extern (10k)
  pinMode(PIN_RESET, INPUT);
  // Schmitt-Trigger (Hysterese) einschalten - bei INPUT ist sie standardmaessig aus
  *portConfigRegister(PIN_CLOCK) |= IOMUXC_PAD_HYS;
  *portConfigRegister(PIN_RESET) |= IOMUXC_PAD_HYS;
  attachInterrupt(digitalPinToInterrupt(PIN_CLOCK), seqClock, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_RESET), seqReset, FALLING);
}

void loop() {
#ifdef DEBUG_HITS
  HitLog h;
  while (seqPopHit(h))
    Serial.printf("Durchlauf %3lu  Step %2d  Schlag %d/%d  LVL %3d  GAT %3d%%\n",
                  h.pass + 1, h.step + 1, h.hit + 1, h.count, h.level, h.gate);
#endif

  // Reset-Taster wirkt wie der Reset-Eingang
  if (btnReset.update() == BTN_SHORT) {
    noInterrupts();
    seqReset();
    interrupts();
  }

  // Playhead geaendert -> neu zeichnen
  static int8_t lastPlayhead = -2;
  int8_t ph = playhead;
  if (ph != lastPlayhead) {
    lastPlayhead = ph;
    dirty = true;
  }

  // Statusmeldung abgelaufen -> Kopfzeile wieder "MENU"
  static bool statusShown = false;
  bool statusActive = millis() < statusUntil;
  if (statusShown && !statusActive) dirty = true;
  statusShown = statusActive;

  // Blinken des Playhead-Punkts
  static uint32_t lastBlink = 0;
  if (millis() - lastBlink >= BLINK_MS) {
    lastBlink = millis();
    blinkOn = !blinkOn;
    if (ph >= 0 && mode == MODE_STEP) dirty = true;
  }

  handleInput();
  if (dirty) {
    drawScreen();
    dirty = false;
  }
}
