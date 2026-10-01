#include "menu_transition.h"
#include "ui_ease.h"

bool MenuTransition_exitBegin(MenuTransitionExit* x, uint32_t now, bool picture, bool animations) {
	x->active = picture && animations;
	x->start = now;
	return x->active;
}

float MenuTransition_exitDarkness(const MenuTransitionExit* x, uint32_t now) {
	if (!x->active)
		return 0.0f;
	uint32_t elapsed = now - x->start; // unsigned: a tick wrap still counts forward
	if (elapsed >= MENU_TRANSITION_FADE_MS)
		return 1.0f;
	return UI_easeStandard((float)elapsed / (float)MENU_TRANSITION_FADE_MS);
}

bool MenuTransition_exitStep(MenuTransitionExit* x, uint32_t now, bool key_pressed) {
	if (!x->active)
		return false;
	if (!key_pressed && now - x->start < MENU_TRANSITION_FADE_MS)
		return false;
	x->active = false;
	return true;
}

void MenuTransition_exitCancel(MenuTransitionExit* x) {
	x->active = false;
}

uint32_t MenuTransition_rebaseStart(uint32_t start, uint32_t now, uint32_t cap) {
	return now - start > cap ? now - cap : start;
}
