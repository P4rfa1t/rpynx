/* Running CPython: on its own thread with a big stack, stdout/stderr reattached,
 * the game's threads given time to finish, then SDL shut down on that thread. */
#include "runtime.h"

#include <stdio.h>

#include <SDL2/SDL.h>

#include "log.h"

#if PY_MAJOR_VERSION >= 3
#define STDLIB_ZIP  L"romfs:/lib/python312.zip"
#else
#define STDLIB_ZIP  "romfs:/lib/python27.zip"
#endif
/* Ren'Py recurses deeply (displayables, pickling); the loader-provided main
 * thread stack is small, so Python runs on its own thread. */
#define PY_STACK    (8 * 1024 * 1024)
#define THREAD_WAIT_MS 1000   /* for the game's threads to finish before Python shuts down */

PyInterpreterState *volatile g_interp;

static void log_py_error(const char *where)
{
    if (!PyErr_Occurred())
        return;
    PyObject *type = NULL, *val = NULL, *tb = NULL;
    PyErr_Fetch(&type, &val, &tb);
    PyErr_NormalizeException(&type, &val, &tb);
    PyObject *ts = type ? PyObject_Str(type) : NULL;
    PyObject *vs = val ? PyObject_Str(val) : NULL;
#if PY_MAJOR_VERSION >= 3
    const char *tstr = ts ? PyUnicode_AsUTF8(ts) : "?";
    const char *vstr = vs ? PyUnicode_AsUTF8(vs) : "?";
#else
    const char *tstr = ts ? PyString_AsString(ts) : "?";
    const char *vstr = vs ? PyString_AsString(vs) : "?";
#endif
    rsl_log("%s: %s: %s", where, tstr ? tstr : "?", vstr ? vstr : "?");
    Py_XDECREF(ts);
    Py_XDECREF(vs);
    PyErr_Restore(type, val, tb);
}

static int exit_code_of(PyObject *exc)
{
    PyObject *code = PyObject_GetAttrString(exc, "code");
    int rc = 1;
    if (code == Py_None)
        rc = 0;
#if PY_MAJOR_VERSION >= 3
    else if (code && PyLong_Check(code))
        rc = (int)PyLong_AsLong(code);
#else
    else if (code && PyInt_Check(code))
        rc = (int)PyInt_AsLong(code);
    else if (code && PyLong_Check(code))
        rc = (int)PyLong_AsLong(code);
#endif
    Py_XDECREF(code);
    PyErr_Clear();
    return rc;
}

/* CPython checks stdout/stderr with fstat(), which libnx's console device does
 * not implement, so it leaves sys.stdout/sys.stderr as None and every print()
 * and traceback is silently dropped. Reattach them: to RSL_PYLOG (a file) if
 * set, otherwise to fds 1/2 (the console; FileIO tolerates the fstat error). */
#if PY_MAJOR_VERSION >= 3
static const char STDIO_SETUP[] =
    "import io, os, sys\n"
    "def _rsl_fd(fd):\n"
    "    return io.TextIOWrapper(io.FileIO(fd, 'w', closefd=False), encoding='utf-8',\n"
    "                            errors='backslashreplace', line_buffering=True)\n"
    "_rsl_log = os.environ.get('RSL_PYLOG')\n"
    "if _rsl_log:\n"
    "    sys.stdout = sys.stderr = open(_rsl_log, 'a', encoding='utf-8', errors='backslashreplace', buffering=1)\n"
    "else:\n"
    "    if sys.stdout is None: sys.stdout = _rsl_fd(1)\n"
    "    if sys.stderr is None: sys.stderr = _rsl_fd(2)\n"
    "del _rsl_fd, _rsl_log\n";
