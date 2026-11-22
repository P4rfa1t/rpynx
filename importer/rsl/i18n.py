"""Language selection for the desktop importer and its webview."""

import json
import locale
import os
import sys
from functools import lru_cache

from .locale_en import MESSAGES as ENGLISH
from .locale_hant import MESSAGES as TRADITIONAL


def language_for(tag):
    parts = tag.replace("_", "-").split(".", 1)[0].lower().split("-")
    if parts[0] != "zh":
        return "en"
    if "hans" in parts:
        return "zh-Hans"
    if any(part in ("hant", "tw", "hk", "mo", "cht") for part in parts):
        return "zh-Hant"
    return "zh-Hans"


def _system_preference():
    if sys.platform == "win32":
        import ctypes

        kernel = ctypes.windll.kernel32
        size = ctypes.c_ulong(0)
        count = ctypes.c_ulong(0)
        kernel.GetUserPreferredUILanguages(8, ctypes.byref(count), None, ctypes.byref(size))
        if size.value:
            names = ctypes.create_unicode_buffer(size.value)
            if kernel.GetUserPreferredUILanguages(8, ctypes.byref(count), names, ctypes.byref(size)):
                return names.value
    elif sys.platform == "darwin":
        try:
            from Foundation import NSLocale
        except ImportError:
            pass
        else:
            languages = NSLocale.preferredLanguages()
            if languages:
                return str(languages[0])
    return (os.environ.get("LC_ALL") or os.environ.get("LC_MESSAGES") or os.environ.get("LANG")
            or locale.getlocale()[0] or "en")


@lru_cache(maxsize=1)
def _system_language():
    return language_for(_system_preference())


def language():
    override = os.environ.get("RSL_LANGUAGE")
    return language_for(override) if override else _system_language()


def messages():
    current = language()
    if current == "en":
        return ENGLISH
    if current == "zh-Hant":
        return TRADITIONAL
    return {}


def tr(text):
    return messages().get(text, text)


def web_page(source):
    catalog = json.dumps({"language": language(), "messages": messages()}, ensure_ascii=False)
    return source.replace("/* RSL_TRANSLATIONS */ null", catalog.replace("<", "\\u003c"), 1)
