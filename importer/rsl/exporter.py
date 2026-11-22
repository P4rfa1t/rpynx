"""Writing an analysed game into the library, on disk or over FTP."""
from .i18n import tr
import contextlib
import json
import os
import shutil
import tempfile
import unicodedata

from .common import ERRORS, Refused
from .library import games_dir, in_game_folder, is_ftp, library_game, open_library
from .mtp import is_mtp
from .media import ART, convert_art, reencode
from .rpa import packable, PACKED, Rpa
from .tools import rmtree


PACKED_MAX = 2 << 30               # bytes per archive, well under FAT32's 4 GiB


def manifest_json(manifest):
    return json.dumps(manifest, indent=2, ensure_ascii=False) + "\n"


def place(rel):
    """The path components a file's name lands at, relative to its game's export folder
    games/<id>/. A "../"-prefixed name is a file that ships beside game/ in the source
    (DDLC keeps characters/*.chr there and opens them as "../characters/monika.chr" and
    as config.basedir + "/characters/monika.chr"): the export's basedir is games/<id>
    itself, so it lands at games/<id>/characters/..., beside game/, not inside it."""
    parts = rel.split("/")
    if parts[0] == "..":
        return parts[1:]
    return ["game"] + parts


def copy_files(manifest, gamedir, files, extra, progress, put, workdir, mkdir=None):
    """Passes every exported file to put(local path, rel, tick); a video to re-encode is
    written into workdir first, and packable() files go into PACKED archives built there.
    tick(fraction) reports progress within a file. mkdir(dir) creates an empty folder in
    the export for a packed file's parents (a game checks os.path.exists(folder) or lists
    it: the folder must exist, its non-ASCII files only live inside the archive)."""
    convert = manifest["performance"]["converted"]
    extra = extra or {}
    todo = list(dict.fromkeys([r for r, _ in files] + sorted(extra)))   # decompiled .rpy are in both
    recode = [r for r in todo if r in convert]
    rpa, count = None, 0

    def finish(done):
        rpa.close()
        rel = PACKED + ("-%d" % count if count > 1 else "") + ".rpa"
        progress(done, len(todo), rel, "")
        put(rpa.path, rel, lambda fraction: progress(done, len(todo), rel, ""))
        os.remove(rpa.path)

    try:
        for done, rel in enumerate(todo):
            source = extra.get(rel) or os.path.join(gamedir, *rel.split("/"))   # "../": beside game/
            if rel in convert:
                progress(done, len(todo), rel, tr("重编码 %d/%d") % (recode.index(rel) + 1, len(recode)))
                target = os.path.join(workdir, "reencoded" + os.path.splitext(rel)[1])
                error = reencode(source, target, convert[rel])
                if error:
                    del convert[rel]
                    manifest["performance"]["issues"].append(tr("%s：重编码失败（%s）；已按原样复制") % (rel, error))
                else:
                    source = target
            else:
                progress(done, len(todo), rel, "")
            if not packable(rel):
                put(source, rel, lambda fraction, done=done, rel=rel: progress(done + fraction, len(todo), rel, ""))
                continue
            if mkdir:
                parents = rel.split("/")[:-1]
                for i in range(1, len(parents) + 1):
                    mkdir("/".join(parents[:i]))
            if rpa and rpa.index and rpa.f.tell() + os.path.getsize(source) > PACKED_MAX:
                finish(done)
                rpa = None
            if rpa is None:
                count += 1
                rpa = Rpa(os.path.join(workdir, "packed.rpa"))
            rpa.add(unicodedata.normalize("NFC", rel), source)   # as a script spells it; macOS may hand back NFD
        if rpa:
            finish(len(todo))
    except BaseException:
        if rpa:
            rpa.f.close()   # workdir is removed by the caller, also on Windows
        raise
    progress(len(todo), len(todo), "", "")


