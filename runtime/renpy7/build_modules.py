"""Compile pygame_sdl2 and Ren'Py 7.8.7 native modules into librenpy7.a for Switch.

usage: build_modules.py <pygame_sdl2 dir> <renpy dir> <out dir>
env:   CC, AR, CFLAGS, JOBS
"""
from __future__ import print_function
import concurrent.futures
import os
import re
import shlex
import subprocess
import sys

PYGAME_MODULES = [
    ("pygame_sdl2.error", []),
    ("pygame_sdl2.color", []),
    ("pygame_sdl2.controller", []),
    ("pygame_sdl2.rect", []),
    ("pygame_sdl2.rwobject", []),
    ("pygame_sdl2.surface", ["src/alphablit.c"]),
    ("pygame_sdl2.display", []),
    ("pygame_sdl2.event", []),
    ("pygame_sdl2.locals", []),
    ("pygame_sdl2.key", []),
    ("pygame_sdl2.mouse", []),
    ("pygame_sdl2.joystick", []),
    ("pygame_sdl2.power", []),
    ("pygame_sdl2.pygame_time", []),
    ("pygame_sdl2.image", ["src/write_jpeg.c", "src/write_png.c"]),
    ("pygame_sdl2.transform", ["src/SDL2_rotozoom.c"]),
    ("pygame_sdl2.gfxdraw", ["src/SDL_gfxPrimitives.c"]),
    ("pygame_sdl2.draw", []),
    ("pygame_sdl2.font", []),
    ("pygame_sdl2.mixer", []),
    ("pygame_sdl2.mixer_music", []),
    ("pygame_sdl2.scrap", []),
    ("pygame_sdl2.render", []),
]

RENPY_MODULES = [
    ("_renpy", ["IMG_savepng.c", "core.c"]),
    ("_renpybidi", ["renpybidicore.c"]),
    ("renpy.audio.renpysound", ["renpysound_core.c", "ffmedia.c"]),
    ("renpy.audio.filter", []),
    ("renpy.lexersupport", []),
    ("renpy.pydict", []),
    ("renpy.style", []),
    ("renpy.encryption", []),
    ("renpy.compat.dictviews", []),
    ("renpy.styledata.styleclass", []),
    ("renpy.styledata.stylesets", []),
    ("renpy.display.matrix", []),
    ("renpy.display.render", []),
    ("renpy.display.accelerator", []),
    ("renpy.display.quaternion", []),
    ("renpy.uguu.gl", []),
    ("renpy.uguu.uguu", []),
    ("renpy.gl.gldraw", []),
    ("renpy.gl.gltexture", []),
    ("renpy.gl.glenviron_shader", []),
    ("renpy.gl.glrtt_copy", []),
    ("renpy.gl.glrtt_fbo", []),
    ("renpy.gl2.gl2mesh", []),
    ("renpy.gl2.gl2mesh2", []),
    ("renpy.gl2.gl2mesh3", []),
    ("renpy.gl2.gl2polygon", []),
    ("renpy.gl2.gl2model", []),
    ("renpy.gl2.gl2draw", []),
    ("renpy.gl2.gl2texture", []),
    ("renpy.gl2.gl2shader", []),
    ("renpy.text.textsupport", []),
    ("renpy.text.texwrap", []),
    ("renpy.text.ftfont", ["ftsupport.c", "ttgsubtable.c"]),
    ("renpy.text.hbfont", ["ftsupport.c"]),
]


def verify_static_init(c_path, name, mangled):
    with open(c_path, "r") as f:
        code = f.read()
    if ("init" + mangled + "(") not in code:
        raise RuntimeError("expected static init%s in %s" % (mangled, c_path))
    if "." in name and ('Py_InitModule4("%s"' % name) not in code:
        raise RuntimeError('expected Py_InitModule4("%s") in %s' % (name, c_path))


