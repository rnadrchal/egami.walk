#include "storage.h"
#include <SD.h>

struct FileHeader {
  char    magic[4];   // "SEQ!"
  uint8_t version;    // PATTERN_VERSION
  uint8_t reserved[3];
};

static bool ready = false;
static bool used[SLOT_MAX + 1];   // Cache: Slot belegt?

static void slotPath(uint8_t slot, char* buf) {
  snprintf(buf, 16, "/SEQ/P%02d.SEQ", slot);
}

bool storageBegin() {
  ready = SD.begin(BUILTIN_SDCARD);
  if (!ready) return false;
  if (!SD.exists("/SEQ")) SD.mkdir("/SEQ");

  char path[16];
  for (uint8_t s = SLOT_MIN; s <= SLOT_MAX; s++) {
    slotPath(s, path);
    used[s] = SD.exists(path);
  }
  return true;
}

bool storageReady() { return ready; }

bool storageUsed(uint8_t slot) {
  return ready && slot >= SLOT_MIN && slot <= SLOT_MAX && used[slot];
}

bool storageSave(uint8_t slot, const Pattern& p) {
  if (!ready) return false;
  char path[16];
  slotPath(slot, path);
  if (SD.exists(path)) SD.remove(path);   // FILE_WRITE haengt sonst an

  File f = SD.open(path, FILE_WRITE);
  if (!f) return false;

  FileHeader h = { {'S','E','Q','!'}, PATTERN_VERSION, {0,0,0} };
  bool ok = f.write((const uint8_t*)&h, sizeof(h)) == sizeof(h)
         && f.write((const uint8_t*)&p, sizeof(p)) == sizeof(p);
  f.close();

  used[slot] = ok;
  return ok;
}

bool storageLoad(uint8_t slot, Pattern& p) {
  if (!ready || !storageUsed(slot)) return false;
  char path[16];
  slotPath(slot, path);

  File f = SD.open(path, FILE_READ);
  if (!f) return false;

  FileHeader h;
  Pattern tmp;
  bool ok = f.size() == sizeof(h) + sizeof(tmp)
         && f.read((uint8_t*)&h, sizeof(h)) == sizeof(h)
         && memcmp(h.magic, "SEQ!", 4) == 0
         && h.version == PATTERN_VERSION
         && f.read((uint8_t*)&tmp, sizeof(tmp)) == sizeof(tmp);
  f.close();
  if (!ok) return false;

  noInterrupts();          // Sequencer-Interrupt liest das Pattern mit
  p = tmp;
  interrupts();
  return true;
}