#else
static const char STDIO_SETUP[] =
    "import imp, io, os, sys\n"
    "class _RslBuiltinLoader(object):\n"
    "    def find_module(self, fullname, path=None):\n"
    "        if '.' in fullname and imp.is_builtin(fullname):\n"
    "            return self\n"
    "        return None\n"
    "    def load_module(self, fullname):\n"
    "        mod = sys.modules.get(fullname)\n"
    "        if mod is not None:\n"
    "            return mod\n"
    "        return imp.init_builtin(fullname)\n"
    "sys.meta_path.insert(0, _RslBuiltinLoader())\n"
    "_rsl_log = os.environ.get('RSL_PYLOG')\n"
    "if _rsl_log:\n"
    "    sys.stdout = sys.stderr = open(_rsl_log, 'a', 1)\n"
    "else:\n"
    "    if sys.stdout is None:\n"
    "        sys.stdout = io.TextIOWrapper(io.FileIO(1, 'w', closefd=False), encoding='utf-8', errors='backslashreplace', line_buffering=True)\n"
    "    if sys.stderr is None:\n"
    "        sys.stderr = io.TextIOWrapper(io.FileIO(2, 'w', closefd=False), encoding='utf-8', errors='backslashreplace', line_buffering=True)\n"
    "del _RslBuiltinLoader, _rsl_log\n";
#endif

/* Returns the script's exit status, or -1 if Python itself failed. */
static int run_python(struct job *j)
{
#if PY_MAJOR_VERSION >= 3
    PyPreConfig pre;
    PyPreConfig_InitIsolatedConfig(&pre);
    pre.utf8_mode = 1;             /* as Ren'Py's own launchers do */
    PyStatus st = Py_PreInitialize(&pre);

    PyConfig c;
    PyConfig_InitIsolatedConfig(&c);
    c.site_import = 0;
    c.write_bytecode = 0;
    c.buffered_stdio = 0;
    c.install_signal_handlers = 0;
    c.parse_argv = 0;
    c.module_search_paths_set = 1;
    if (!PyStatus_Exception(st) && j->renpy)
        st = PyWideStringList_Append(&c.module_search_paths, L"" RENPY_BASE);
    if (!PyStatus_Exception(st))
        st = PyWideStringList_Append(&c.module_search_paths, STDLIB_ZIP);
    if (!PyStatus_Exception(st))
        st = PyConfig_SetString(&c, &c.home, L"romfs:/lib");
    if (!PyStatus_Exception(st))
        st = PyConfig_SetBytesArgv(&c, j->argc, j->argv);
    if (!PyStatus_Exception(st))
        st = Py_InitializeFromConfig(&c);
    PyConfig_Clear(&c);
    if (PyStatus_Exception(st)) {
        rsl_log("Python init failed: %s: %s", st.func ? st.func : "?", st.err_msg ? st.err_msg : "?");
        printf("Python init failed: %s: %s\n", st.func ? st.func : "?", st.err_msg ? st.err_msg : "?");
        return -1;
    }
    rsl_log("Python %s initialised", Py_GetVersion());
    g_interp = PyInterpreterState_Get();
#else
    Py_NoSiteFlag = 1;
    Py_DontWriteBytecodeFlag = 1;
    Py_IgnoreEnvironmentFlag = 1;
    Py_FrozenFlag = 1;
    Py_SetPythonHome("romfs:");
    Py_InitializeEx(0);
    PySys_SetArgvEx(j->argc, j->argv, 0);
    {
        PyObject *path = PySys_GetObject("path");
        if (path && PyList_Check(path)) {
            PyObject *zip = PyString_FromString(STDLIB_ZIP);
            PyList_SetSlice(path, 0, PyList_GET_SIZE(path), NULL);
            if (j->renpy) {
                PyObject *base = PyString_FromString(RENPY_BASE);
                PyList_Append(path, base);
                Py_XDECREF(base);
            }
            if (zip) {
                PyList_Append(path, zip);
                Py_DECREF(zip);
            }
        }
        PyObject *cache = PySys_GetObject("path_importer_cache");
        if (cache && PyDict_Check(cache))
            PyDict_Clear(cache);
    }
    rsl_log("Python %s initialised", Py_GetVersion());
    g_interp = PyThreadState_Get()->interp;
#endif
    {
        PyObject *m_dict = PyModule_GetDict(PyImport_AddModule("__main__"));
        PyObject *res = PyRun_StringFlags(STDIO_SETUP, Py_file_input, m_dict, m_dict, NULL);
        if (!res) {
            log_py_error("STDIO_SETUP");
            PyErr_Print();
            rsl_log("could not attach Python stdout/stderr");
        } else {
            Py_DECREF(res);
        }
    }

    int rc = 0;
    FILE *f = fopen(j->script, "rb");
    if (!f) {
        rsl_log("cannot open %s", j->script);
        printf("cannot open %s\n", j->script);
        rc = -1;
    } else {
        PyObject *globals = PyModule_GetDict(PyImport_AddModule("__main__"));
#if PY_MAJOR_VERSION >= 3
        PyObject *file = PyUnicode_FromString(j->script);
#else
        PyObject *file = PyString_FromString(j->script);
#endif
        PyDict_SetItemString(globals, "__file__", file);
        Py_XDECREF(file);
        PyObject *r = PyRun_FileExFlags(f, j->script, Py_file_input, globals, globals, 1, NULL);
        if (r) {
            Py_DECREF(r);
        } else if (PyErr_ExceptionMatches(PyExc_SystemExit)) {
            /* PyErr_Print would call exit() on SystemExit; take the code instead. */
#if PY_MAJOR_VERSION >= 3
            PyObject *exc = PyErr_GetRaisedException();
#else
            PyObject *type = NULL, *exc = NULL, *tb = NULL;
            PyErr_Fetch(&type, &exc, &tb);
            PyErr_NormalizeException(&type, &exc, &tb);
            Py_XDECREF(type);
            Py_XDECREF(tb);
#endif
            rc = exc ? exit_code_of(exc) : 0;
            Py_XDECREF(exc);
        } else {
            log_py_error("script error");
            PyErr_Print();   /* stderr: the console in script mode */
            rc = 1;
        }
    }
    /* The game's threads may still need Python once more: a music decode thread frees
     * its file through Python after Ren'Py's audio.quit(). One that came after
     * Py_FinalizeEx stayed stuck, and its stack crashed the next game (hardware run 22).
     * So let them finish first, with the GIL released. */
    u64 t0 = armGetSystemTick();
    size_t left;
    Py_BEGIN_ALLOW_THREADS
    left = wait_threads(THREAD_WAIT_MS);
    Py_END_ALLOW_THREADS
    rsl_log("before Python shutdown: %zu thread(s) still running after %llu ms", left,
            (unsigned long long)(armTicksToNs(armGetSystemTick() - t0) / 1000000ULL));
    g_interp = NULL;
#if PY_MAJOR_VERSION >= 3
    if (Py_FinalizeEx() < 0)
        rsl_log("Py_FinalizeEx reported an error");
#else
    Py_Finalize();
#endif
    return rc;
}

