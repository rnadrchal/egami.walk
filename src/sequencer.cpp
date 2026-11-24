#include "sequencer.h"
#include "output.h"
#include "scale.h"

const uint8_t  PIN_GATE       = 14;
const uint8_t  PIN_LED_START  = 15;
const uint32_t RESET_GUARD_US = 5000;    // Clock kurz nach Reset ignorieren
const uint32_t EDGE_GUARD_US  = 2000;    // Mehrfachflanken innerhalb dieser Zeit ignorieren
const uint32_t GATE_MIN_US    = 1000;    // kuerzestes Gate (auch bei 0 %)
const uint32_t GATE_GAP_US    = 2000;    // Gate endet spaetestens so lange vor dem naechsten Step
const uint32_t RETRIG_GAP_US  = 200;     // Pause, wenn ein noch offenes Gate neu angestossen wird

volatile int8_t playhead = -1;

static Pattern*     pat;
static uint8_t      divCount      = 0;       // Clock-Teiler
static int8_t       pingDir       = 1;       // Ping-Pong: +1 / -1
static uint32_t     resetAt       = 0;
static uint32_t     lastClockUs   = 0;
static uint32_t     clockPeriodUs = 125000;  // Startannahme bis zur ersten Messung
static bool         gateHigh      = false;
static bool         tieActive     = false;   // letzter gespielter Step war Legato
static uint32_t     loopCount     = 0;       // Durchlauf, 0 = erster nach Reset/Start
static uint8_t      stepsInPass   = 0;       // nur fuer Richtung Zufall

// Swing: jeder zweite Step wird verzoegert
static IntervalTimer   swingTimer;
static volatile int16_t swingPending = -1;     // verzoegerter Step, -1 = keiner
static bool            offbeat       = true;   // erster Step nach Start wird zum "On-Beat"
static IntervalTimer gateTimer;

// Ratchets: weitere Schlaege innerhalb des Steps
static IntervalTimer ratchetTimer;
static uint8_t  ratchetLeft  = 0;   // noch ausstehende Schlaege
static uint32_t ratchetSubUs = 0;   // Abstand der Schlaege
static uint8_t  ratchetGate  = 0;   // GAT des Steps
static bool     ratchetTie   = false;
static uint8_t  ratchetN     = 1;   // Schlaege insgesamt
static uint8_t  ratchetIdx   = 0;   // naechster Schlag (0-basiert)
static uint8_t  ratchetLevel = 0;   // LVL des Steps
static int8_t   ratchetRamp  = 0;   // RMP des Steps
static uint8_t  ratchetStep  = 0;

// Slide: Pitch gleitet linear vom letzten zum neuen Ton
const uint32_t SLIDE_TICK_US = 500;          // 2 kHz Aktualisierung
static IntervalTimer slideTimer;
static uint16_t curPitchCode = 0;            // zuletzt ausgegebener Pitch-Code
static int32_t  slideFrom = 0, slideTo = 0;
static uint32_t slideTicks = 0, slideTick = 0;

static void slideIsr() {
  slideTick++;
  int32_t code = slideFrom + (slideTo - slideFrom) * (int32_t)slideTick / (int32_t)slideTicks;
  curPitchCode = (uint16_t)code;
  outPitchCode(curPitchCode);
  if (slideTick >= slideTicks) slideTimer.end();
}

// --- Protokoll (Ringpuffer) ---
static const uint8_t LOG_SIZE = 32;
static HitLog          hitLog[LOG_SIZE];
static volatile uint8_t logHead = 0, logTail = 0;

static void logHit(uint8_t step, uint8_t hit, uint8_t count, uint8_t level, uint8_t gate) {
  uint8_t next = (logHead + 1) % LOG_SIZE;
  if (next == logTail) return;         // voll -> verwerfen
  hitLog[logHead] = { step, hit, count, level, gate, loopCount };
  logHead = next;
}

bool seqPopHit(HitLog& h) {
  if (logTail == logHead) return false;
  noInterrupts();
  h = hitLog[logTail];
  logTail = (logTail + 1) % LOG_SIZE;
  interrupts();
  return true;
}

// Rampe: Schlag i von n bekommt einen Anteil von 100 % bis (100 - |ramp|) %
//   ramp > 0: erster Schlag voll, letzter am leisesten (Daempfung)
//   ramp < 0: erster am leisesten, letzter voll (Crescendo)
static uint8_t rampScale(uint8_t value, int8_t ramp, uint8_t i, uint8_t n) {
  if (n <= 1 || ramp == 0) return value;
  int32_t span = n - 1;
  int32_t k    = ramp > 0 ? i : (span - i);          // Abstand vom vollen Wert
  int32_t pct  = 100 - (int32_t)abs(ramp) * k / span;
  return (uint8_t)((int32_t)value * pct / 100);
}

