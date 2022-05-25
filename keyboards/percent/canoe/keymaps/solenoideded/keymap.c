/*
Copyright 2017 Luiz Ribeiro <luizribeiro@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// Thanks to Walkerstop for providing his guide <3
// Author:  Adrian <@DetectiveDazgud#3670>
// Guide:   https://www.keebtalk.com/t/how-to-install-a-solenoid-in-kbdfans-8x/3849/4
// Notes:   Code in this file is modified from Walkerstop's KBD8X file, using different QMK functions to
//          enable and disable the pin outputs for controlling the solenoid.
//          This file also contains code that more accurately emulates the "key repeat" behaviour of holding
//          down keys in Windows, with more control over the hold delay and repeat rate!
//          Also, this keymap.c file was written for a Percent Canoe Gen1 PCB, so ymmv for your own board!

#include QMK_KEYBOARD_H
#include <timer.h>
#include "gpio.h"

#define _BL 0
#define _FL 1

//-- Custom Keycodes --//
enum sol_keycodes
{
    SOL_TOGGLE_MAIN = SAFE_RANGE,   // Enable/Disable solenoid entirely.
    SOL_TOGGLE_HOLD,                // Enable/Disable solenoid hold spam.
};

//-- Keymap --//
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] =
{
    [_BL] = LAYOUT_65_ansi_blocker(
        KC_GESC,     KC_1,     KC_2,     KC_3,     KC_4,     KC_5,     KC_6,     KC_7,     KC_8,     KC_9,     KC_0,     KC_MINS,  KC_EQL,  KC_BSPC,  KC_DEL,
        KC_TAB,      KC_Q,     KC_W,     KC_E,     KC_R,     KC_T,     KC_Y,     KC_U,     KC_I,     KC_O,     KC_P,     KC_LBRC,  KC_RBRC, KC_BSLS,  KC_F13,
        KC_CAPS,     KC_A,     KC_S,     KC_D,     KC_F,     KC_G,     KC_H,     KC_J,     KC_K,     KC_L,     KC_QUOT,  KC_SCLN,  KC_ENT,            KC_F14,
        KC_LSFT,               KC_Z,     KC_X,     KC_C,     KC_V,     KC_B,     KC_N,     KC_M,     KC_COMM,  KC_DOT,   KC_SLSH,  KC_RSFT, KC_UP,    MO(_FL),
        KC_LCTL,     KC_LGUI,  KC_LALT,                                KC_SPC,                       KC_RALT,  KC_RCTL,            KC_LEFT, KC_DOWN,  KC_RIGHT),

    [_FL] = LAYOUT_65_ansi_blocker(
        RESET,       KC_F1,    KC_F2,    KC_F3,    KC_F4,    KC_F5,    KC_F6,    KC_F7,    KC_F8,    KC_F9,    KC_F10,   KC_F11,   KC_F12,  KC_TRNS,  SOL_TOGGLE_MAIN,
        KC_TRNS,     KC_TRNS,  KC_TRNS,  KC_TRNS,  KC_TRNS,  KC_TRNS,  KC_TRNS,  KC_TRNS,  KC_TRNS,  KC_TRNS,  KC_TRNS,  KC_TRNS,  KC_TRNS, KC_TRNS,  SOL_TOGGLE_HOLD,
        KC_TRNS,     KC_TRNS,  KC_TRNS,  KC_TRNS,  KC_TRNS,  KC_TRNS,  KC_TRNS,  KC_TRNS,  KC_TRNS,  KC_TRNS,  KC_TRNS,  KC_TRNS,  KC_TRNS,           KC_TRNS,
        KC_TRNS,               KC_TRNS,  KC_TRNS,  KC_TRNS,  KC_TRNS,  KC_TRNS,  KC_TRNS,  KC_TRNS,  KC_TRNS,  KC_TRNS,  KC_TRNS,  KC_TRNS, KC_TRNS,  KC_TRNS,
        KC_TRNS,     KC_TRNS,  KC_TRNS,                                KC_TRNS,                      KC_TRNS,  KC_TRNS,            KC_TRNS, KC_TRNS,  KC_TRNS)

};

//-- Defines for Solenoid Controls --//
#define SOLENOID_FIRE_DWELL 20          // Default amount of time that the solenoid will stay extended for. Too HIGH leads to slower RPM, too LOW leads to solenoid not extending far enough in one cycle.
#define SOLENOID_HOLD       500         // Amount of time (in ms) that a single key must be held to trigger solenoid spam. (Search: Windows Repeat Delay)
#define SOLENOID_HOLD_DWELL 50          // Amount of time (in ms) between spam. (Search: Windows Repeat Rate)
#define SOLENOID_PIN        D6          // Data pin on the IC to actuate the MOSFET controlling the solenoid.

//-- Flags for Solenoid --//
bool sol_enabled            = true;     // True if you want the keyboard to start with the solenoid enabled.
bool sol_hold_enabled       = true;     // True if you want the solenoid to start spamming after holding down a single key long enough. (Search: "Windows Repeat Delay" and "Windows Repeat Rate")
bool sol_firing             = false;    // Flag to show that the solenoid is currently extended.
bool sol_spamming           = false;    // Flag to show that the solenoid is currently spamming.

//-- Single Keystroke --//
uint32_t sol_fire_time      = 0;        // Timestamp of when the solenoid is powered.

//-- Holding Keys --//
uint32_t sol_hold_start     = 0;        // Timestamp of when a key is held.
uint16_t sol_hold_keycode   = KC_TRNS;  // Keycode of the currently held key.


//-- Solenoid Helper Functions --//
/// <summary>
/// Turns off the solenoid and set all fire flags to false.
/// </summary>
void sol_hardstop(void)
{
    sol_firing = false;
    sol_spamming = false;
    writePinLow(SOLENOID_PIN);
}

/// <summary>
/// Fires the solenoid by turning the SOLENOID_PIN to output HIGH.
/// </summary>
void sol_fire(void)
{
    if (!sol_enabled || sol_firing)
        return;

    sol_fire_time = timer_read32();
    sol_firing = true;
    writePinHigh(SOLENOID_PIN);
}

/// <summary>
/// Stops the solenoid by turning the SOLENOID_PIN to output LOW.
/// </summary>
void sol_stop(void)
{
    sol_firing = false;
    writePinLow(SOLENOID_PIN);
}

/// <summary>
/// Start spamming the solenoid to follow character spam when holding down a key.
/// </summary>
void sol_fire_spam(void)
{
    if (!sol_hold_enabled)
        return;

    uint16_t elapsed = timer_elapsed32(sol_fire_time);
    if (elapsed > SOLENOID_HOLD_DWELL)
    {
        sol_spamming = true;
        sol_fire();
    }
}

/// <summary>
/// Stops the solenoid spamming.
/// </summary>
void sol_stop_spam(void)
{
    sol_spamming = false;
    sol_stop();
}

/// <summary>
/// Updates the state/color of the LEDs based on the state of the keyboard.
/// </summary>
void sol_rgb_indicator_update(void)
{
    // Toggles the side lighting to show if solenoid is enabled or not.
    if (sol_enabled)
        rgblight_enable();
    else
        rgblight_disable();

    // Toggles color of side lighting to show if solenoid hold spam is enabled or not.
    if (sol_hold_enabled)
        rgblight_setrgb(RGB_PINK);
    else
        rgblight_setrgb(RGB_CYAN);
}

/// <summary>
/// Toggles the state of the solenoid.
/// </summary>
void sol_enable_main_toggle(void)
{
    sol_enabled = !sol_enabled;
    sol_rgb_indicator_update();

    // Reset solenoid just in case.
    sol_hardstop();
}

/// <summary>
/// Toggles if hold spam is enabled or not.
/// </summary>
void sol_enable_hold_toggle(void)
{
    sol_hold_enabled = !sol_hold_enabled;
    sol_rgb_indicator_update();

    // Reset solenoid just in case.
    sol_hardstop();
}


//-- Solenoid Main Functions --//
/// <summary>
/// Updates the state of the solenoid.
/// </summary>
void sol_update(void)
{
    if (!sol_enabled)
        return;

    uint16_t elapsed = 0;

    // Is user holding down a valid key?
    if (sol_hold_keycode != KC_TRNS)
    {
        // Has key been held down for long enough?
        elapsed = timer_elapsed32(sol_hold_start);
        if (elapsed > SOLENOID_HOLD)
            sol_fire_spam();
    }
    else if (sol_spamming)
    {
        sol_stop_spam();
    }

    // Has solenoid extended for long enough?
    if (sol_firing)
    {
        elapsed = timer_elapsed32(sol_fire_time);
        if (elapsed > SOLENOID_FIRE_DWELL)
            sol_stop();
    }
}

/// <summary>
/// Sets up the solenoid pins and initializes the IC's timer code.
/// </summary>
void sol_setup(void)
{
    setPinOutput(SOLENOID_PIN);
    timer_init();
}


//-- Matrix Main Functions --//
/// <summary>
/// Runs right after hardware initialization, alot of software features might not be initialized!
/// </summary>
void matrix_init_user(void)
{
    sol_setup();
}

/// <summary>
/// Runs every tick of the IC.
/// </summary>
void matrix_scan_user(void)
{
    sol_update();
}

/// <summary>
/// Runs after the keyboard finishes initialization. Software features should mostly have been initialized by now.
/// </summary>
void keyboard_post_init_user(void)
{
    // Set keyboard's top right LED to display solenoid enabled status.
    rgblight_mode(1);
    sol_rgb_indicator_update();

    // Reset solenoid just in case.
    sol_hardstop();
}

/// <summary>
/// Processes key inputs.
/// </summary>
/// <param name="keyCode">Keycode that's been pressed.</param>
/// <param name="record">Event record.</param>
/// <returns>True if you want QMK to continue processing the input (so to add on behaviours ontop of current ones, like pressing 'E' will make a sound and also output the letter 'E'.
/// False if you want QMK to stop processing the input after this.</returns>
bool process_record_user(uint16_t keyCode, keyrecord_t* record)
{
    if (record->event.pressed)
    {
        sol_fire();

        // Solenoid Control keys.
        switch (keyCode)
        {
        case SOL_TOGGLE_MAIN:
            sol_enable_main_toggle();
            break;

        case SOL_TOGGLE_HOLD:
            sol_enable_hold_toggle();
            break;
        }

        // Holding keys.
        if (sol_hold_enabled)
        {
            if ((keyCode >= KC_A && keyCode <= KC_ENT) ||        // Alphanumerics and Enter
                (keyCode >= KC_BSPC && keyCode <= KC_SLSH) ||    // Backspace/Tab/Space and Punctuation
                (keyCode >= KC_RGHT && keyCode <= KC_UP) ||      // Arrows
                (keyCode >= KC_PSLS && keyCode <= KC_PDOT))      // Numpad
            {
                if (sol_hold_keycode != keyCode)
                {
                    sol_hold_keycode = keyCode;
                    sol_hold_start = timer_read32();
                }
            }
            else
            {
                sol_hold_keycode = KC_TRNS;
            }
        }
        else
            sol_hold_keycode = KC_TRNS;
    }
    else
    {
        sol_hold_keycode = KC_TRNS;
    }

    // QMK to continue processing the input.
    return true;
}
