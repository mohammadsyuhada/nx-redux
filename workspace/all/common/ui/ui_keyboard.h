#ifndef UI_KEYBOARD_H
#define UI_KEYBOARD_H

// Kept for backward compatibility (scraper/musicplayer call it); no-op.
void UIKeyboard_init(void);

// Show the modal in-process on-screen keyboard, blocking until the user
// confirms or cancels. Returns a malloc'd string with the input, or NULL
// if cancelled or empty. Caller must free() the result.
// `prompt` is rendered as the title above the input line.
char* UIKeyboard_open(const char* prompt);

// UIKeyboard_openEx flags.
// KB_START_CANCELS: a START tap cancels like B. Opt-in for prompts that
// START itself opened (launcher Search, #112) so the same button toggles
// the screen closed again; off by default because the keyboard is shared
// by password / rename / playlist prompts where START has no meaning.
#define KB_START_CANCELS (1 << 0)
char* UIKeyboard_openEx(const char* prompt, int flags);

#endif // UI_KEYBOARD_H
