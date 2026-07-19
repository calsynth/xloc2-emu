// Emulator equivalent of firmware Main.cpp (which is excluded from the
// build): replicates setup(), the CORE/UI timer ISRs and one iteration of
// loop(), minus USB/SD/audio-hardware specifics.
#include <Arduino.h>
#include <EEPROM.h>

#include "OC_core.h"
#include "OC_app_switcher.h"
#include "OC_apps.h"
#include "OC_DAC.h"
#include "OC_debug.h"
#include "OC_gpio.h"
#include "OC_global_settings.h"
#include "OC_ADC.h"
#include "OC_calibration.h"
#include "OC_digital_inputs.h"
#include "OC_menus.h"
#include "OC_strings.h"
#include "OC_ui.h"
#include "OC_options.h"
#include "src/drivers/display.h"
#include "util/util_debugpins.h"
#include "HSMIDI.h"
#include "PhzConfig.h"
#include "AudioIO.h"
#include <Wire.h>
#include <SD.h>

extern USBHost thisUSB;

#include "emu.h"
#include "emu_internal.h"

uint_fast8_t MENU_REDRAW = true;
static OC::UiMode ui_mode = OC::UI_MODE_MENU;
static OC::IOFrame io_frame;

/* ------------------------ UI timer ISR --------------------------- */

IntervalTimer UI_timer;

static void UI_timer_ISR() {
  OC::ui.Poll();
}

/* ------------------------ core timer ISR ------------------------- */

IntervalTimer CORE_timer;
volatile bool OC::CORE::app_isr_enabled = false;
volatile bool OC::CORE::display_update_enabled = false;
volatile bool OC::CORE::app_loop_enabled = false;
volatile uint32_t OC::CORE::ticks = 0;

static void CORE_timer_ISR() {
  using namespace OC;

  // Mirrors firmware Main.cpp exactly:
  display::Flush();
  DAC::Update();
  display::Update();

  OC::ADC::Scan_DMA();
  DigitalInputs::Scan();

  ++CORE::ticks;
  if (CORE::app_isr_enabled) {
    OC::app_switcher.Process(&io_frame);
  }
}

/* ------------------------ setup() -------------------------------- */

