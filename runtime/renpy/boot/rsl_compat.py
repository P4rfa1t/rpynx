"""Engine fixes some games need on the Switch, for rsl_bridge: Ren'Py 7.x games'
gui values across a language change, links and folder buttons, and audio streams
left running at exit."""
import os

import renpy


def _old_language_change():
    """True for games made with Ren'Py 7.5.3/8.0.3 or older (script_version.txt)."""
    v = renpy.config.script_version
    return bool(v) and (v <= (7, 5, 3) or (v[0] == 8 and v <= (8, 0, 3)))


def install_language():
    """Ren'Py 8.5.3 re-runs every `define gui.*` when the language changes;
    7.5.3/8.0.3 did that only in gui.rebuild(). A 7.x game that sets gui values
    in `init python` lost them (Forbidden Fantasy: button text size back to 0,
    every button label invisible, hardware run 13). For such games, skip that
    redefine unless gui.rebuild() asks for it (rebuild=True)."""
    import renpy.ast
    import renpy.exports
    import renpy.translation

    redefine, change_language = renpy.ast.redefine, renpy.translation.change_language
    skip = []

    def compat_redefine(stores):
        if not (skip and skip[-1] and stores == ["store.gui"]):
            redefine(stores)

    def compat_change_language(language, force=False, rebuild=False):
        skip.append(not rebuild and _old_language_change())
        try:
            return change_language(language, force=force, rebuild=rebuild)
        finally:
            skip.pop()

    renpy.ast.redefine = compat_redefine
    renpy.translation.change_language = compat_change_language
    renpy.exports.change_language = compat_change_language


# Opening a website or a folder. Ren'Py's open_url() tries webbrowser and stays silent
# when there is none, so a link did nothing; Forbidden Fantasy's "deluxe" button ran
# subprocess.Popen(["xdg-open", folder]), and the Switch cannot start processes, so the
# game stopped with an error (run 24). Both show this notice instead.
_OPENERS = {"xdg-open", "open", "explorer", "explorer.exe", "start", "cmd", "cmd.exe"}


def _cannot_open_screen(target="", what="", **kwargs):
    ui, store = renpy.ui, renpy.store
    hide = store.Hide("_rsl_cannot_open")
    ui.add(store.Solid("#000000b0"))
    ui.frame(xalign=0.5, yalign=0.5, xmaximum=1000, padding=(40, 30), background=store.Solid("#1d1e22"))
    ui.vbox(spacing=24)
    if what == "website":
        ui.text("This link opens a website, which the Switch cannot show here. "
                "To visit it, open this address on a phone or computer:",
                substitute=False, color="#f0f1f4", size=28)
    else:
        ui.text("This opens a folder or file on a computer, which the Switch cannot do:",
                substitute=False, color="#f0f1f4", size=28)
    ui.text(str(target).replace("{", "{{"), substitute=False, color="#25d0f2", size=26)
    ui.textbutton("OK", action=hide, xalign=0.5)
    ui.close()
    ui.key("game_menu", action=hide)
    ui.key("rollback", action=hide)


def _cannot_open(target):
    target = str(target)
    print("rsl: cannot open on the Switch: %s" % target, flush=True)
    what = "website" if target.lower().startswith(("http:", "https:", "www.")) else "file"
    try:
        renpy.exports.show_screen("_rsl_cannot_open", target=target, what=what)
        renpy.exports.restart_interaction()
    except Exception as e:   # a notice must never stop the game
        print("rsl: could not show the notice: %r" % e, flush=True)


def _open_url(url, *args, **kwargs):
    _cannot_open(url)


def _install_open_notice():
    if renpy.display.screen.get_screen_variant("_rsl_cannot_open") is None:   # gone after a reload
        renpy.exports.define_screen("_rsl_cannot_open", _cannot_open_screen, modal="True", zorder="1000")
    renpy.exports.open_url = _open_url
    try:   # never let these stop the bridge from loading
        import subprocess
        import webbrowser
    except Exception as e:
        print("rsl: no subprocess/webbrowser hooks: %r" % e, flush=True)
        return
    webbrowser.open = webbrowser.open_new = webbrowser.open_new_tab = lambda url, *a, **k: _open_url(url) or True
    if getattr(subprocess.Popen, "_rsl", False):
        return

    class Popen(subprocess.Popen):
        """Turns "open this folder/link" commands into the notice and behaves as a process
        that already finished; anything else runs (and fails) as before."""
        _rsl = True

        def __init__(self, args, *a, **kw):
            argv = args.split() if isinstance(args, str) else list(args)
            if argv and os.path.basename(str(argv[0])).lower() in _OPENERS:
                _cannot_open(argv[-1] if len(argv) > 1 else argv[0])
                self.args, self.returncode, self.pid = args, 0, None
                self.stdin = self.stdout = self.stderr = None
                self._child_created = self._communication_started = False
                return
            super().__init__(args, *a, **kw)

    subprocess.Popen = Popen


