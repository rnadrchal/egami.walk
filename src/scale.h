#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------
// Skalen: Bitmaske der erlaubten Halbtoene ab Grundton (Bit 0 = Grundton)
// ---------------------------------------------------------------

struct ScaleDef {
  const char* name;
  uint16_t    mask;
};

const ScaleDef SCALES[] = {
  { "Chromatisch", 0xFFF },
  { "Dur",         0xAB5 },   // 0 2 4 5 7 9 11
  { "Moll",        0x5AD },   // 0 2 3 5 7 8 10
  { "Harm. Moll",  0x9AD },   // 0 2 3 5 7 8 11
  { "Dorisch",     0x6AD },   // 0 2 3 5 7 9 10
  { "Phrygisch",   0x5AB },   // 0 1 3 5 7 8 10
  { "Lydisch",     0xAD5 },   // 0 2 4 6 7 9 11
  { "Mixolydisch", 0x6B5 },   // 0 2 4 5 7 9 10
  { "Pent. Dur",   0x295 },   // 0 2 4 7 9
  { "Pent. Moll",  0x4A9 },   // 0 3 5 7 10
  { "Blues",       0x4E9 },   // 0 3 5 6 7 10
  { "Ganzton M1",  0x555 },   // 0 2 4 6 8 10          (= Messiaen Modus 1)
  // --- traditionelle japanische Pentatoniken ---
  { "Hirajoshi",   0x18D },   // 0 2 3 7 8
  { "In",          0x1A3 },   // 0 1 5 7 8             (Miyako-bushi)
  { "Insen",       0x4A3 },   // 0 1 5 7 10
  { "Iwato",       0x463 },   // 0 1 5 6 10
  { "Yo",          0x2A5 },   // 0 2 5 7 9
  { "Kumoi",       0x28D },   // 0 2 3 7 9
  { "Ryukyu",      0x8B1 },   // 0 4 5 7 11            (Okinawa)
  // --- Messiaen: Modi mit begrenzter Transponierbarkeit ---
  { "Messiaen 2",  0x6DB },   // 0 1 3 4 6 7 9 10      (oktatonisch, Halb-Ganz)
  { "Messiaen 3",  0xDDD },   // 0 2 3 4 6 7 8 10 11
  { "Messiaen 4",  0x9E7 },   // 0 1 2 5 6 7 8 11
  { "Messiaen 5",  0x8E3 },   // 0 1 5 6 7 11
  { "Messiaen 6",  0xD75 },   // 0 2 4 5 6 8 10 11
  { "Messiaen 7",  0xBEF },   // 0 1 2 3 5 6 7 8 9 11
};
const uint8_t SCALE_COUNT = sizeof(SCALES) / sizeof(SCALES[0]);
const uint8_t PITCH_MAX   = 95;

const char* const ROOT_NAMES[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

inline bool inScale(int pitch, uint8_t scale, uint8_t root) {
  if (pitch < 0 || pitch > PITCH_MAX) return false;
  uint8_t deg = (pitch - root + 120) % 12;
  return SCALES[scale].mask & (1 << deg);
}

// naechster Ton der Skala (bei Gleichstand der tiefere)
inline uint8_t quantize(uint8_t pitch, uint8_t scale, uint8_t root) {
  if (scale == 0) return pitch;
  for (int d = 0; d <= 6; d++) {
    if (inScale(pitch - d, scale, root)) return pitch - d;
    if (inScale(pitch + d, scale, root)) return pitch + d;
  }
  return pitch;
}

// um "degrees" Skalenstufen weitergehen (Eingabe mit Encoder)
inline uint8_t scaleStep(uint8_t pitch, int degrees, uint8_t scale, uint8_t root) {
  int p = quantize(pitch, scale, root);
  int dir = degrees > 0 ? 1 : -1;
  for (int n = abs(degrees); n > 0; n--) {
    int q = p + dir;
    while (q >= 0 && q <= PITCH_MAX && !inScale(q, scale, root)) q += dir;
    if (q < 0 || q > PITCH_MAX) break;   // Rand erreicht
    p = q;
  }
  return (uint8_t)p;
}
