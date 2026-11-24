#pragma once
#include <Arduino.h>
#include <Encoder.h>

// ---------------------------------------------------------------
// Eingabe: Encoder-Delta pro Rastung, Taster mit Entprellung
// und Unterscheidung kurz / lang
// ---------------------------------------------------------------

const int ENC_DIV = 4;   // Zaehlschritte pro Rastung (KY-040: 4)

class EncoderInput {
public:
  EncoderInput(uint8_t pinA, uint8_t pinB) : enc(pinA, pinB) {}

  // Rastungen seit dem letzten Aufruf (+ = im Uhrzeigersinn)
  int delta() {
    long raw = enc.read();
    long d = (raw - base) / ENC_DIV;
    if (d != 0) base += d * ENC_DIV;
    return (int)d;
  }

  // wie delta(), aber schnelles Drehen wird vervielfacht
  int deltaAccel() {
    int d = delta();
    if (d == 0) return 0;
    uint32_t now = millis();
    uint32_t dt  = now - lastMoveMs;
    lastMoveMs = now;
    if (dt < ACCEL_FAST_MS) return d * ACCEL_FAST;
    if (dt < ACCEL_MID_MS)  return d * ACCEL_MID;
    return d;
  }

  // Abstand zwischen zwei Rastungen -> Faktor
  static const uint32_t ACCEL_FAST_MS = 40;
  static const uint32_t ACCEL_MID_MS  = 100;
  static const int      ACCEL_FAST    = 8;
  static const int      ACCEL_MID     = 3;

private:
  Encoder  enc;
  long     base = 0;
  uint32_t lastMoveMs = 0;
};

enum ButtonEvent : uint8_t { BTN_NONE, BTN_SHORT, BTN_LONG };

class Button {
public:
  static const uint32_t DEBOUNCE_MS = 20;
  static const uint32_t LONG_MS     = 600;

  explicit Button(uint8_t pin) : pin(pin) {}

  void begin() { pinMode(pin, INPUT_PULLUP); }

  // einmal pro loop() aufrufen
  ButtonEvent update() {
    bool raw = (digitalRead(pin) == LOW);   // gedrueckt = LOW
    uint32_t now = millis();

    if (raw != lastRaw) {            // Pegel hat sich geaendert -> Prellzeit neu starten
      lastRaw = raw;
      changedAt = now;
    }
    if (now - changedAt < DEBOUNCE_MS) return BTN_NONE;

    if (raw && !pressed) {           // stabil gedrueckt
      pressed = true;
      pressedAt = now;
      longFired = false;
    } else if (!raw && pressed) {    // stabil losgelassen
      pressed = false;
      if (!longFired) return BTN_SHORT;
    } else if (pressed && !longFired && now - pressedAt >= LONG_MS) {
      longFired = true;              // lang: feuert schon waehrend des Haltens
      return BTN_LONG;
    }
    return BTN_NONE;
  }

private:
  uint8_t  pin;
  bool     lastRaw   = false;
  bool     pressed   = false;
  bool     longFired = false;
  uint32_t changedAt = 0;
  uint32_t pressedAt = 0;
};
