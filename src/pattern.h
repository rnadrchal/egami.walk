#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------
// Datenmodell: Step, Pattern, Parameterzugriff
// Pitch 0 = C0 = 0 V, 1 Halbton = 1/12 V
// ---------------------------------------------------------------

const uint8_t MAX_STEPS      = 64;
const uint8_t PATTERN_VERSION = 3;   // fuer SD-Format (3: + Ratchet-Rampe)

struct Step {
  uint8_t pitch;    // Halbton 0–95 (C0…B7)
  uint8_t gate;     // 0–100 %
  uint8_t level;    // 0–100 (0–8 V)
  uint8_t prob;     // 0–100 %
  uint8_t ratchet;  // 1–8 Schlaege pro Step (1 = kein Ratchet)
  uint8_t ratProb;  // 0–100 %: Wahrscheinlichkeit, dass der Step ratchet (sonst Einzelschlag)
  uint8_t cond;     // Bedingung, Index in die Tabelle (0 = immer), siehe condText()
  uint8_t slide;    // 0–100 % der Step-Dauer gleiten zum Ton (0 = aus)
  int8_t  ramp;     // −100…+100 %: Ratchet-Rampe auf LVL und Gate (+ = leiser, − = lauter)
  int8_t  cv[3];    // −50…+50 = −5,0…+5,0 V
  bool    on;       // false = Pause
  bool    tie;      // Legato
};

enum Direction : uint8_t { DIR_FWD, DIR_REV, DIR_PINGPONG, DIR_RANDOM };

struct Pattern {
  Step      steps[MAX_STEPS];
  uint8_t   length;     // 1–64
  uint8_t   clockDiv;   // 1, 2, 3, 4, 6, 8 …
  Direction direction;
  int8_t    transpose;  // ±24
  uint8_t   scale;      // Index Skala (0 = chromatisch)
  uint8_t   root;       // Grundton 0–11 (C…B)
  uint8_t   swing;      // 50–75 % (50 = kein Swing)
};

// --- Bedingungen (CND) ---
// Index 0      : "---" immer
// Index 1–35   : A:B mit B = 2…8, A = 1…B  (spielt im A-ten von B Durchlaeufen)
// Index 36     : "1ST" nur im ersten Durchlauf nach Reset
// Index 37     : "!1S" nie im ersten Durchlauf
const uint8_t COND_ALWAYS    = 0;
const uint8_t COND_FIRST     = 36;
const uint8_t COND_NOT_FIRST = 37;
const uint8_t COND_COUNT     = 38;

// A:B aus dem Index berechnen (nur fuer Index 1–35)
inline void condRatio(uint8_t idx, uint8_t& a, uint8_t& b) {
  uint8_t i = idx - 1;
  for (b = 2; b <= 8; b++) {
    if (i < b) { a = i + 1; return; }
    i -= b;
  }
  a = 1; b = 1;   // ungueltig
}

inline void condText(uint8_t idx, char* buf, size_t len) {
  if (idx == COND_ALWAYS)         snprintf(buf, len, "---");
  else if (idx == COND_FIRST)     snprintf(buf, len, "1ST");
  else if (idx == COND_NOT_FIRST) snprintf(buf, len, "!1S");
  else { uint8_t a, b; condRatio(idx, a, b); snprintf(buf, len, "%d:%d", a, b); }
}

// --- Parameter pro Step (Reihenfolge = Zyklus von Encoder 2) ---
// Seite 1: P_PIT … P_CND, Seite 2: P_SLD … P_CV3 (CVs muessen am Ende bleiben)
enum Param : uint8_t { P_PIT, P_GAT, P_LVL, P_TIE, P_PRB, P_CND, P_RAT, P_RPR,
                       P_RMP, P_SLD, P_CV1, P_CV2, P_CV3, P_COUNT };

const uint8_t PARAMS_PER_PAGE = 8;
inline uint8_t paramPage(Param p) { return p / PARAMS_PER_PAGE; }

struct ParamInfo {
  const char* label;
  int16_t     minVal;
  int16_t     maxVal;
};

const ParamInfo PARAMS[P_COUNT] = {
  { "PIT",   0,  95 },
  { "GAT",   0, 100 },
  { "LVL",   0, 100 },
  { "TIE",   0,   1 },
  { "PRB",   0, 100 },
  { "CND",   0, COND_COUNT - 1 },
  { "RAT",   1,   8 },
  { "RPR",   0, 100 },
  { "RMP", -100, 100 },
  { "SLD",   0, 100 },
  { "CV1", -50,  50 },
  { "CV2", -50,  50 },
  { "CV3", -50,  50 },
};

inline int16_t getParam(const Step& s, Param p) {
  switch (p) {
    case P_PIT: return s.pitch;
    case P_GAT: return s.gate;
    case P_LVL: return s.level;
    case P_TIE: return s.tie ? 1 : 0;
    case P_PRB: return s.prob;
    case P_RAT: return s.ratchet;
    case P_RPR: return s.ratProb;
    case P_CND: return s.cond;
    case P_RMP: return s.ramp;
    case P_SLD: return s.slide;
    case P_CV1: return s.cv[0];
    case P_CV2: return s.cv[1];
    case P_CV3: return s.cv[2];
    default:    return 0;
  }
}

inline void setParam(Step& s, Param p, int16_t v) {
  v = constrain(v, PARAMS[p].minVal, PARAMS[p].maxVal);
  switch (p) {
    case P_PIT: s.pitch = v; break;
    case P_GAT: s.gate  = v; break;
    case P_LVL: s.level = v; break;
    case P_TIE: s.tie   = (v != 0); break;
    case P_PRB: s.prob  = v; break;
    case P_RAT: s.ratchet = v; break;
    case P_RPR: s.ratProb = v; break;
    case P_CND: s.cond    = v; break;
    case P_RMP: s.ramp    = v; break;
    case P_SLD: s.slide   = v; break;
    case P_CV1: s.cv[0] = v; break;
    case P_CV2: s.cv[1] = v; break;
    case P_CV3: s.cv[2] = v; break;
    default: break;
  }
}

// --- Standardwerte ---
inline void initStep(Step& s) {
  s.pitch = 24;     // C2
  s.gate  = 50;
  s.level = 80;
  s.prob  = 100;
  s.ratchet = 1;
  s.ratProb = 100;
  s.cond    = COND_ALWAYS;
  s.slide   = 0;
  s.ramp    = 0;
  s.cv[0] = s.cv[1] = s.cv[2] = 0;
  s.on    = true;
  s.tie   = false;
}

inline void initPattern(Pattern& pt) {
  for (auto& s : pt.steps) initStep(s);
  pt.length    = 16;
  pt.clockDiv  = 1;
  pt.direction = DIR_FWD;
  pt.transpose = 0;
  pt.scale     = 0;
  pt.root      = 0;
  pt.swing     = 50;
}

// --- Anzeige-Hilfe: Halbton -> "C#3" ---
inline void noteName(uint8_t pitch, char* buf) {   // buf >= 4 Zeichen
  static const char* names[12] =
    { "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-" };
  buf[0] = names[pitch % 12][0];
  buf[1] = names[pitch % 12][1];
  buf[2] = '0' + pitch / 12;
  buf[3] = '\0';
}
