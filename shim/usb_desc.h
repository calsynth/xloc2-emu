// usb_desc.h stub: pretend we're a plain USB MIDI build (no MTP, no audio
// interface) so optional firmware features gated on USB descriptors are off.
#pragma once

#define MIDI_INTERFACE 1
// deliberately NOT defined: MTP_INTERFACE, AUDIO_INTERFACE, SEREMU_INTERFACE