/* Shut SDL down on the thread that initialised it (Ren'Py's): from the main
 * thread SDL_Quit() hung forever after the game quit (hardware runs 5-6).
 * Each subsystem is logged so a remaining hang names its culprit. */
static void quit_sdl(void)
{
    static const struct { Uint32 flag; const char *name; } subs[] = {
        { SDL_INIT_AUDIO, "audio" }, { SDL_INIT_GAMECONTROLLER, "gamecontroller" },
        { SDL_INIT_JOYSTICK, "joystick" }, { SDL_INIT_HAPTIC, "haptic" },
        { SDL_INIT_SENSOR, "sensor" }, { SDL_INIT_VIDEO, "video" },
        { SDL_INIT_TIMER, "timer" }, { SDL_INIT_EVENTS, "events" },
    };
    /* Ren'Py's set_timer starts SDL's timer thread through SDL_AddTimer, which
     * does not count the timer subsystem as initialised, so SDL_Quit() left the
     * thread running into the loader; it crashed there (hardware run 9). Taking
     * a reference makes the loop below quit the timer, which joins the thread. */
    SDL_InitSubSystem(SDL_INIT_TIMER);
    Uint32 init = SDL_WasInit(SDL_INIT_EVERYTHING);
    rsl_log("SDL shutdown: subsystems 0x%x", (unsigned)init);
    for (size_t i = 0; i < sizeof subs / sizeof subs[0]; i++)
        if (init & subs[i].flag) {
            rsl_log("SDL shutdown: %s", subs[i].name);
            SDL_QuitSubSystem(subs[i].flag);
        }
    SDL_Quit();
    rsl_log("SDL shutdown: done");
}

static void *python_thread(void *arg)
{
    struct job *j = arg;
    j->rc = run_python(j);
    if (j->renpy)
        quit_sdl();
    j->done = true;
    return NULL;
}

bool start_python(struct job *j, pthread_t *th)
{
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, PY_STACK);
    bool ok = pthread_create(th, &attr, python_thread, j) == 0;
    pthread_attr_destroy(&attr);
    if (!ok)
        rsl_log("could not start the Python thread");
    return ok;
}
