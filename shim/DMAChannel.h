// Minimal DMAChannel stub (never actually transfers; emu drivers bypass DMA).
#pragma once

#include <cstdint>
#include <cstring>
#include "imxrt.h"

#define DMAMUX_SOURCE_ADC_ETC 0
#define DMAMUX_SOURCE_FLEXIO2_REQUEST0 1

class DMABaseClass {
 public:
  typedef struct __attribute__((packed, aligned(4))) {
    volatile const void* volatile SADDR;
    int16_t SOFF;
    union { uint16_t ATTR; struct { uint8_t ATTR_DST; uint8_t ATTR_SRC; }; };
    union {
      uint32_t NBYTES;
      uint32_t NBYTES_MLNO;
      uint32_t NBYTES_MLOFFNO;
      uint32_t NBYTES_MLOFFYES;
    };
    int32_t SLAST;
    volatile void* volatile DADDR;
    int16_t DOFF;
    union { volatile uint16_t CITER; volatile uint16_t CITER_ELINKYES; volatile uint16_t CITER_ELINKNO; };
    int32_t DLASTSGA;
    volatile uint16_t CSR;
    union { volatile uint16_t BITER; volatile uint16_t BITER_ELINKYES; volatile uint16_t BITER_ELINKNO; };
  } TCD_t;

  TCD_t* TCD;

  DMABaseClass() { TCD = &tcd_storage_; memset(TCD, 0, sizeof(TCD_t)); }

  void begin(bool force_initialization = false) { (void)force_initialization; }
  void enable() {}
  void disable() {}
  bool complete() { return false; }
  void clearComplete() {}
  void clearError() {}
  void clearInterrupt() {}
  void attachInterrupt(void (*isr)(void)) { (void)isr; }
  void detachInterrupt() {}
  void interruptAtCompletion() {}
  void disableOnCompletion() {}
  void triggerAtHardwareEvent(uint8_t source) { (void)source; }
  void triggerAtTransfersOf(DMABaseClass&) {}
  void triggerAtCompletionOf(DMABaseClass&) {}

 private:
  TCD_t tcd_storage_;
};

class DMAChannel : public DMABaseClass {
 public:
  DMAChannel() {}
  explicit DMAChannel(bool allocate) { (void)allocate; }
};
