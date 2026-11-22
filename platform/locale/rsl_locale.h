#ifndef RSL_LOCALE_H
#define RSL_LOCALE_H

#ifdef __SWITCH__
#include <switch.h>
#endif

static inline const char *rsl_system_language(void)
{
#ifdef __SWITCH__
    u64 code;
    SetLanguage language;
    if (R_SUCCEEDED(setInitialize())) {
        Result rc = setGetSystemLanguage(&code);
        if (R_SUCCEEDED(rc))
            rc = setMakeLanguage(code, &language);
        setExit();
        if (R_SUCCEEDED(rc)) {
            if (language == SetLanguage_ZHCN || language == SetLanguage_ZHHANS)
                return "zh-Hans";
            if (language == SetLanguage_ZHTW || language == SetLanguage_ZHHANT)
                return "zh-Hant";
        }
    }
#endif
    return "en";
}

#endif