// --- Gate ---
static void gateWrite(bool on) {
  gateHigh = on;
  digitalWriteFast(PIN_GATE, on);
}

static void gateOffIsr() {
  gateTimer.end();
  gateWrite(false);
}

// Gate-Laenge in Prozent einer Zeitspanne (Step oder Ratchet-Teilschlag)
static uint32_t gateLengthUs(uint8_t percent, uint32_t spanUs) {
  uint32_t len = (uint64_t)spanUs * percent / 100;
  if (spanUs > GATE_GAP_US + GATE_MIN_US && len > spanUs - GATE_GAP_US) len = spanUs - GATE_GAP_US;
  if (len < GATE_MIN_US) len = GATE_MIN_US;
  return len;
}

// Einen Schlag ausloesen. holdOpen = Gate bleibt offen (Legato auf dem letzten Schlag)
static void startHit(bool newNote, bool holdOpen, uint8_t gatePct, uint32_t spanUs) {
  if (newNote) {
    if (gateHigh) {                    // Gate noch offen -> kurz schliessen
      gateWrite(false);
      delayMicroseconds(RETRIG_GAP_US);
    }
    gateWrite(true);
  }
  gateTimer.end();
  if (!holdOpen) gateTimer.begin(gateOffIsr, gateLengthUs(gatePct, spanUs));
}

// Schlag ausgeben: beim ersten Schlag Pitch, Level und CV, bei Ratchets nur Level
// spanUs / allowSlide nur beim ersten Schlag relevant
static void outputHit(uint8_t step, uint8_t hit, uint8_t n, uint8_t level, uint8_t gate,
                      uint32_t spanUs = 0, bool allowSlide = false) {
  if (hit == 0) {
    const Step& st = pat->steps[step];
    // erst in die Skala ziehen, dann transponieren (Transpose verschiebt die Tonart mit)
    int p = constrain((int)quantize(st.pitch, pat->scale, pat->root) + pat->transpose, 0, 95);
    uint16_t target = pitchToCode((uint8_t)p);

    slideTimer.end();
    uint16_t startCode = target;
    if (allowSlide && st.slide > 0 && curPitchCode != target) {
      uint32_t durUs = (uint64_t)spanUs * st.slide / 100;
      slideTicks = max(1UL, durUs / SLIDE_TICK_US);
      slideTick  = 0;
      slideFrom  = curPitchCode;
      slideTo    = target;
      startCode  = curPitchCode;               // Start beim alten Ton
      slideTimer.begin(slideIsr, SLIDE_TICK_US);
    }
    curPitchCode = startCode;
    outStep(startCode, level, st.cv);
  } else {
    outLevel(level);
  }
  logHit(step, hit, n, level, gate);
}

static void ratchetIsr() {
  uint8_t i    = ratchetIdx++;
  bool    last = (ratchetLeft == 1);
  uint8_t lvl  = rampScale(ratchetLevel, ratchetRamp, i, ratchetN);
  uint8_t gat  = rampScale(ratchetGate,  ratchetRamp, i, ratchetN);
  outputHit(ratchetStep, i, ratchetN, lvl, gat);
  startHit(true, last && ratchetTie, gat, ratchetSubUs);
  if (--ratchetLeft == 0) ratchetTimer.end();
}

// --- Step-Auswahl ---
static uint8_t startStep() {
  return pat->direction == DIR_REV ? pat->length - 1 : 0;
}

static uint8_t nextStep(int cur) {
  uint8_t len = pat->length;
  if (cur < 0 || cur >= len) return startStep();   // Start oder Laenge verkuerzt

  switch (pat->direction) {
    case DIR_FWD:
      return (cur + 1) % len;
    case DIR_REV:
      return cur == 0 ? len - 1 : cur - 1;
    case DIR_PINGPONG:
      if (len == 1) return 0;
      if (cur + pingDir >= len || cur + pingDir < 0) pingDir = -pingDir;
      return cur + pingDir;
    case DIR_RANDOM:
      return random(len);
  }
  return 0;
}

// --- Bedingte Trigs ---
static bool condOk(uint8_t cond) {
  if (cond == COND_ALWAYS)    return true;
  if (cond == COND_FIRST)     return loopCount == 0;
  if (cond == COND_NOT_FIRST) return loopCount > 0;
  uint8_t a, b;
  condRatio(cond, a, b);
  return (loopCount % b) == (uint32_t)(a - 1);
}

