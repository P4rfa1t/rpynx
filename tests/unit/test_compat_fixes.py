"""Checks the two game-compat fixes in runtime/renpy/boot/rsl_compat.py, against fakes
modelled on the real failures:

- analysis_cache_check: a cache/py3analysis.rpyb shipped inside a game can hold ASTs whose
  end_lineno is smaller than their lineno (Z.A.T.O. had 7, e.g. an expression at line 959
  with end_lineno 1); CPython 3.12's compile() rejects those with
  "ValueError: AST node line range (959, 1) is not valid", so the game cannot start.
  The fix drops poisoned entries and marks the cache updated.
- archive_listdir: the export packs every non-ASCII-named file into rsl-nonascii.rpa
  (the Switch SD cannot list such names, hardware run 20), so a game's os.listdir() of
  that folder sees it as empty (Z.A.T.O.'s load_fonts() then crashes on the empty map).
  The fix unions the real listing with the loaded archives' members under that folder.

usage: python -m unittest tests/unit/test_compat_fixes.py
"""
import ast
import io
import os
import pickle
import sys
import tempfile
import types
import unittest
import zlib

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "runtime", "renpy", "boot"))

# A minimal renpy package stub: rsl_compat imports renpy at module level and the two
# functions under test reach it only when called.
renpy_stub = types.ModuleType("renpy")
renpy_stub.__path__ = []
sys.modules.setdefault("renpy", renpy_stub)
import rsl_compat  # noqa: E402


def poisoned_ast(lineno, end_lineno):
    """An AST like the shipped cache holds (renpy's ast_eval() returns the body node):
    right shape, wrong line range."""
    tree = ast.parse("If(flag, Rollback(), None)", "game/tl/extend.rpy", "eval")
    tree.body.lineno = lineno
    tree.body.end_lineno = end_lineno
    return tree.body


class FakeCcache:
    def __init__(self):
        self.ast_eval_cache = {}
        self.ast_exec_cache = {}
        self.updated = False


def install_cache_check(monkey_ccache):
    """Run install_analysis_cache_check against a fake renpy.pyanalysis."""
    fake = types.ModuleType("renpy.pyanalysis")
    fake.load_cache = lambda: None
    fake.ccache = monkey_ccache
    sys.modules["renpy.pyanalysis"] = fake
    renpy_stub.pyanalysis = fake
    rsl_compat.install_analysis_cache_check()
    fake.load_cache()


class TestAnalysisCacheCheck(unittest.TestCase):
    def test_poisoned_entries_dropped_and_marked_updated(self):
        good = poisoned_ast(959, 959)
        bad = poisoned_ast(959, 1)
        bad2 = poisoned_ast(1042, 1041)
        cc = FakeCcache()
        cc.ast_eval_cache = {
            ("If(flag, Rollback(), None)", "game/tl/extend.rpy", 959): (bad, False),
            ("other", "game/script.rpy", 3): (poisoned_ast(3, 3), True),
        }
        cc.ast_exec_cache = {("block", None, None): (bad2, False)}

        install_cache_check(cc)

        self.assertEqual(len(cc.ast_eval_cache), 1)   # the good entry stays
        self.assertEqual(len(cc.ast_exec_cache), 0)
        self.assertTrue(cc.updated)

    def test_clean_cache_untouched(self):
        cc = FakeCcache()
        cc.ast_eval_cache = {("x + 1", "game/script.rpy", 5): (poisoned_ast(5, 5), True)}
        install_cache_check(cc)
        self.assertEqual(len(cc.ast_eval_cache), 1)
        self.assertFalse(cc.updated)

    def test_cache_check_lets_surviving_entries_compile(self):
        # End to end over a real (shrunk) poisoned cache: entries left after the fix
        # must compile as renpy compiles them (ast.Expression(body=node), locations set).
        tree = ast.parse("If(flag, Rollback(), None)", "game/tl/extend.rpy", "eval")
        tree.body.lineno, tree.body.end_lineno = 959, 1
        cc = FakeCcache()
        cc.ast_eval_cache = {
            ("If(flag, Rollback(), None)", "game/tl/extend.rpy", 959): (tree.body, False),
            ("1 + 1", "game/tl/extend.rpy", 3): (poisoned_ast(3, 3), True),
        }
        install_cache_check(cc)
        node = cc.ast_eval_cache[("1 + 1", "game/tl/extend.rpy", 3)][0]
        compile(ast.Expression(body=node), "game/tl/extend.rpy", "eval", 0, True)


