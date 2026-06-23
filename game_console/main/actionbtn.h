#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ACTION BUTTON — dedicated pushbutton on GPIO27.
 *
 * This is the in-game "jump" / "flap" / "shoot" / "fire" button — the
 * single action control used DURING gameplay, replacing the joystick
 * click for that purpose entirely. The joystick click is now reserved
 * exclusively for menu navigation (select/confirm in the game selector,
 * pause menu, help screen, game-over screen), and GPIO26 remains the
 * dedicated pause-only button. Three physically distinct buttons, three
 * distinct jobs — no overloaded button serving double duty anywhere.
 *
 * Wired identically to the pause button: internal pull-up enabled, reads
 * LOW (0) when pressed, HIGH (1) at rest.
 */

void actionbtn_init(void);

/*
 * Returns the CURRENT debounced level of the button as a simple boolean —
 * true while held down, false while released. Unlike pausebtn_pressed()
 * (which fires once per press), games need to know the held state for
 * things like "hold to charge a jump" or simply reading it every poll
 * tick the same way the joystick axes are read. Edge detection (if a
 * specific game wants "fire on press, not hold") is handled inside that
 * game's own input function, exactly the same pattern already used for
 * joystick button edge detection throughout this project.
 */
bool actionbtn_is_pressed(void);

#ifdef __cplusplus
}
#endif