def main():
    pygame_dir, renpy_dir, out = (os.path.abspath(p) for p in sys.argv[1:4])
    cc, ar = os.environ["CC"], os.environ["AR"]
    base_cflags = shlex.split(os.environ["CFLAGS"]) + [
        "-std=gnu99", "-Wno-unused-function",
        "-Wno-implicit-function-declaration", "-Wno-incompatible-pointer-types", "-Wno-int-conversion",
    ]
    pygame_cflags = base_cflags + [
        "-I" + pygame_dir,
        "-I" + os.path.join(pygame_dir, "src"),
        "-I" + os.path.join(pygame_dir, "include"),
        "-I" + os.path.join(pygame_dir, "gen-static"),
    ]
    renpy_cflags = base_cflags + [
        "-I" + os.path.join(renpy_dir, "module"),
        "-I" + os.path.join(renpy_dir, "module", "include"),
        "-I" + os.path.join(renpy_dir, "module", "gen-static"),
        "-I" + os.path.join(pygame_dir, "src"),
        "-I" + os.path.join(pygame_dir, "gen-static"),
    ]
    if not os.path.exists(out):
        os.makedirs(out)

    modules, jobs, seen = [], [], set()

    for name, extras in PYGAME_MODULES:
        modules.append(name)
        mangled = name.replace(".", "_")
        gen_c = os.path.join(pygame_dir, "gen-static", name + ".c")
        verify_static_init(gen_c, name, mangled)
        for src in [gen_c] + [os.path.join(pygame_dir, e) for e in extras]:
            obj = os.path.join(out, "pg_" + os.path.basename(src)[:-2] + ".o")
            if obj in seen:
                continue
            seen.add(obj)
            jobs.append([cc] + pygame_cflags + ["-c", src, "-o", obj])

    renpy_gen = os.path.join(renpy_dir, "module", "gen-static")
    style_mods = sorted(
        fn[:-2] for fn in os.listdir(renpy_gen)
        if fn.startswith("renpy.styledata.style_") and fn.endswith("functions.c")
    )
    all_renpy_modules = list(RENPY_MODULES) + [(m, []) for m in style_mods]

    for name, extras in all_renpy_modules:
        modules.append(name)
        mangled = name.replace(".", "_")
        gen_c = os.path.join(renpy_gen, name + ".c")
        verify_static_init(gen_c, name, mangled)
        for src in [gen_c] + [os.path.join(renpy_dir, "module", e) for e in extras]:
            obj = os.path.join(out, "rp_" + os.path.basename(src)[:-2] + ".o")
            if obj in seen:
                continue
            seen.add(obj)
            jobs.append([cc] + renpy_cflags + ["-c", src, "-o", obj])

    inittab = os.path.join(out, "librenpy7_inittab.c")
    with open(inittab, "w") as f:
        f.write('#include "Python.h"\n\n')
        for m in modules:
            f.write("void init%s(void);\n" % m.replace(".", "_"))
        f.write("\nstatic struct _inittab inittab[] = {\n")
        for m in modules:
            f.write('    { "%s", init%s },\n' % (m, m.replace(".", "_")))
        f.write("    { NULL, NULL },\n};\n\n")
        f.write("int rsl_init_librenpy(void)\n{\n    return PyImport_ExtendInittab(inittab);\n}\n")
    jobs.append([cc] + base_cflags + ["-c", inittab, "-o", inittab[:-2] + ".o"])

    failed = []
    pool = concurrent.futures.ThreadPoolExecutor(int(os.environ.get("JOBS", "4")))
    try:
        results = list(pool.map(lambda c: subprocess.run(c, capture_output=True, text=True), jobs))
    finally:
        pool.shutdown()

    for cmd, res in zip(jobs, results):
        if res.returncode:
            failed.append(cmd[-3])
            sys.stderr.write("FAILED %s\n%s\n" % (cmd[-3], res.stderr[-4000:]))
        elif res.stderr.strip():
            # Keep warning output bounded
            pass
    if failed:
        sys.exit("%d of %d compile jobs failed" % (len(failed), len(jobs)))

    lib = os.path.join(out, "librenpy7.a")
    if os.path.exists(lib):
        os.remove(lib)
    objs = sorted(cmd[-1] for cmd in jobs)
    subprocess.run([ar, "rcsD", lib] + objs, check=True)
    print("%s: %d modules, %d objects" % (lib, len(modules), len(objs)))


if __name__ == "__main__":
    main()