def _close_dying_streams():
    """Ren'Py's audio.quit() stops the thread that calls renpysound.periodic() before it
    fades the channels out, so a stream that ends then stays on the C side's "dying" list
    and its decode thread waits forever (ffmedia decode_thread in SDL_CondWait). On a PC
    the process ends anyway; here the next game runs in the same process, and that
    thread's stack crashed it (hardware runs 22-23). Runs after audio.quit()."""
    try:
        renpy.audio.renpysound.periodic()
        print("rsl: finished audio streams closed at exit", flush=True)
    except Exception as e:
        print("rsl: closing finished audio streams failed: %r" % e, flush=True)


def install_analysis_cache_check():
    """cache/py3analysis.rpyb ships inside some games and holds cached expression ASTs.
    A cache built by a different Ren'Py/Python than ours can contain ASTs whose end_lineno
    is smaller than their lineno (Z.A.T.O.: 7 entries like `If(flag,Rollback(), None)` with
    lineno 959, end_lineno 1), which CPython 3.12's compile() rejects with
    "ValueError: AST node line range (959, 1) is not valid" the moment a screen is
    prepared - the game cannot start. pyanalysis.load_cache() has no digest check, unlike
    the screens cache. Wrap it: drop every poisoned entry and mark the cache updated, so
    a clean one is saved at exit and the game's own expressions are recompiled."""
    import ast

    import renpy.pyanalysis

    load_cache, ccache = renpy.pyanalysis.load_cache, renpy.pyanalysis.ccache

    def check():
        for cache in (ccache.ast_eval_cache, ccache.ast_exec_cache):
            for key, val in list(cache.items()):
                node = val[0] if isinstance(val, tuple) else val
                if not isinstance(node, ast.AST):
                    continue
                bad = False
                for sub in ast.walk(node):
                    ln, eln = getattr(sub, "lineno", None), getattr(sub, "end_lineno", None)
                    if ln is not None and eln is not None and eln < ln:
                        bad = True
                        break
                if bad:
                    del cache[key]
                    ccache.updated = True

    def compat_load_cache():
        load_cache()
        check()

    renpy.pyanalysis.load_cache = compat_load_cache


def install_archive_listdir():
    """os.listdir() sees only loose files, but the export packs every non-ASCII-named file
    into rsl-nonascii.rpa, because the Switch SD cannot list or create such names
    (hardware run 20). A game that lists such a folder then sees it as empty: Z.A.T.O.'s
    load_fonts() lists tl/chinese/fonts/, finds no fonts, and crashes on the empty map
    (StopIteration) the moment the language changes to Chinese. Union the real listing
    with the members of the loaded archives that live under that folder; names are added,
    never removed, and os.walk (which uses scandir) is untouched."""
    import renpy.loader

    real_listdir = os.listdir

    def listdir(path):
        entries = real_listdir(path)
        gamedir = getattr(renpy.config, "gamedir", None)
        if not gamedir:
            return entries
        try:
            rel = os.path.relpath(path, gamedir).replace(os.sep, "/")
        except ValueError:
            return entries
        if rel in (".", "..") or rel.startswith(".."):
            return entries
        prefix = rel + "/"
        names = set(entries)
        for _fn, index in renpy.loader.archives:
            for member in index:
                if member.startswith(prefix):
                    rest = member[len(prefix):]
                    if not rest:
                        continue
                    names.add(rest.split("/", 1)[0])
        if len(names) != len(entries):
            entries = sorted(names)
        return entries

    os.listdir = listdir


def install_display():
    """Runs from rsl_bridge's display setup, once the game's config exists."""
    _install_open_notice()
    install_archive_listdir()
    if _close_dying_streams not in renpy.config.python_exit_callbacks:
        renpy.config.python_exit_callbacks.append(_close_dying_streams)