def keep_chosen(manifest, art, old):
    """--replace: a cover the user chose before (in the window, with --cover or on the
    Switch) stays, unless this export brings a chosen one too. Updates manifest and art;
    returns the file names to keep from the old export."""
    keep = []
    for key in ART:
        name = old.get(key) if isinstance(old, dict) and old.get(key + "_custom") else None
        if in_game_folder(name) and "/" not in name and not manifest.get(key + "_custom"):
            manifest[key], manifest[key + "_custom"] = old[key], True
            art.pop(key, None)
            keep.append(old[key])
    return keep


def export(manifest, gamedir, files, out, replace, extra=None, progress=None, art=None):
    """Writes games/<id>/, made of game/ plus any files that ship beside it (see place()),
    manifest.json and the pictures in art ({key: path}, from make_art()). Refuses to
    overwrite unless replace; keeps game/saves (Ren'Py writes saves there too) and the
    pictures the user chose, and never touches saves/<id>.
    progress(done, total, rel, note) is called before each file, note being "re-encoding
    1/2" or ""; if it raises, the staged copy is removed and the old export stays as it was.
    Over FTP it is also called during an upload, with done + the fraction sent."""
    if manifest["status"] == "unsupported":
        raise Refused(tr("未导出：没有任何运行时能运行此游戏"))
    progress = progress or (lambda *a: None)
    manifest, art = dict(manifest), dict(art or {})   # what --replace keeps is this export's only
    if is_ftp(out) or is_mtp(out):
        return remote_export(manifest, gamedir, files, out, replace, extra, progress, art)
    games = games_dir(out)
    dest = os.path.join(games, manifest["id"])
    if os.path.lexists(dest) and not replace:
        raise Refused(tr("%s 已存在；请加 --replace 以更新其 game/ 和 manifest.json"
                      "（存档保留）") % dest)
    try:
        with open(os.path.join(dest, "manifest.json"), encoding="utf-8") as f:
            keep_chosen(manifest, art, json.load(f))   # the kept files stay where they are
    except (OSError, ValueError):
        pass
    os.makedirs(games, exist_ok=True)
    stage = tempfile.mkdtemp(prefix=".rsl-import-", dir=games)   # same volume: renames are atomic
    try:
        new = os.path.join(stage, "game")   # the game folder; ../ names land in stage, beside it

        def put(source, rel, tick):
            target = os.path.join(stage, *place(rel))   # game/…, or ../ names beside game/
            os.makedirs(os.path.dirname(target), exist_ok=True)
            shutil.copy2(source, target)   # mtimes: a .rpyc older than its .rpy recompiles at every launch

        def mkdir(rel_dir):
            os.makedirs(os.path.join(new, *rel_dir.split("/")), exist_ok=True)

        copy_files(manifest, gamedir, files, extra, progress, put, stage, mkdir)
        os.makedirs(new, exist_ok=True)
        for key, path in art.items():
            shutil.copy2(path, os.path.join(stage, manifest[key]))
        with open(os.path.join(stage, "manifest.json"), "w", encoding="utf-8") as f:
            f.write(manifest_json(manifest))
        os.makedirs(dest, exist_ok=True)
        old = os.path.join(dest, "game")
        if os.path.isdir(os.path.join(old, "saves")):
            os.rename(os.path.join(old, "saves"), os.path.join(new, "saves"))
        if os.path.lexists(old):
            os.rename(old, os.path.join(stage, "old-game"))
        os.rename(new, old)
        for rootdir, dirnames, filenames in os.walk(stage):   # beside game/, just now staged
            rel_dir = os.path.relpath(rootdir, stage).replace(os.sep, "/")
            if rel_dir == "." or rel_dir.split("/")[0] in ("game", "old-game"):
                continue
            target_dir = os.path.join(dest, *rel_dir.split("/"))
            os.makedirs(target_dir, exist_ok=True)
            for name in filenames:   # replace the old copies; other files beside game/ stay
                os.replace(os.path.join(rootdir, name), os.path.join(target_dir, name))
        for key in art:
            os.replace(os.path.join(stage, manifest[key]), os.path.join(dest, manifest[key]))
        os.replace(os.path.join(stage, "manifest.json"), os.path.join(dest, "manifest.json"))
    finally:
        rmtree(stage)
    return dest


