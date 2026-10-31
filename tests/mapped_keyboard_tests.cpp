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
void escapeSplashDismissesBeforeEditorExit() {
    MappedKeyboard ownerKeys;
    mesher::EscapeExitGuard exit;
    // The main Vulkan viewport can receive Escape while the splash is
    // active. It records the key for its regular input system, but the
    // splash consumes that entire press instead of exiting the editor.
    ownerKeys.onKey(GLFW_KEY_ESCAPE,GLFW_PRESS);
    exit.consumeUntilRelease(); // splash was present at the key callback
    check(ownerKeys.down(GLFW_KEY_ESCAPE) &&
          !exit.exitRequested(ownerKeys.down(GLFW_KEY_ESCAPE)),
          "Escape on owner must dismiss splash without exiting Mesher");
    ownerKeys.onKey(GLFW_KEY_ESCAPE,GLFW_REPEAT);
    check(!exit.exitRequested(ownerKeys.down(GLFW_KEY_ESCAPE)),
          "Holding/repeating splash Escape must not trigger exit later");
    ownerKeys.onKey(GLFW_KEY_ESCAPE,GLFW_RELEASE);
    check(!exit.exitRequested(ownerKeys.down(GLFW_KEY_ESCAPE)),
          "Release clears the splash Escape suppression without exiting");
    ownerKeys.onKey(GLFW_KEY_ESCAPE,GLFW_PRESS);
    check(exit.exitRequested(ownerKeys.down(GLFW_KEY_ESCAPE)),
          "A new Escape press after release must exit normally");
    ownerKeys.onKey(GLFW_KEY_ESCAPE,GLFW_RELEASE);
    check(!exit.exitRequested(ownerKeys.down(GLFW_KEY_ESCAPE)),
          "Release clears an ordinary Escape exit press");

    // Focus loss during the consumed splash press must not leave Escape
    // permanently blocked (same guard is used by the New Model dialog).
    ownerKeys.onKey(GLFW_KEY_ESCAPE,GLFW_PRESS);
    exit.consumeUntilRelease();
    ownerKeys.clear();
    check(!exit.exitRequested(ownerKeys.down(GLFW_KEY_ESCAPE)),
          "Focus loss clears a consumed Escape through the regular key state");
    ownerKeys.onKey(GLFW_KEY_ESCAPE,GLFW_PRESS);
    check(exit.exitRequested(ownerKeys.down(GLFW_KEY_ESCAPE)),
          "Escape should work again after focus has been reset");

    // If the actual splash child receives Escape, it dismisses itself and
    // does not forward that keypress into the owner's keyboard state.
    MappedKeyboard childFocusedOwner;
    mesher::EscapeExitGuard childFocusedExit;
    check(!childFocusedExit.exitRequested(
              childFocusedOwner.down(GLFW_KEY_ESCAPE)),
          "Splash-child Escape must not exit an owner that saw no key press");
    childFocusedOwner.onKey(GLFW_KEY_ESCAPE,GLFW_PRESS);
    check(childFocusedExit.exitRequested(
              childFocusedOwner.down(GLFW_KEY_ESCAPE)),
          "Fresh owner Escape after splash-child dismissal still exits");
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
    check(keys.active(GLFW_KEY_W),
          "A quick tap must also register in held-state merge input");
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
        escapeSplashDismissesBeforeEditorExit();
        invalidNamesDoNotBecomePhysicalShortcuts();
        std::cout<<"Layout-mapped key and modifier behavior: PASS\n";
    }catch(const std::exception& error) {
        std::cerr<<"Mapped keyboard regression: "<<error.what()<<'\n';
        return 1;
    }
}
