#ifndef CHARACTER_UI_H
#define CHARACTER_UI_H

void character_ui_init(void);
void character_ui_cleanup(void);
void character_ui_reset(void);

void character_ui_set_intro(float progress);

void character_ui_draw_health_bar(const char *name, float ratio, float flash);
void character_ui_draw_stamina_bar(float ratio);
void character_ui_draw_c_buttons(void);

// Draws all character-owned HUD elements.
// Scene should call this after the 3D/world pass.
void character_ui_draw(void);

#endif // CHARACTER_UI_H