class TestArchiveListdir(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.gamedir = os.path.join(self.tmp.name, "game")
        os.makedirs(os.path.join(self.gamedir, "tl", "chinese", "fonts"))
        with open(os.path.join(self.gamedir, "tl", "chinese", "fonts", "loose.ttf"), "wb"):
            pass

    def install(self, archives):
        fake_loader = types.ModuleType("renpy.loader")
        fake_loader.archives = archives
        fake_renpy = types.ModuleType("renpy")
        fake_config = types.SimpleNamespace(gamedir=self.gamedir)
        fake_renpy.config = fake_config
        fake_renpy.loader = fake_loader
        real = {k: sys.modules.get(k) for k in ("renpy", "renpy.loader")}
        sys.modules["renpy"] = fake_renpy
        sys.modules["renpy.loader"] = fake_loader
        rsl_compat.install_archive_listdir()
        self.addCleanup(self.restore, real)
        self.addCleanup(setattr, os, "listdir", os._orig_listdir if hasattr(os, "_orig_listdir") else os.listdir)

    # os.listdir is patched in place; the module-level _real_listdir marks whether it is.
    def restore(self, real):
        if os.listdir is not rsl_compat._real_listdir:
            os.listdir = rsl_compat._real_listdir
        for k, v in real.items():
            if v is not None:
                sys.modules[k] = v
            else:
                sys.modules.pop(k, None)

    def test_archive_members_appear(self):
        index = {
            "tl/chinese/fonts/京華老宋体GB.ttf": [(0, 10)],
            "tl/chinese/fonts/汇文明朝体.otf": [(10, 10)],
            "audio/boom.ogg": [(0, 5)],
            "tl/chinese/fonts/nested/deep.otf": [(0, 5)],
        }
        self.install([(("game/rsl-nonascii.rpa"), index)])
        got = os.listdir(os.path.join(self.gamedir, "tl", "chinese", "fonts"))
        self.assertEqual(sorted(got), ["loose.ttf", "nested", "京華老宋体GB.ttf", "汇文明朝体.otf"])

    def test_outside_gamedir_unchanged(self):
        self.install([("game/rsl-nonascii.rpa", {"tl/x.ttf": [(0, 1)]})])
        other = os.path.join(self.tmp.name, "other")
        os.makedirs(other, exist_ok=True)
        self.assertEqual(os.listdir(other), [])

    def test_above_gamedir_unchanged(self):
        self.install([("game/rsl-nonascii.rpa", {"tl/x.ttf": [(0, 1)]})])
        self.assertEqual(os.listdir(self.tmp.name), ["game"])

    def test_reinstall_is_a_no_op(self):
        """post_import also runs after every reload_all; the second install must not
        wrap the first wrapper (each layer would union the archives again - harmless
        but unbounded)."""
        index = {"tl/chinese/fonts/京華老宋体GB.ttf": [(0, 10)]}
        self.install([("game/rsl-nonascii.rpa", index)])
        once = os.listdir
        rsl_compat.install_archive_listdir()   # a second post_import
        self.assertIs(os.listdir, once, "the second install wrapped nothing")


class TestPy2Compat(unittest.TestCase):
    def setUp(self):
        import builtins
        self.orig_sorted = builtins.sorted

        class FakeRevertableList(list):
            pass

        fake_rev = types.ModuleType("renpy.revertable")
        fake_rev.RevertableList = FakeRevertableList
        self.FakeRevertableList = FakeRevertableList
        self.real_rev = sys.modules.get("renpy.revertable")
        sys.modules["renpy.revertable"] = fake_rev
        renpy_stub.revertable = fake_rev
        rsl_compat._real_sorted = None
        rsl_compat._real_revertable_sort = None
        rsl_compat.install_py2_compat()
        self.addCleanup(self.restore)

    def restore(self):
        import builtins
        builtins.sorted = self.orig_sorted
        rsl_compat._real_sorted = None
        rsl_compat._real_revertable_sort = None
        if self.real_rev is not None:
            sys.modules["renpy.revertable"] = self.real_rev
        else:
            sys.modules.pop("renpy.revertable", None)

    def test_sort_and_sorted_accept_cmp(self):
        def rev_cmp(a, b):
            return (b > a) - (b < a)

        items = self.FakeRevertableList(["b", "c", "a"])
        items.sort(cmp=rev_cmp)
        self.assertEqual(items, ["c", "b", "a"])
        self.assertEqual(sorted(["b", "c", "a"], cmp=rev_cmp), ["c", "b", "a"])
        self.assertEqual(sorted(["b", "c", "a"], key=str.upper, cmp=rev_cmp), ["c", "b", "a"])
        self.assertEqual(sorted([3, 1, 2]), [1, 2, 3])


class TestNoopRpyWriteGuard(unittest.TestCase):
    def setUp(self):
        import builtins
        import io
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.gamedir = os.path.join(self.tmp.name, "game")
        os.makedirs(self.gamedir, exist_ok=True)
        self._had_config = hasattr(renpy_stub, "config")
        self._orig_config = getattr(renpy_stub, "config", None)
        renpy_stub.config = types.SimpleNamespace(gamedir=self.gamedir)
        self.orig_builtin_open = builtins.open
        self.orig_io_open = io.open
        rsl_compat._real_builtin_open = builtins.open
        rsl_compat._real_io_open = io.open
        rsl_compat.install_noop_rpy_write_guard()
        self.addCleanup(self.restore)

    def restore(self):
        import builtins
        import io
        builtins.open = self.orig_builtin_open
        io.open = self.orig_io_open
        if self._had_config:
            renpy_stub.config = self._orig_config
        elif hasattr(renpy_stub, "config"):
            delattr(renpy_stub, "config")

    def test_identical_rpy_rewrite_preserves_mtime(self):
        rpy = os.path.join(self.gamedir, "tl.rpy")
        with self.orig_builtin_open(rpy, "wb") as f:
            f.write(b"translate chzh a:\n    \"hello\"\n")
        os.utime(rpy, (1700000000, 1700000000))
        old_mtime = os.path.getmtime(rpy)

        with open(rpy, "w", encoding="utf-8", newline="\n") as f:
            f.write("translate chzh a:\n    \"hello\"\n")
        self.assertEqual(os.path.getmtime(rpy), old_mtime)

        with open(rpy, "w", encoding="utf-8", newline="\n") as f:
            f.write("translate chzh a:\n    \"changed\"\n")
        with self.orig_builtin_open(rpy, "rb") as f:
            self.assertEqual(f.read(), b"translate chzh a:\n    \"changed\"\n")
        self.assertNotEqual(os.path.getmtime(rpy), old_mtime)

    def test_non_rpy_write_not_skipped(self):
        txt = os.path.join(self.gamedir, "note.txt")
        with self.orig_builtin_open(txt, "wb") as f:
            f.write(b"same")
        os.utime(txt, (1700000000, 1700000000))
        with open(txt, "w", encoding="utf-8") as f:
            f.write("same")
        self.assertGreater(os.path.getmtime(txt), 1700000000)


class TestHorizonPosixGuards(unittest.TestCase):
    def test_install_restores_missing_waitpid_attrs(self):
        import importlib.util
        spec = importlib.util.spec_from_file_location(
            "switch_main_test_mod",
            os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
                         "runtime", "renpy", "boot", "switch_main.py"),
        )
        # Inspect _install_horizon_posix_guards without executing main() at import time
        src_path = spec.origin
        ns = {"__file__": src_path}
        with open(src_path, "r", encoding="utf-8") as f:
            src = f.read().replace("\nmain()\n", "\n").replace("\nmain()", "\n")
        exec(compile(src, src_path, "exec"), ns)

        fake_os = types.SimpleNamespace()
        ns_os_orig = ns["os"]
        ns["os"] = fake_os
        try:
            ns["_install_horizon_posix_guards"]()
            self.assertEqual(fake_os.WNOHANG, 1)
            self.assertTrue(fake_os.WIFEXITED(0))
            with self.assertRaises(OSError):
                fake_os.waitpid(-1, 0)
        finally:
            ns["os"] = ns_os_orig


if __name__ == "__main__":
    unittest.main()
