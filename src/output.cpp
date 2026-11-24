#include "output.h"
#include <SPI.h>

const uint8_t PIN_CS_DAC1 = 10;
const uint8_t PIN_CS_DAC2 = 9;
const uint8_t PIN_CS_DAC3 = 8;
const uint8_t PIN_LDAC    = 17;

static const SPISettings DAC_SPI(20000000, MSBFIRST, SPI_MODE0);   // MCP482x: max. 20 MHz

// Steuerbits MCP482x: Bit 15 = Kanal (0 = A, 1 = B), Bit 13 = Verstaerkung 1x, Bit 12 = aktiv
const uint16_t CH_A = 0x0000;
const uint16_t CH_B = 0x8000;
const uint16_t GAIN_1X_ACTIVE = 0x3000;

// --- Umrechnung ---------------------------------------------------
// Ausgangsstufe Pitch / Level: nicht-invertierend, Verstaerkung 1 + Rf / Rg
const float RF_PITCH_LEVEL = 33000.0f;   // Steckbrett: 33 kOhm (Plan: 30 kOhm)
const float RG_PITCH_LEVEL = 10000.0f;
const float GAIN_PL        = 1.0f + RF_PITCH_LEVEL / RG_PITCH_LEVEL;   // 4,3

const float CODES_PER_VOLT = 4096.0f / 2.048f;   // 2000 Codes pro Volt am DAC

static uint16_t voltsToCode(float vOut, float gain) {
  int32_t code = lroundf(vOut / gain * CODES_PER_VOLT);
  return (uint16_t)constrain(code, 0, 4095);
}

// Pitch: 1 V/Okt am Ausgang, 0 V = C0
uint16_t pitchToCode(uint8_t pitch) {
  return voltsToCode(pitch / 12.0f, GAIN_PL);
}

// Level 0–100 -> 0–8 V am Ausgang
static uint16_t levelToCode(uint8_t level) {
  return voltsToCode(level * 0.08f, GAIN_PL);
}

// CV −50…+50 (Zehntel-Volt). Ausgangsstufe invertierend mit Offset aus −12 V:
//   Vout = (Rf / Roff) · 12 V − (Rf / Rin) · Vdac   ->   Vdac = (Offset − Vout) / (Rf / Rin)
// Aus den Bauteilen gerechnet (Plan):
//   CV_GAIN   = Rf / Rin              = 100 k / 20 k            = 5,0
//   CV_OFFSET = Rf / Roff · |−12 V|   = 100 k / 240 k · 12 V    = 5,0 V
// Steckbrett gemessen (−12-V-Schiene = −11,74 V, Rin 20 k, Roff 230 k = 220 k + 10 k):
//   DAC 0 V -> +4,98 V, Verstaerkung aus erster Messung (Rin unveraendert) 5,035
const float CV_GAIN     = 5.035f;
const float CV_OFFSET   = 4.980f;
// -> spaeter durch Kalibrierung (Nullpunkt + Steigung pro CV-Ausgang) ersetzen

static uint16_t cvToCode(int8_t cv) {
  float vOut = cv * 0.1f;
  float vDac = (CV_OFFSET - vOut) / CV_GAIN;
  int32_t code = lroundf(vDac * CODES_PER_VOLT);
  return (uint16_t)constrain(code, 0, 4095);
}

// --- SPI ------------------------------------------------------------
static void dacWrite(uint8_t csPin, uint16_t channel, uint16_t code) {
  uint16_t word = channel | GAIN_1X_ACTIVE | (code & 0x0FFF);
  SPI.beginTransaction(DAC_SPI);
  digitalWriteFast(csPin, LOW);
  SPI.transfer16(word);
  digitalWriteFast(csPin, HIGH);
  SPI.endTransaction();
}

static void ldacPulse() {
  digitalWriteFast(PIN_LDAC, LOW);    // mind. 100 ns
  delayNanoseconds(200);
  digitalWriteFast(PIN_LDAC, HIGH);
}

// --- Oeffentlich ----------------------------------------------------
void outBegin() {
  pinMode(PIN_CS_DAC1, OUTPUT); digitalWriteFast(PIN_CS_DAC1, HIGH);
  pinMode(PIN_CS_DAC2, OUTPUT); digitalWriteFast(PIN_CS_DAC2, HIGH);
  pinMode(PIN_CS_DAC3, OUTPUT); digitalWriteFast(PIN_CS_DAC3, HIGH);
  pinMode(PIN_LDAC,    OUTPUT); digitalWriteFast(PIN_LDAC,    HIGH);
  SPI.begin();

  int8_t zero[3] = { 0, 0, 0 };
  outStep(0, 0, zero);
}

void outStep(uint16_t pitchCode, uint8_t level, const int8_t cv[3]) {
  dacWrite(PIN_CS_DAC1, CH_A, pitchCode);
  dacWrite(PIN_CS_DAC1, CH_B, levelToCode(level));
  dacWrite(PIN_CS_DAC2, CH_A, cvToCode(cv[0]));
  dacWrite(PIN_CS_DAC2, CH_B, cvToCode(cv[1]));
  dacWrite(PIN_CS_DAC3, CH_A, cvToCode(cv[2]));   // MCP4821: Bit 15 muss 0 sein
  ldacPulse();
}

void outPitchCode(uint16_t code) {
  dacWrite(PIN_CS_DAC1, CH_A, code);
  ldacPulse();
}

void outLevel(uint8_t level) {
  dacWrite(PIN_CS_DAC1, CH_B, levelToCode(level));
  ldacPulse();
}
