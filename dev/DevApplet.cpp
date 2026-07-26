// ============================================================================
//  YOUR APPLET GOES HERE.
//
//  This file is its own translation unit, so editing it costs ~2 seconds
//  instead of the ~34 seconds a change under firmware/.../applets/ costs.
//  Rebuild with:   cmake --build build --target phz_core
//  The app reloads the core automatically.
//
//  Select it on the module as the applet named "Dev".
//
//  Write the class exactly as you would a real applet — the same macros and
//  helpers are in scope. When you are happy with it:
//    1. copy the class (and its #defines) into
//       firmware/software/src/applets/YourName.h
//    2. add  #include "YourName.h"  and a  DeclareApplet<YourName, <free id>,
//       CAT_...>  line to applets/_config.h
//    3. one full rebuild to confirm it still compiles inside the registry
//  See applets/Boilerplate.h for the canonical authoring notes.
// ============================================================================

#include "HemisphereApplet.h"

using namespace HS;

// The same name/icon rewriting hack applets/_config.h installs, so the class
// below is written in ordinary applet style and copies across verbatim.
// (It turns `const char* applet_name() { return "X"; }` into a const final
// override plus the static constexpr the applet registry wants.)
#define applet_name applet_name() const final { return applet_name_(); } \
  static constexpr const char* applet_name_

#define applet_icon applet_icon() const final { return applet_icon_(); } \
  static constexpr const uint8_t* applet_icon_

// ============================================================================
//  ---------------------------- YOUR APPLET ---------------------------------
// ============================================================================

#define DEV_MAX_RATE 63

class DevApplet : public HemisphereApplet {
public:

    const char* applet_name() {
        return "Shaka";
    }
    const uint8_t* applet_icon() { return ZAP_ICON; }

    void Start() {
        ForEachChannel(ch) {
            rise[ch] = 8;
            fall[ch] = 8;
            current[ch] = 0;
        }
        cursor = 0;
    }

    // Called from the core ISR at 16.6 kHz — keep it cheap.
    void Controller() {
        ForEachChannel(ch) {
            const int target = In(ch);
            const int rate = (target > current[ch]) ? rise[ch] : fall[ch];

            // rate 0 = instant, DEV_MAX_RATE = slowest
            if (rate == 0) {
                current[ch] = target;
            } else {
                const int step = (HEMISPHERE_MAX_CV / 64) / rate + 1;
                if (target > current[ch])
                    current[ch] = min(target, current[ch] + step);
                else
                    current[ch] = max(target, current[ch] - step);
            }

            Out(ch, current[ch]);
        }
    }

    void View() {
        DrawInterface();
    }

    void OnEncoderMove(int direction) {
        if (!EditMode()) {
            MoveCursor(cursor, direction, 3);
            return;
        }
        const uint8_t ch = cursor / 2;
        if (cursor & 1) fall[ch] = constrain(fall[ch] + direction, 0, DEV_MAX_RATE);
        else            rise[ch] = constrain(rise[ch] + direction, 0, DEV_MAX_RATE);
    }

    uint64_t OnDataRequest() {
        uint64_t data = 0;
        Pack(data, PackLocation {0,6}, rise[0]);
        Pack(data, PackLocation {6,6}, fall[0]);
        Pack(data, PackLocation {12,6}, rise[1]);
        Pack(data, PackLocation {18,6}, fall[1]);
        return data;
    }

    void OnDataReceive(uint64_t data) {
        rise[0] = constrain(Unpack(data, PackLocation {0,6}), 0, DEV_MAX_RATE);
        fall[0] = constrain(Unpack(data, PackLocation {6,6}), 0, DEV_MAX_RATE);
        rise[1] = constrain(Unpack(data, PackLocation {12,6}), 0, DEV_MAX_RATE);
        fall[1] = constrain(Unpack(data, PackLocation {18,6}), 0, DEV_MAX_RATE);
    }

    // NOTE: public here (real applets keep it protected behind
    // APPLET_INTERFACE) so DevSlot can forward the help screen to it.
    void SetHelp() {
        //                    "-------" <-- Label size guide
        help[HELP_DIGITAL1] = "";
        help[HELP_DIGITAL2] = "";
        help[HELP_CV1]      = "Sig Ch1";
        help[HELP_CV2]      = "Sig Ch2";
        help[HELP_OUT1]     = "Slew 1";
        help[HELP_OUT2]     = "Slew 2";
        help[HELP_EXTRA1] = "Rise / Fall time";
        help[HELP_EXTRA2] = "  0 = instant";
        //                  "---------------------" <-- Extra text size guide
    }

private:
    int cursor;
    int rise[2];
    int fall[2];
    int current[2];

    void DrawInterface() {
        ForEachChannel(ch) {
            const int y = 15 + (ch * 22);

            gfxPrint(0, y, OutputLabel(ch));

            gfxPrint(18, y, "R");
            gfxPrint(rise[ch]);
            if (cursor == ch * 2) gfxCursor(24, y + 8, 14, "Rise");

            gfxPrint(44, y, "F");
            gfxPrint(fall[ch]);
            if (cursor == ch * 2 + 1) gfxCursor(50, y + 8, 14, "Fall");

            // live level bar
            const int w = Proportion(abs(current[ch]), HEMISPHERE_MAX_CV, 60);
            gfxLine(1, y + 12, 1 + constrain(w, 0, 60), y + 12);
        }
    }
};

// ============================================================================
//  ------------------------------- GLUE -------------------------------------
//  Leave this alone. DevSlot (applets/DevSlot.h) calls it to build an
//  instance; the registry makes one DevSlot per hemisphere slot, so this may
//  be called more than once.
// ============================================================================

HemisphereApplet* xloc_dev_applet_create() {
    return new DevApplet();
}

// SetHelp() is protected on HemisphereApplet, so DevSlot cannot call it
// through a base pointer. Yours is public (see the note above it), which lets
// this shim reach it.
void xloc_dev_applet_set_help(HemisphereApplet* a) {
    static_cast<DevApplet*>(a)->SetHelp();
}
