#ifndef RSL_UI_LOCALE_H
#define RSL_UI_LOCALE_H

void rsl_ui_language(const char *language);
const char *rsl_ui_tr(const char *text) __attribute__((format_arg(1)));

#endif