namespace emu {

void firmware_setup() {
  delay(50);
  Serial.begin(9600);

  OC::Pinout_Detect();

  SPI_init();
  SERIAL_PRINTLN("* O&C BOOTING... (XLOC2 emulator)");
  SERIAL_PRINTLN("* %s", OC::Strings::VERSION);

  OC::DEBUG::Init();
  OC::DigitalInputs::Init();

  if (DAC8568_Uses_SPI) {
    OC::DAC::DAC8568_Vref_enable();
  }
  if (ADC33131D_Uses_FlexIO) {
    OC::ADC::ADC33131D_Vref_calibrate();  // instant no-op on host
  } else {
    delay(400);
  }

  OC::calibration_load();
  OC::SetFlipMode(OC::calibration_data.flipcontrols());

  Wire.begin();
  Wire.setClock(100000);

  OC::ADC::Init(&OC::calibration_data.adc, OC::calibration_data.flipcontrols());
  OC::ADC::Init_DMA();
  OC::DAC::Init(&OC::calibration_data.dac,
                &OC::global_settings.autotune_calibration_data,
                OC::calibration_data.flipcontrols());

  display::AdjustOffset(OC::calibration_data.display_offset);
  display::SetFlipMode(OC::calibration_data.flipscreen());
  display::Init();

  GRAPHICS_BEGIN_FRAME(true);
  GRAPHICS_END_FRAME();

  OC::ui.Init();
  OC::ui.configure_encoders(OC::calibration_data.encoder_config());

  SERIAL_PRINTLN("* CORE ISR @%luus", OC_CORE_TIMER_RATE);
  io_frame.Reset();
  CORE_timer.begin(CORE_timer_ISR, OC_CORE_TIMER_RATE);
  CORE_timer.priority(OC_CORE_TIMER_PRIO);

  // Wait until there's at least some ADC values read
  delay(4);
  uint32_t random_seed =
      OC::ADC::raw_value(ADC_CHANNEL_1) * OC::ADC::raw_value(ADC_CHANNEL_2) +
      OC::ADC::raw_value(ADC_CHANNEL_3) + OC::ADC::raw_value(ADC_CHANNEL_4);
  randomSeed(random_seed);

  SERIAL_PRINTLN("* UI ISR @%luus", OC_UI_TIMER_RATE);
  UI_timer.begin(UI_timer_ISR, OC_UI_TIMER_RATE);
  UI_timer.priority(OC_UI_TIMER_PRIO);

  // first sign of life
  GRAPHICS_BEGIN_FRAME(true);
  graphics.setPrintPos(1, 28);
  graphics.print("*Main Screen Turn On*");
  GRAPHICS_END_FRAME();

  // Standard MIDI on Serial8 / USB host: emulated no-ops
  if (MIDI_Uses_Serial8) {
    MIDI1.begin(MIDI_CHANNEL_OMNI);
  }
  thisUSB.begin();

  // SD card: host-directory-backed shim
  SDcard_Ready = SD.begin(BUILTIN_SDCARD);

  if (I2S2_Audio_ADC && I2S2_Audio_DAC) {
    OC::AudioIO::Init();
  }

  // initialize LittleFS for config files
  PhzConfig::Init();

  // Display loading splash screen and optional calibration
  bool reset_settings = false;
  ui_mode = OC::ui.Splashscreen(reset_settings, 0);

  bool start_cal = false;
  if (ui_mode == OC::UI_MODE_CALIBRATE) {
    start_cal = true;
    ui_mode = OC::UI_MODE_MENU;
  }
  OC::ui.set_screensaver_timeout(OC::calibration_data.screensaver_timeout);

  // use default global config file in LFS
  bool firstrun = !PhzConfig::load_config();

  // initialize apps
  OC::app_switcher.Init(reset_settings || firstrun);

  // Welcome splash
  OC::ui.Splashscreen(firstrun, 1);

  if (start_cal) OC::start_calibration();

  OC::app_switcher.current_app()->DispatchAppEvent(OC::APP_EVENT_RESUME);

  SERIAL_PRINTLN("[End of setup()]");

  // Firmware loop() sets these on entry; loop_once() is called repeatedly so
  // set them once here.
  OC::CORE::app_isr_enabled = true;
  OC::CORE::display_update_enabled = true;
  OC::CORE::app_loop_enabled = true;
}

/* ------------------------ loop(), one iteration ------------------- */

void run_loop_once() {
  using namespace OC;
  static uint32_t last_redraw_time = 0;

  // Refresh display
  if (MENU_REDRAW && CORE::display_update_enabled) {
    GRAPHICS_BEGIN_FRAME(false);  // Don't busy wait

    if (UI_MODE_APP_SETTINGS == ui_mode) {
      ui.AppSettings(true);
    } else {
      app_switcher.current_app()->Draw(ui_mode);
    }

    MENU_REDRAW = 0;
    last_redraw_time = ui.ticks();
    GRAPHICS_END_FRAME();
  }

  // Run current app
  if (CORE::app_loop_enabled) app_switcher.current_app()->DispatchLoop();

  // Take care of queued tasks
  OC::CORE::FlushTasks();

  // UI events
  if (UI_MODE_APP_SETTINGS == ui_mode) {
    if (!ui.AppSettings(false)) {
      ui_mode = UI_MODE_MENU;
    }
  } else {
    UiMode mode = ui.DispatchEvents(app_switcher.current_slot());

    if (mode != ui_mode) {
      if (UI_MODE_SCREENSAVER == mode)
        app_switcher.current_app()->DispatchAppEvent(APP_EVENT_SCREENSAVER_ON);
      else if (UI_MODE_SCREENSAVER == ui_mode)
        app_switcher.current_app()->DispatchAppEvent(APP_EVENT_SCREENSAVER_OFF);
      else if (UI_MODE_APP_SETTINGS == mode)
        app_switcher.current_app()->DispatchAppEvent(APP_EVENT_SUSPEND);

      ui_mode = mode;
    }
  }

  if (ui.ticks() - last_redraw_time > REDRAW_TIMEOUT_MS) MENU_REDRAW = 1;

  // Flush any EEPROM writes to disk
  emu::eeprom_flush();
}

}  // namespace emu
