#pragma once
#include <Arduino.h>
#include "pattern.h"

// ---------------------------------------------------------------
// Patterns auf der SD-Karte des Teensy 4.1 speichern / laden
// Dateien: /SEQ/P01.SEQ … /SEQ/P99.SEQ
// ---------------------------------------------------------------

const uint8_t SLOT_MIN = 1;
const uint8_t SLOT_MAX = 99;

bool storageBegin();                              // false = keine Karte
bool storageReady();
bool storageSave(uint8_t slot, const Pattern& p);
bool storageLoad(uint8_t slot, Pattern& p);       // p bleibt bei Fehler unveraendert
bool storageUsed(uint8_t slot);                   // aus Cache, schnell
