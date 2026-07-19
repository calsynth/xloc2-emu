#include <cstdio>
#include <cstring>
#include "../core/emu.h"
#include <Arduino.h>
#include "OC_ADC.h"
#include "OC_DAC.h"

static void run_ms(uint64_t ms) {
  for (uint64_t i = 0; i < ms; ++i) { emu::step_us(1000); emu::run_loop_once(); }
}

int main() {
  emu::boot("./state_dbg");
  run_ms(4000);

  // ADC: 1.000V on CV1 should give value ~409-410, pitch ~1536 (12<<7)
  emu::set_cv_in(0, 1.0f);
  run_ms(20);
  int32_t v = OC::ADC::value(ADC_CHANNEL_1);
  int32_t pitch = OC::ADC::value_to_pitch(v);
  printf("CV1=1.0V -> ADC value=%d pitch=%d (expect ~410, ~1536)\n", v, pitch);
  emu::set_cv_in(0, -2.0f);
  run_ms(20);
  v = OC::ADC::value(ADC_CHANNEL_1);
  printf("CV1=-2.0V -> ADC value=%d (expect ~-819)\n", v);

  // DAC: set octave values and read back voltage
  for (int oct = -2; oct <= 2; ++oct) {
    OC::DAC::set_octave(DAC_CHANNEL_A, oct);
    OC::DAC::Update();
    printf("DAC A set_octave(%d) -> %.4f V (code %u)\n", oct,
           emu::get_cv_out(0), OC::DAC::value((size_t)DAC_CHANNEL_A));
  }
  return 0;
}
