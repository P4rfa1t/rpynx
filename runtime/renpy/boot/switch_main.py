# -*- coding: utf-8 -*-
"""rpynx: game entry point, run instead of renpy.py.

Ren'Py lets a distribution customise the path_to_* functions of its launcher
script (renpy.py); this loads that script unchanged, overrides where logs go
(the loader's log folder, not the game's own folder), installs the
controller bridge, and starts Ren'Py.

The runtime sets sys.argv to [<renpy-base>/renpy.py, <basedir>, "run",
"--savedir", <savedir>] and RSL_LOGDIR to the per-game log folder.
"""
import errno
import os
import sys
import types

BASE = os.path.dirname(os.path.abspath(__file__))
LOGDIR = os.environ.get("RSL_LOGDIR")


def _install_horizon_posix_guards():
    """Horizon fsdev does not support POSIX file locks (returns ENOSYS) or child
    process wait/kill calls. Tolerate ENOSYS on fcntl and provide standard os.waitpid
    constants/stubs so Python 2.7 libraries (subprocess, future, tendo.singleton)
    import cleanly."""
    if not hasattr(os, "WNOHANG"):
        os.WNOHANG = 1
    if not hasattr(os, "WIFSIGNALED"):
        os.WIFSIGNALED = lambda s: (s & 0x7f) != 0 and (s & 0x7f) != 0x7f
    if not hasattr(os, "WTERMSIG"):
        os.WTERMSIG = lambda s: s & 0x7f
    if not hasattr(os, "WIFEXITED"):
        os.WIFEXITED = lambda s: (s & 0x7f) == 0
    if not hasattr(os, "WEXITSTATUS"):
        os.WEXITSTATUS = lambda s: (s >> 8) & 0xff
    if not hasattr(os, "WIFSTOPPED"):
        os.WIFSTOPPED = lambda s: (s & 0xff) == 0x7f
    if not hasattr(os, "WSTOPSIG"):
        os.WSTOPSIG = lambda s: (s >> 8) & 0xff
    if not hasattr(os, "waitpid"):
        def _waitpid(pid, options):
            raise OSError(errno.ENOSYS, "waitpid not supported")
        os.waitpid = _waitpid
    if not hasattr(os, "kill"):
        def _kill(pid, sig):
            raise OSError(errno.ENOSYS, "kill not supported")
        os.kill = _kill

    try:
        import fcntl
    except ImportError:
        m = types.ModuleType("fcntl")
        m.LOCK_SH, m.LOCK_EX, m.LOCK_NB, m.LOCK_UN = 1, 2, 4, 8
        m.lockf = lambda fd, cmd, *a, **kw: None
        m.flock = lambda fd, op: None
        sys.modules["fcntl"] = m
        return
    for name in ("lockf", "flock"):
        orig = getattr(fcntl, name, None)
        if orig is not None and not getattr(orig, "_rsl_guard", False):
            def _guarded(fn=orig):
                def wrapper(*args, **kwargs):
                    try:
                        return fn(*args, **kwargs)
                    except (IOError, OSError) as e:
                        if getattr(e, "errno", None) == errno.ENOSYS:
                            return None
                        raise
                wrapper._rsl_guard = True
                return wrapper
            setattr(fcntl, name, _guarded())

# Not named renpy*: Ren'Py pickles the state of every renpy* module for
# restarts, and this one holds plain functions.
LAUNCHER = "rsl_renpy_launcher"


def load_renpy_launcher():
    path = os.path.join(BASE, "renpy.py")
    if sys.version_info[0] >= 3:
        import importlib.util
        spec = importlib.util.spec_from_file_location(LAUNCHER, path)
        module = importlib.util.module_from_spec(spec)
        sys.modules[LAUNCHER] = module
        spec.loader.exec_module(module)
        return module
    import imp
    return imp.load_source(LAUNCHER, path)


def path_to_logdir(basedir):
    return LOGDIR


def main():
    _install_horizon_posix_guards()
    launcher = load_renpy_launcher()

    if LOGDIR:
        if not os.path.isdir(LOGDIR):
            os.makedirs(LOGDIR)
        launcher.path_to_logdir = path_to_logdir

    import rsl_bridge
    rsl_bridge.install()

    launcher.main()


main()
