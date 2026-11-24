#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------
// Ausgabe ueber die DACs (SPI, gemeinsamer LDAC)
//   DAC1 MCP4822 (CS 10): A = Pitch, B = Level
//   DAC2 MCP4822 (CS 9) : A = CV1,   B = CV2
//   DAC3 MCP4821 (CS 8) : CV3 (nur ein Kanal)
// Alle Werte werden geschrieben und dann per LDAC gleichzeitig ausgegeben.
// ---------------------------------------------------------------

void outBegin();

// Neuer Step: Pitch als DAC-Code (siehe pitchToCode), Level 0–100, CV −50…+50
void outStep(uint16_t pitchCode, uint8_t level, const int8_t cv[3]);

// Nur Pitch aendern (Slide)
void outPitchCode(uint16_t code);

// Nur Level aendern (Ratchet-Schlaege)
void outLevel(uint8_t level);

// DAC-Wert fuer einen Halbton (fuer spaetere Kalibrierung / Slide)
uint16_t pitchToCode(uint8_t pitch);
