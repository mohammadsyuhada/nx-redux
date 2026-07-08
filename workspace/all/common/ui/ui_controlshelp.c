#include "ui_controlshelp.h"
#include "api.h"
#include "defines.h"

void UI_renderControlsHelp(SDL_Surface* screen, const char* title,
						   const ControlHelp* controls) {
	int hw = screen->w;
	int hh = screen->h;

	// Count controls
	int control_count = 0;
	while (controls[control_count].button != NULL)
		control_count++;

	// Dialog box dimensions
	int line_height = SCALE1(18);
	int hint_gap = SCALE1(15);
	int box_w = SCALE1(240);
	int box_h = SCALE1(60) + (control_count * line_height) + hint_gap;

	// Clear scroll text layer
	GFX_clearLayers(LAYER_SCROLLTEXT);

	// Center the box
	int box_x = (hw - box_w) / 2;
	int box_y = (hh - box_h) / 2;
	int content_x = box_x + SCALE1(15);

	// Dark background around dialog
	SDL_FillRect(screen, &(SDL_Rect){0, 0, hw, box_y}, RGB_BLACK);
	SDL_FillRect(screen, &(SDL_Rect){0, box_y + box_h, hw, hh - box_y - box_h}, RGB_BLACK);
	SDL_FillRect(screen, &(SDL_Rect){0, box_y, box_x, box_h}, RGB_BLACK);
	SDL_FillRect(screen, &(SDL_Rect){box_x + box_w, box_y, hw - box_x - box_w, box_h}, RGB_BLACK);

	// Box background + border
	SDL_FillRect(screen, &(SDL_Rect){box_x, box_y, box_w, box_h}, RGB_BLACK);
	SDL_FillRect(screen, &(SDL_Rect){box_x, box_y, box_w, SCALE1(2)}, RGB_WHITE);
	SDL_FillRect(screen, &(SDL_Rect){box_x, box_y + box_h - SCALE1(2), box_w, SCALE1(2)}, RGB_WHITE);
	SDL_FillRect(screen, &(SDL_Rect){box_x, box_y, SCALE1(2), box_h}, RGB_WHITE);
	SDL_FillRect(screen, &(SDL_Rect){box_x + box_w - SCALE1(2), box_y, SCALE1(2), box_h}, RGB_WHITE);

	// Title
	SDL_Surface* title_surf = TTF_RenderUTF8_Blended(font.medium, title, COLOR_WHITE);
	if (title_surf) {
		SDL_BlitSurface(title_surf, NULL, screen, &(SDL_Rect){content_x, box_y + SCALE1(10)});
		SDL_FreeSurface(title_surf);
	}

	// Control rows
	int y_offset = box_y + SCALE1(35);
	int right_col = box_x + SCALE1(90);
	for (int i = 0; i < control_count; i++) {
		SDL_Surface* btn_surf = TTF_RenderUTF8_Blended(font.small, controls[i].button, COLOR_GRAY);
		if (btn_surf) {
			SDL_BlitSurface(btn_surf, NULL, screen, &(SDL_Rect){content_x, y_offset});
			SDL_FreeSurface(btn_surf);
		}
		SDL_Surface* action_surf = TTF_RenderUTF8_Blended(font.small, controls[i].action, COLOR_WHITE);
		if (action_surf) {
			SDL_BlitSurface(action_surf, NULL, screen, &(SDL_Rect){right_col, y_offset});
			SDL_FreeSurface(action_surf);
		}
		y_offset += line_height;
	}

	// Hint at bottom
	const char* hint = "Press any button to close";
	SDL_Surface* hint_surf = TTF_RenderUTF8_Blended(font.small, hint, COLOR_GRAY);
	if (hint_surf) {
		int hint_y = box_y + box_h - SCALE1(10) - hint_surf->h;
		SDL_BlitSurface(hint_surf, NULL, screen, &(SDL_Rect){content_x, hint_y});
		SDL_FreeSurface(hint_surf);
	}
}
