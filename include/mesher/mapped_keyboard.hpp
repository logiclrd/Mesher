#pragma once

// GLFW key tokens describe physical US-layout key positions, even when the
// system layout assigns a different printable character to that position.
// Capture the glfwGetKeyName translation on key DOWN, and remember it until
// key UP so layout changes while a key is held cannot leave a stuck shortcut.
// Scancodes are not used to choose an action or to track a held key.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <array>
#include <string_view>

namespace mesher {

class MappedKeyboard {
public:
    // A single ASCII printable key name, as returned by glfwGetKeyName,
    // defines the user-visible binding. Non-ASCII characters cannot be
    // mistaken for the underlying US-layout physical-key token.
    static int mappedKey(int physical, std::string_view name) noexcept {
        if(physical==GLFW_KEY_UNKNOWN || physical<0 ||
           physical>GLFW_KEY_LAST)return GLFW_KEY_UNKNOWN;
        if(physical==GLFW_KEY_SPACE)return GLFW_KEY_SPACE;
        if(physical>=GLFW_KEY_APOSTROPHE &&
           physical<=GLFW_KEY_WORLD_2) {
            if(name.size()!=1)return GLFW_KEY_UNKNOWN;
            unsigned char c=static_cast<unsigned char>(name.front());
            if(c>='a'&&c<='z')c=static_cast<unsigned char>(c-'a'+'A');
            // GLFW's printable key tokens are ASCII for alphanumeric
            // and these named punctuation keys.
            if((c>='A'&&c<='Z')||(c>='0'&&c<='9'))
                return static_cast<int>(c);
            switch(c) {
            case '\'': case ',': case '-': case '.': case '/':
            case ';': case '=': case '[': case '\\': case ']': case '`':
                return static_cast<int>(c);
            default:return GLFW_KEY_UNKNOWN;
            }
        }
        // Named non-printable keys (modifiers, F8, arrows, Escape...) retain
        // GLFW's stable semantic token; they are not text entry.
        return physical;
    }

    void onKey(int physical,int action,std::string_view name={}) noexcept {
        if(physical<0 || physical>GLFW_KEY_LAST)return;
        if(action==GLFW_RELEASE) {
            const auto key=assigned_[physical];
            if(key!=GLFW_KEY_UNKNOWN) {
                if(held_[key]>0)--held_[key];
                assigned_[physical]=GLFW_KEY_UNKNOWN;
            }
            return;
        }
        if(action!=GLFW_PRESS || assigned_[physical]!=GLFW_KEY_UNKNOWN)
            return; // Auto-repeats must not generate a second press.
        const auto key=mappedKey(physical,name);
        if(key==GLFW_KEY_UNKNOWN)return;
        assigned_[physical]=key;
        if(held_[key]++==0)pressed_[key]=true;
    }

    bool down(int key) const noexcept {
        return key>=0 && key<=GLFW_KEY_LAST && held_[key]!=0;
    }
    bool takePress(int key) noexcept {
        if(key<0||key>GLFW_KEY_LAST)return false;
        const bool result=pressed_[key];
        pressed_[key]=false;
        return result;
    }
    void clear() noexcept {
        assigned_.fill(GLFW_KEY_UNKNOWN);
        held_.fill(0);
        pressed_.fill(false);
    }

    MappedKeyboard() noexcept {clear();}
private:
    std::array<int,GLFW_KEY_LAST+1> assigned_{};
    std::array<unsigned int,GLFW_KEY_LAST+1> held_{};
    std::array<bool,GLFW_KEY_LAST+1> pressed_{};
};

} // namespace mesher
