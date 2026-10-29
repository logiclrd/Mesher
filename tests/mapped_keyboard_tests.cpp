#include "mesher/mapped_keyboard.hpp"

#include <stdexcept>
#include <iostream>

using mesher::MappedKeyboard;

namespace {
void check(bool okay,const char* what) {
    if(!okay)throw std::runtime_error(what);
}
void mappedAZERTYAndDvorak() {
    MappedKeyboard keys;
    // The A printed on an AZERTY keyboard comes from a different US key.
    keys.onKey(GLFW_KEY_Q,GLFW_PRESS,"a");
    check(keys.down(GLFW_KEY_A) && !keys.down(GLFW_KEY_Q),
          "Key actions must follow mapped A rather than QWERTY position");
    check(keys.takePress(GLFW_KEY_A) && !keys.takePress(GLFW_KEY_A),
          "Mapped letter must emit exactly one action edge");
    keys.onKey(GLFW_KEY_Q,GLFW_REPEAT,"a");
    check(!keys.takePress(GLFW_KEY_A),"Repeated key events must not toggle");
    keys.onKey(GLFW_KEY_Q,GLFW_RELEASE,"q");
    check(!keys.down(GLFW_KEY_A),"Release must use press-time mapping");

    // Two physical keys with the same layout name must not drop the
    // semantic held state until both have been released.
    keys.onKey(GLFW_KEY_W,GLFW_PRESS,"z");
    keys.onKey(GLFW_KEY_Y,GLFW_PRESS,"Z");
    check(keys.down(GLFW_KEY_Z),"Two physical sources may map to Z");
    keys.onKey(GLFW_KEY_W,GLFW_RELEASE);
    check(keys.down(GLFW_KEY_Z),"First release must not cancel second key");
    keys.onKey(GLFW_KEY_Y,GLFW_RELEASE);
    check(!keys.down(GLFW_KEY_Z),"Last release must clear logical key");
}
void shortcutsAndModifierKeys() {
    MappedKeyboard keys;
    keys.onKey(GLFW_KEY_LEFT_CONTROL,GLFW_PRESS);
    keys.onKey(GLFW_KEY_Q,GLFW_PRESS,"s");
    check(keys.down(GLFW_KEY_LEFT_CONTROL)&&keys.takePress(GLFW_KEY_S),
          "Ctrl+mapped S must activate the Save shortcut");
    check(!keys.down(GLFW_KEY_Q),"Save must not use physical Q");
    keys.onKey(GLFW_KEY_Q,GLFW_RELEASE,"q");
    keys.onKey(GLFW_KEY_LEFT_CONTROL,GLFW_RELEASE);
    keys.onKey(GLFW_KEY_LEFT_SUPER,GLFW_PRESS);
    keys.onKey(GLFW_KEY_Z,GLFW_PRESS,"y");
    check(keys.down(GLFW_KEY_LEFT_SUPER)&&keys.takePress(GLFW_KEY_Y),
          "Command/Super+mapped Y must be available to macOS shortcut policy");
    check(!keys.takePress(GLFW_KEY_Z),"Physical Z is not mapped Z");
}
void specialKeysAndFocus() {
    MappedKeyboard keys;
    keys.onKey(GLFW_KEY_ESCAPE,GLFW_PRESS);
    keys.onKey(GLFW_KEY_F8,GLFW_PRESS);
    keys.onKey(GLFW_KEY_PAGE_DOWN,GLFW_PRESS);
    keys.onKey(GLFW_KEY_SPACE,GLFW_PRESS);
    check(keys.down(GLFW_KEY_ESCAPE)&&keys.down(GLFW_KEY_F8)&&
          keys.down(GLFW_KEY_PAGE_DOWN)&&keys.down(GLFW_KEY_SPACE),
          "Nonprintable controls and Space must retain their binding");
    keys.clear(); // A window losing focus must never retain stale input.
    check(!keys.down(GLFW_KEY_ESCAPE)&&!keys.down(GLFW_KEY_F8)&&
          !keys.down(GLFW_KEY_SPACE)&&!keys.takePress(GLFW_KEY_PAGE_DOWN),
          "Focus loss must clear state and queued press edges");
    keys.onKey(GLFW_KEY_F8,GLFW_RELEASE);
    check(!keys.down(GLFW_KEY_F8),
          "Synthetic release after focus-loss must be harmless");
}
void invalidNamesDoNotBecomePhysicalShortcuts() {
    MappedKeyboard keys;
    keys.onKey(GLFW_KEY_A,GLFW_PRESS,"");
    keys.onKey(GLFW_KEY_B,GLFW_PRESS,"é");
    keys.onKey(GLFW_KEY_C,GLFW_PRESS,"ab");
    keys.onKey(GLFW_KEY_UNKNOWN,GLFW_PRESS,"a");
    check(!keys.down(GLFW_KEY_A)&&!keys.down(GLFW_KEY_B)&&
          !keys.down(GLFW_KEY_C),
          "Unmapped/dead/non-ASCII keys must not fall back to US positions");
    // Quick taps remain visible even if the release arrives before draw.
    keys.onKey(GLFW_KEY_Q,GLFW_PRESS,"w");
    keys.onKey(GLFW_KEY_Q,GLFW_RELEASE);
    check(keys.takePress(GLFW_KEY_W),"Quick mapped tap must be delivered");
    check(!keys.takePress(GLFW_KEY_W),"Tap is consumed exactly once");
    keys.onKey(GLFW_KEY_Q,GLFW_PRESS,"n");
    keys.finishFrame();
    check(!keys.takePress(GLFW_KEY_N),
          "Unused mode-specific shortcut must not replay on a later frame");
}
}
int main() {
    try {
        mappedAZERTYAndDvorak();
        shortcutsAndModifierKeys();
        specialKeysAndFocus();
        invalidNamesDoNotBecomePhysicalShortcuts();
        std::cout<<"Layout-mapped key and modifier behavior: PASS\n";
    }catch(const std::exception& error) {
        std::cerr<<"Mapped keyboard regression: "<<error.what()<<'\n';
        return 1;
    }
}
