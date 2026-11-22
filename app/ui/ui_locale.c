#include <stddef.h>
#include <string.h>

#include "ui_locale.h"

struct translation { const char *source, *text; };

#define T(source, text) { source, text },
static const struct translation english[] = {
#include "locale_en.inc"
};
static const struct translation traditional[] = {
#include "locale_hant.inc"
};
#undef T

static const struct translation *catalog;
static size_t entries;

void rsl_ui_language(const char *language)
{
    if (strcmp(language, "zh-Hans") == 0) {
        catalog = NULL;
        entries = 0;
    } else if (strcmp(language, "zh-Hant") == 0) {
        catalog = traditional;
        entries = sizeof traditional / sizeof traditional[0];
    } else {
        catalog = english;
        entries = sizeof english / sizeof english[0];
    }
}

const char *rsl_ui_tr(const char *text)
{
    if (!text) return NULL;
    for (size_t i = 0; i < entries; ++i)
        if (strcmp(text, catalog[i].source) == 0)
            return catalog[i].text;
    return text;
}
