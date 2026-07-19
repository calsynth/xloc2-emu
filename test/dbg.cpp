#include <cstdio>
#include "../core/emu.h"
#include <Arduino.h>
#include "OC_digital_inputs.h"

static void run_ms(uint64_t ms) {
  for (uint64_t i = 0; i < ms; ++i) { emu::step_us(1000); emu::run_loop_once(); }
}

int main() {
  emu::boot("./state_dbg");
  run_ms(4000);
  // trigger input test: pulse TR1 and check the edge is seen by Scan()
  uint32_t before = OC::DigitalInputs::rising_edges();
  emu::set_trigger_in(0, true);
  emu::step_us(200);  // let a couple of core ISRs run Scan()
  bool raised = OC::DigitalInputs::read_immediate(OC::DIGITAL_INPUT_1);
  emu::set_trigger_in(0, false);
  printf("TR1 raised while high: %d (expect 1)\n", raised);
  // rising_edges_ is cleared each Scan; test by watching over one tick
  emu::set_trigger_in(0, true);
  emu::step_us(60);
  printf("TR1 rising edge seen: %d (expect nonzero)\n",
         OC::DigitalInputs::rising_edges() & 1);
  emu::set_trigger_in(0, false);
  return 0;
}
