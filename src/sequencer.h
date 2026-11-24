#pragma once
#include <Arduino.h>
#include "pattern.h"

// ---------------------------------------------------------------
// Sequencer-Lauf: Playhead weiterschalten (Clock), Reset
// seqClock() / seqReset() werden direkt aus den Interrupts gerufen,
// damit der Step-Wechsel nicht vom Display-Aufbau verzoegert wird.
// ---------------------------------------------------------------

extern volatile int8_t playhead;   // -1 = noch nicht gestartet

void seqBegin(Pattern* p);
void seqClock();
void seqReset();

// Protokoll der ausgeloesten Schlaege (zum Testen ohne DAC)
struct HitLog {
  uint8_t step;    // 0-basiert
  uint8_t hit;     // 0-basiert
  uint8_t count;   // Anzahl Schlaege im Step
  uint8_t level;   // nach Rampe
  uint8_t gate;    // GAT % nach Rampe
  uint32_t pass;   // Durchlauf (0 = erster nach Reset)
};
bool seqPopHit(HitLog& h);   // false = nichts Neues