// --- Step ausgeben ---
static void playStep(uint8_t s, bool retrigger, uint32_t spanUs) {
  playhead = s;
  digitalWriteFast(PIN_LED_START, s == 0);

  const Step& st = pat->steps[s];
  bool plays = st.on
            && condOk(st.cond)
            && (st.prob >= 100 || (uint8_t)random(100) < st.prob);

  gateTimer.end();
  ratchetTimer.end();
  ratchetLeft = 0;

  if (!plays) {                        // stumm oder per Probability ausgefallen
    gateWrite(false);
    tieActive = false;
    return;
  }


  // Ratchet? RPR = Wahrscheinlichkeit, dass der Step ratchet (sonst Einzelschlag)
  uint8_t n = st.ratchet;
  if (n > 1 && st.ratProb < 100 && (uint8_t)random(100) >= st.ratProb) n = 1;

  uint32_t subUs = spanUs / n;          // spanUs = tatsaechliche Dauer dieses Steps (mit Swing)

  // erster Schlag: neue Note, ausser Legato vom vorigen Step (Gate bleibt dann offen)
  uint8_t lvl0 = rampScale(st.level, st.ramp, 0, n);
  uint8_t gat0 = rampScale(st.gate,  st.ramp, 0, n);
  outputHit(s, 0, n, lvl0, gat0, spanUs, !retrigger);   // nach Reset kein Slide
  startHit(retrigger || !tieActive, n == 1 && st.tie, gat0, subUs);
  tieActive = st.tie;

  if (n > 1) {                         // restliche Schlaege per Timer
    ratchetLeft  = n - 1;
    ratchetSubUs = subUs;
    ratchetGate  = st.gate;
    ratchetTie   = st.tie;
    ratchetN     = n;
    ratchetIdx   = 1;
    ratchetLevel = st.level;
    ratchetRamp  = st.ramp;
    ratchetStep  = s;
    ratchetTimer.begin(ratchetIsr, subUs);
  }
}

// --- Swing ---
static uint32_t stepPeriodUs() {
  return clockPeriodUs * pat->clockDiv;
}

// Verzoegerung des Off-Beats: 50 % = 0, 75 % = halbe Step-Dauer
static uint32_t swingDelayUs() {
  return (uint64_t)stepPeriodUs() * (pat->swing - 50) / 50;
}

static void swingIsr() {
  swingTimer.end();
  int16_t s = swingPending;
  swingPending = -1;
  if (s >= 0) playStep(s, false, stepPeriodUs() - swingDelayUs());
}

// --- Oeffentliche Funktionen ---
void seqBegin(Pattern* p) {
  pat = p;
  outBegin();
  pinMode(PIN_GATE, OUTPUT);
  pinMode(PIN_LED_START, OUTPUT);
  gateWrite(false);
  resetAt = micros() - RESET_GUARD_US;
}

void seqClock() {
  uint32_t now = micros();
  uint32_t period = now - lastClockUs;
  if (period < EDGE_GUARD_US) return;          // Prellen / Mehrfachflanke
  lastClockUs = now;
  if (period < 4000000) clockPeriodUs = period;   // lange Pause (Stopp) nicht als Tempo werten

  if (now - resetAt < RESET_GUARD_US) return;   // gleichzeitiger Reset + Clock
  if (++divCount < pat->clockDiv) return;
  divCount = 0;

  // noch nicht gespielter Swing-Step (Tempo ploetzlich schneller) -> jetzt nachholen
  if (swingPending >= 0) {
    swingTimer.end();
    uint8_t p = swingPending;
    swingPending = -1;
    playStep(p, false, stepPeriodUs() - swingDelayUs());
  }

  int     cur = playhead;
  uint8_t nxt = nextStep(cur);

  // neuer Durchlauf? Bei Zufall nach "Laenge" Steps, sonst bei Rueckkehr zum Start-Step
  if (cur >= 0) {
    bool newPass = (pat->direction == DIR_RANDOM) ? (++stepsInPass >= pat->length)
                                                  : (nxt == startStep());
    if (newPass) {
      loopCount++;
      stepsInPass = 0;
    }
  }
  offbeat = !offbeat;
  uint32_t T = stepPeriodUs();
  uint32_t d = swingDelayUs();

  if (offbeat && d > 0) {              // Off-Beat: verzoegert spielen
    swingPending = nxt;
    swingTimer.begin(swingIsr, d);
  } else {
    playStep(nxt, false, offbeat ? T : T + d);   // On-Beat dauert bis zum verzoegerten Off-Beat
  }
}

void seqReset() {
  if (micros() - resetAt < EDGE_GUARD_US) return;   // Prellen / Mehrfachflanke
  resetAt  = micros();
  divCount = 0;
  pingDir  = 1;
  loopCount   = 0;
  stepsInPass = 0;
  swingTimer.end();
  swingPending = -1;
  offbeat      = false;                // Reset-Step ist On-Beat
  playStep(startStep(), true, stepPeriodUs() + swingDelayUs());   // Gate schliesst kurz, Step 1 neu
}
