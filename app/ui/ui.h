/* Loader screens drawn with LVGL from the state in loader.h (docs/LOADER_UI.md). */
#ifndef RSL_UI_H
#define RSL_UI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Call after lv_init() and creating the display. ttf is the text font and
 * icons the console's button icon font (TrueType data, kept alive by the
 * caller); NULL falls back to Montserrat 14 and to drawn chips. cjk is the
 * console's Chinese font (NULL: none), chained as the fallback for missing
 * glyphs, so Chinese titles and this loader's Chinese text render. */
void ui_init(const void *ttf, size_t ttf_len, const void *cjk, size_t cjk_len,
             const void *icons, size_t icons_len);

/* Bring the screen in line with g. Call every frame: it only touches what
 * changed, and decodes at most one cover per call. */
void ui_update(void);

/* Nothing pending: no animation, no cover waiting to be decoded. */
bool ui_idle(void);
void ui_cover_counts(int *decoded, int *failed);

/* Up (-1) or down (1) on the detail page: scroll its facts. */
void ui_scroll(int dir);

/* Picture page: pointer drag delta since the last call. */
bool ui_picture_drag(int *dx, int *dy);

/* Game i's cover file changed, or the library was read again: decode it again. */
void ui_art_changed(int i);

/* The last tap on the touchscreen, once: the action it asks for (-1 if
 * none) and in *item the item tapped (-1 if none): an index of g.sel, of
 * g.menu_sel while the menu is open, or the notice's OK. */
int ui_tap(int *item);

/* Accent colours: preset i (0 .. ACCENT_PRESETS-1), and the custom row's
 * colour for a hue. */
void ui_accent_preset(int i, uint32_t *rgb, const char **name);
uint32_t ui_hue_color(int hue);

/* Names for status messages, as the screens show them. */
const char *ui_action_name(int a);
const char *ui_button_name(int b);
const char *ui_preset_name(int p);

#endif