def remote_export(manifest, gamedir, files, out, replace, extra, progress, art):
    """export() to the Switch: uploads into games/.uploading-<id>, then renames it into
    place. The previous version waits in games/.old-<id> until its game/saves and chosen
    pictures are moved over."""
    gid = manifest["id"]
    dest, stage, old = "games/" + gid, "games/.uploading-" + gid, "games/.old-" + gid
    with contextlib.closing(open_library(out)) as remote:
        have = remote.entries("games")
        if gid in have and not replace:
            raise Refused(tr("%s/%s 已存在；请替换以更新其 game/ 和 manifest.json"
                          "（存档保留）") % (games_dir(out), gid))
        if ".old-" + gid in have:
            raise Refused(tr("%s/.old-%s 是一次中途停止的更新留下的，可能存有存档（game/saves）；"
                          "请先取出需要的内容，删除它，然后重试")
                          % (games_dir(out), gid))
        if ".uploading-" + gid in have:
            remote.rmtree(stage)   # a cancelled or failed upload; it never holds saves
        keep = keep_chosen(manifest, art, library_game(remote, gid)) if gid in have else []
        # The sources' size: what is sent, give or take a re-encoded video and the archive index.
        extra = extra or {}
        remote.announce(sum(os.path.getsize(extra.get(rel) or os.path.join(gamedir, *rel.split("/")))
                         for rel in dict.fromkeys([r for r, _ in files] + sorted(extra)))
                     + sum(os.path.getsize(p) for p in art.values()))
        def put(source, rel, tick):   # as on disk: game/…, and ../ names beside it
            remote.put(source, stage + "/" + "/".join(place(rel)), tick)
        try:
            work = tempfile.mkdtemp(prefix="rsl-import-")
            try:
                copy_files(manifest, gamedir, files, extra, progress, put, work,
                           lambda rel_dir: remote.mkdirs(stage + "/game/" + rel_dir))
            finally:
                rmtree(work)
            for key, path in art.items():
                remote.put(path, stage + "/" + manifest[key])
            remote.mkdirs(stage + "/game")
            remote.write(stage + "/manifest.json", manifest_json(manifest).encode("utf-8"))
        except BaseException:
            remote.close()   # it may be mid-transfer: clean up over a new connection
            with contextlib.suppress(*ERRORS), contextlib.closing(open_library(out)) as again:
                again.rmtree(stage)
            raise
        try:
            if gid in have:
                remote.rename(dest, old)
            remote.rename(stage, dest)
            if gid in have:
                if "saves" in remote.entries(old + "/game"):
                    remote.rename(old + "/game/saves", dest + "/game/saves")
                for name in keep:
                    if name in remote.entries(old):
                        remote.rename(old + "/" + name, dest + "/" + name)
                remote.rmtree(old)
        except ERRORS as e:
            raise Refused(tr("上传已完成，但将其移动到位时中断了（%s）。在 %s 中，"
                          ".uploading-%s 是新副本，.old-%s 是上一个副本，其中含其 game/saves")
                          % (e, games_dir(out), gid, gid))
    return games_dir(out) + "/" + gid


def set_art(out, game_id, key, image):
    """A cover the user chose for a game already in the library: image made
    into games/<id>/<key>.jpg and named in its manifest.json as chosen, so that an export
    with --replace keeps it."""
    work = tempfile.mkdtemp(prefix="rsl-art-")
    try:
        path = os.path.join(work, key + ".jpg")
        error = convert_art(image, path, ART[key])
        if error:
            raise Refused("%s: %s" % (os.path.basename(image), error))
        with contextlib.closing(open_library(out)) as lib:
            manifest = library_game(lib, game_id)
            if manifest is None:
                raise Refused(tr("%s 没有 manifest.json（其中记录图片）；请先在导入器中导出它") % game_id)
            with open(path, "rb") as f:
                lib.write("games/%s/%s.jpg" % (game_id, key), f.read())
            manifest[key], manifest[key + "_custom"] = key + ".jpg", True
            lib.write("games/%s/manifest.json" % game_id, manifest_json(manifest).encode("utf-8"))
    finally:
        rmtree(work)
