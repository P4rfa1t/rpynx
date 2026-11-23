"""What would load the Switch: image sizes, video probing (ffprobe), --optimize re-encoding,
and the cover the loader shows."""
from .i18n import tr
import io
import json
import os
import re
import struct
import tempfile
import zlib

from .common import examples, IMAGE_EXTS, read_text, Refused, VIDEO_EXTS
from .rpa import archive_media, extract_member, read_member, rpa_index
from .tools import FFMPEG, FFPROBE, find_tool, run


# What keeps the Switch smooth and cool. Video is decoded in software on a ~1 GHz
# Cortex-A57, and the handheld screen is 1280x720 (1920x1080 docked).
SWITCH_SCREEN = (1280, 720)
DOCKED_SCREEN = (1920, 1080)
MAX_VIDEO_FPS = 30
MAX_TEXTURE = 4096                 # GL_MAX_TEXTURE_SIZE on the Switch (Ren'Py log, run 13)
HEAVY_CODECS = ("hevc", "av1")     # the most expensive to decode in software

# --optimize re-encodes in the same codec family (the game names the file), at
# constant quality so it stays sharp. VP9 tiles let the Switch decode on several cores.
ENCODERS = {
    "vp9": ["-c:v", "libvpx-vp9", "-crf", "31", "-b:v", "0", "-deadline", "good", "-cpu-used", "2",
            "-row-mt", "1", "-tile-columns", "2"],
    "vp8": ["-c:v", "libvpx", "-crf", "8", "-b:v", "8M", "-deadline", "good", "-cpu-used", "1"],
    "h264": ["-c:v", "libx264", "-crf", "20", "-preset", "slow", "-profile:v", "high"],
}


def probe_video(ffprobe, path):
    """{codec, width, height, fps, alpha} of the first video stream, or None."""
    r = run([ffprobe, "-v", "error", "-select_streams", "v:0", "-show_entries",
             "stream=codec_name,width,height,avg_frame_rate,r_frame_rate:stream_tags=alpha_mode",
             "-of", "json", path])
    try:
        st = json.loads(r.stdout)["streams"][0]
        rate = st.get("avg_frame_rate", "0/0")
        if rate.endswith("/0") or rate.startswith("0/"):
            rate = st.get("r_frame_rate", "0/1")
        num, den = rate.split("/")
        return {"codec": st["codec_name"], "width": int(st["width"]), "height": int(st["height"]),
                "fps": float(num) / float(den), "alpha": st.get("tags", {}).get("alpha_mode") == "1"}
    except (ValueError, KeyError, IndexError, ZeroDivisionError):
        return None


def image_size(path):
    """(width, height) from a PNG, JPEG or WebP header (a path or a binary file), or None."""
    if not isinstance(path, str):
        return _image_size(path)
    with open(path, "rb") as f:
        return _image_size(f)


def _image_size(f):
    head = f.read(32)
    if head[:8] == b"\x89PNG\r\n\x1a\n":
        return struct.unpack(">II", head[16:24])
    if head[:4] == b"RIFF" and head[8:12] == b"WEBP":
        kind = head[12:16]
        if kind == b"VP8X":
            return int.from_bytes(head[24:27], "little") + 1, int.from_bytes(head[27:30], "little") + 1
        if kind == b"VP8L":
            bits = int.from_bytes(head[21:25], "little")
            return (bits & 0x3FFF) + 1, ((bits >> 14) & 0x3FFF) + 1
        if kind == b"VP8 ":
            w, h = struct.unpack("<HH", head[26:30])
            return w & 0x3FFF, h & 0x3FFF
        return None
    if head[:2] != b"\xff\xd8":
        return None
    f.seek(2)
    while True:   # JPEG: walk the segments to the frame header (SOFn)
        marker = f.read(2)
        if len(marker) < 2 or marker[0] != 0xFF:
            return None
        length = struct.unpack(">H", f.read(2))[0]
        if 0xC0 <= marker[1] <= 0xCF and marker[1] not in (0xC4, 0xC8, 0xCC):
            h, w = struct.unpack(">xHH", f.read(5))
            return w, h
        f.seek(length - 2, 1)


def performance(gamedir, files, warnings, optimize, workdir=None, *, loose_paths=None):
    """What would load the Switch. Returns (remaining issues, {video: codec} to re-encode,
    {archived video: extracted copy} to export loose, asset coverage, the videos that
    --optimize would re-encode if it were on)."""
    issues, convert, extra = [], {}, {}
    loose_paths = loose_paths or {}
    archived, locked = archive_media(gamedir, files, workdir, warnings)
    images = [(r, loose_paths.get(r) or os.path.join(gamedir, r))
              for r, _ in files if r.lower().endswith(IMAGE_EXTS)]
    images += [(r + tr("（位于 %s）") % m[0], io.BytesIO(read_member(gamedir, m, 256 << 10)))
               for r, m in sorted(archived.items()) if r.lower().endswith(IMAGE_EXTS)]
    big_images = []
    for label, source in images:
        try:
            size = image_size(source)
        except (OSError, struct.error):
            size = None
        if size and max(size) > MAX_TEXTURE:
            big_images.append("%s %dx%d" % (label, size[0], size[1]))
    if big_images:
        issues.append(tr("%d 张图片超出 Switch GPU 的 %dx%d 纹理上限，可能无法显示"
                      "（%s）；请手动缩小它们，游戏的布局依赖其尺寸")
                      % (len(big_images), MAX_TEXTURE, MAX_TEXTURE, examples(big_images, 2)))
    videos = [(r, loose_paths.get(r) or os.path.join(gamedir, r), None)
              for r, _ in files if r.lower().endswith(VIDEO_EXTS)]
    videos += [(r, None, m) for r, m in sorted(archived.items()) if r.lower().endswith(VIDEO_EXTS)]
    coverage = {"loose_media": sum(1 for r, _ in files if r.lower().endswith(VIDEO_EXTS + IMAGE_EXTS)),
                "archived_media": len(archived), "locked_archives": locked}
    if locked:
        issues.append(tr("%d 个压缩包无法检查（%s）：其中的图像和视频可能不适合 Switch")
                      % (len(locked), examples(locked, 2)))
    ffprobe = find_tool(FFPROBE)
    if videos and not ffprobe:
        warnings.append(tr("%d 个视频未检查：请安装 FFmpeg（ffprobe）或用 --tools 指定其文件夹，"
                        "以便为 Switch 检查尺寸和帧率") % len(videos))
        return issues, convert, extra, coverage, []
    fast, large, huge, heavy, unfixable, would_fix = [], [], [], [], [], []
    for rel, path, member in videos:
        if member:   # probe a temporary copy; keep it only if it gets re-encoded
            path = os.path.join(workdir or tempfile.gettempdir(), "rpa-media", *rel.split("/"))
            extract_member(gamedir, member, path)
        v = probe_video(ffprobe, path)
        if not v:
            continue
        can_fix = v["fps"] > MAX_VIDEO_FPS + 0.5 and v["codec"] in ENCODERS and not v["alpha"]
        fixable = optimize and can_fix
        # Label what the export will hold: a re-encoded video is MAX_VIDEO_FPS.
        fps = tr("%d fps（原为 %.0f）") % (MAX_VIDEO_FPS, v["fps"]) if fixable else "%.0f fps" % v["fps"]
        label = "%s%s %dx%d %s %s" % (rel, tr("（位于 %s）") % member[0] if member else "",
                                      v["width"], v["height"], v["codec"], fps)
        if member and fixable:
            extra[rel] = path
        elif member:
            os.remove(path)
        if v["fps"] > MAX_VIDEO_FPS + 0.5:
            if fixable:
                convert[rel] = v["codec"]
            else:
                (unfixable if optimize else fast).append(label)
            if can_fix and not optimize:
                would_fix.append(rel)
        if v["width"] * v["height"] > DOCKED_SCREEN[0] * DOCKED_SCREEN[1]:
            huge.append(label)
        elif v["width"] * v["height"] > SWITCH_SCREEN[0] * SWITCH_SCREEN[1]:
            large.append(label)
        if v["codec"] in HEAVY_CODECS:
            heavy.append(label)
    if fast:
        issues.append(tr("%d 个视频高于 %d fps（%s）：CPU 负担和发热大；--optimize 会以相同尺寸"
                      "重编码为 %d fps") % (len(fast), MAX_VIDEO_FPS, examples(fast, 2), MAX_VIDEO_FPS))
    if unfixable:
        issues.append(tr("%d 个高于 %d fps 的视频 --optimize 无法重编码（带 alpha 或编解码器不支持）：%s")
                      % (len(unfixable), MAX_VIDEO_FPS, examples(unfixable, 2)))
    if huge:
        issues.append(tr("%d 个视频高于 1920x1080（%s）：在 Switch 上难以流畅解码；"
                      "不会自动缩小，因为游戏可能按原尺寸显示它们")
                      % (len(huge), examples(huge, 2)))
    if large:
        issues.append(tr("%d 个视频高于 1280x720 掌机屏幕（%s）：解码负担约为 720p 的两倍")
                      % (len(large), examples(large, 2)))
    if heavy:
        issues.append(tr("%d 个视频使用 HEVC/AV1（%s）：软件解码开销最大")
                      % (len(heavy), examples(heavy, 2)))
    if convert:
        warnings.append(tr("--optimize 将以 %d fps 重编码 %d 个视频（相同尺寸、恒定质量）：%s")
                        % (len(convert), MAX_VIDEO_FPS, examples(convert, 3)))
    if extra:
        warnings.append(tr("其中 %d 个来自压缩包，将导出为散装文件，Ren'Py 会优先于压缩包副本加载它们")
                        % len(extra))
    return issues, convert, extra, coverage, would_fix


def reencode(source, target, codec):
    """Re-encode a video at MAX_VIDEO_FPS with the same codec family and size; audio is
    copied. Returns an error string, or None."""
    ffmpeg = find_tool(FFMPEG)
    if not ffmpeg:
        return tr("未找到 FFmpeg")
    tmp = target + ".part" + os.path.splitext(target)[1]
    r = run([ffmpeg, "-v", "error", "-y", "-i", source, "-map", "0:v:0", "-map", "0:a?",
             "-vf", "fps=%d" % MAX_VIDEO_FPS] + ENCODERS[codec] + ["-c:a", "copy", tmp])
    if r.returncode != 0:
        if os.path.exists(tmp):
            os.remove(tmp)
        return (r.stderr.strip().splitlines() or ["exit %d" % r.returncode])[-1][:120]
    os.replace(tmp, target)
    return None


# The loader's cover (docs/LOADER_UI.md "Covers"): a baseline JPEG of this size, next
# to game/, named by the manifest's "cover".
ART = {"cover": (300, 450)}
MENU_ART = "gui/main_menu.png"     # Ren'Py's template default for gui.main_menu_background
_SCRIPT_TEXT_EXTS = (".rpy", ".rpym")
_SCRIPT_COMPILED_EXTS = (".rpyc", ".rpymc")
_MAX_COVER_SCRIPT = 8 << 20        # bounded read/decompress per script when scanning for cover art
_IMAGE_EXT_RE = "|".join(re.escape(ext.lstrip(".")) for ext in IMAGE_EXTS)
_IMAGE_LITERAL_RE = re.compile(r"[uU]?(['\"])([^'\"\\\x00-\x1f\r\n]+\.(?:" + _IMAGE_EXT_RE + r"))\1", re.I)
_QUOTED_STR_RE = re.compile(r"[uU]?(['\"])([^'\"\\\x00-\x1f\r\n]+)\1")
_MAIN_MENU_BG_RE = re.compile(
    r"^\s*(?:define\s+)?gui\.main_menu_background\s*=\s*[uU]?(['\"])([^'\"\\\r\n]+)\1\s*(?:#.*)?$", re.M)
_MM_ROOT_BG_RE = re.compile(r"\bstyle\.mm_root\.background\s*=\s*[uU]?(['\"])([^'\"\\\r\n]+)\1")
_WINDOW_ICON_RE = re.compile(r"\bconfig\.window_icon\s*=\s*[uU]?(['\"])([^'\"\\\r\n]+)\1")
_TAG_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_ -]*$")
_TEMPLATE_STEMS = ("gui/main_menu", "gui/menu_bg")
_TEMPLATE_BASENAME_RE = re.compile(r"^(?:bg[-_])?main(?:[-_](?:menu|bg))?\.", re.I)
_LOGO_BASENAME_RE = re.compile(r"(?:^|[-_])(?:logo(?:large)?|title)(?:[-_.]|$)", re.I)
_PLAIN_BG_RE = re.compile(r"^(?:bg[-_]?main|main[-_]?bg|menu[-_]?bg|bg[-_]?menu|background|pattern|tile)\.", re.I)
_CHROME_PATH_RE = re.compile(r"(?:^|/)(?:overlay|button|buttons|scrollbar|slider|bar|phone)/|locked|hover|idle|selected|cred", re.I)


def convert_art(source, target, size, overlay=None):
    """One picture as the loader reads it: a baseline JPEG of exactly size. A picture of
    another aspect is fitted whole into the middle (scale with force_original_aspect_ratio
    =decrease, overlaid centred); the rest of the frame is the same picture scaled up and
    blurred (GOG/Netflix style), so nothing of the source is cropped away - the loader
    fills its 2:3 cell by cropping, and here there is nothing to crop. Any image FFmpeg
    reads will do. When overlay is given (e.g. a title logo over a plain menu background),
    it is centred on top of the background. Returns an error string, or None."""
    ffmpeg = find_tool(FFMPEG)
    if not ffmpeg:
        return tr("封面需要 FFmpeg；请安装它或用 --tools 指定其文件夹")
    w, h = size
    part = target + ".part"
    if overlay:
        ow, oh = int(w * 0.86), int(h * 0.86)
        cmd = [ffmpeg, "-v", "error", "-y", "-i", source, "-i", overlay, "-frames:v", "1", "-filter_complex",
               "[0:v]scale=%d:%d:force_original_aspect_ratio=increase,crop=%d:%d[bg];"
               "[1:v]scale=%d:%d:force_original_aspect_ratio=decrease[fg];"
               "[bg][fg]overlay=(W-w)/2:(H-h)/2" % (w, h, w, h, ow, oh),
               "-pix_fmt", "yuvj420p", "-q:v", "2", "-f", "mjpeg", part]
    else:
        cmd = [ffmpeg, "-v", "error", "-y", "-i", source, "-frames:v", "1", "-filter_complex",
               "[0:v]scale=%d:%d:force_original_aspect_ratio=increase,crop=%d:%d,gblur=sigma=30[bg];"
               "[0:v]scale=%d:%d:force_original_aspect_ratio=decrease[fg];"
               "[bg][fg]overlay=(W-w)/2:(H-h)/2" % (w, h, w, h, w, h),
               "-pix_fmt", "yuvj420p", "-q:v", "2", "-f", "mjpeg", part]
    r = run(cmd)
    if r.returncode != 0 or not os.path.isfile(part):
        if os.path.exists(part):
            os.remove(part)
        return (r.stderr.strip().splitlines() or ["exit %d" % r.returncode])[-1][:120]
    os.replace(part, target)
    return None


def _decompress_rpyc(data):
    """Decompress legacy or RPC2 slot 1/2 payloads with a bounded size; never unpickles."""
    if not data:
        return []
    if not data.startswith(b"RENPY RPC2"):
        streams = [data]
    elif len(data) >= 46:
        streams = [data[offset:offset + length]
                   for slot, offset, length in struct.iter_unpack("<III", data[10:46])
                   if slot in (1, 2) and 46 <= offset <= len(data) - length]
    else:
        return []
    out = []
    for stream in streams:
        try:
            out.append(zlib.decompressobj().decompress(stream, _MAX_COVER_SCRIPT))
        except zlib.error:
            continue
    return out


def _indented_block(text, match):
    """Return the matched line and all following lines indented deeper than it."""
    line_start = text.rfind("\n", 0, match.start()) + 1
    first_line = text[line_start:match.end()]
    indent = len(first_line) - len(first_line.lstrip(" \t"))
    pos = text.find("\n", match.end())
    if pos == -1:
        return text[line_start:]
    end = pos + 1
    while end < len(text):
        next_nl = text.find("\n", end)
        line = text[end:] if next_nl == -1 else text[end:next_nl]
        stripped = line.lstrip(" \t")
        if stripped and not stripped.startswith("#"):
            if len(line) - len(stripped) <= indent:
                break
        end = len(text) if next_nl == -1 else next_nl + 1
    return text[line_start:end]


def _script_priority(rel):
    low = rel.lower()
    if low == "gui.rpy" or low == "gui.rpyc":
        return (0, low)
    if low == "screens.rpy" or low == "screens.rpyc":
        return (1, low)
    if low == "options.rpy" or low == "options.rpyc":
        return (2, low)
    return (3, low)


def _collect_scripts(gamedir, files, extra):
    """Collect (rpy_texts, rpyc_texts) from loose files (preferring extra) and standard .rpa
    archives, skipping translation folders (tl/) and never unpickling."""
    rpy_items, rpyc_items = [], []
    seen_rpy, seen_rpyc = set(), set()

    def add_rpy(rel, text):
        low = rel.lower()
        if not text or low in seen_rpy or low.startswith("tl/"):
            return
        seen_rpy.add(low)
        rpy_items.append((rel, text))

    def add_rpyc(rel, raw):
        low = rel.lower()
        if not raw or low in seen_rpyc or low.startswith("tl/"):
            return
        seen_rpyc.add(low)
        for payload in _decompress_rpyc(raw):
            rpyc_items.append((rel, payload.decode("latin-1", "ignore")))

    extra = extra or {}
    all_loose = {}
    for rel, size in files:
        if not rel.startswith("../"):
            all_loose[rel] = (extra.get(rel) or os.path.join(gamedir, *rel.split("/")), size)
    for rel, path in extra.items():
        if not rel.startswith("../") and rel not in all_loose:
            try:
                all_loose[rel] = (path, os.path.getsize(path))
            except OSError:
                continue

    for rel in sorted(all_loose, key=_script_priority):
        path, size = all_loose[rel]
        low = rel.lower()
        if low.startswith("tl/") or size > _MAX_COVER_SCRIPT:
            continue
        if low.endswith(_SCRIPT_TEXT_EXTS):
            add_rpy(rel, read_text(path) or "")
        elif low.endswith(_SCRIPT_COMPILED_EXTS):
            try:
                with open(path, "rb") as f:
                    add_rpyc(rel, f.read(_MAX_COVER_SCRIPT))
            except OSError:
                continue

    for rel, _ in files:
        if rel.startswith("../") or not rel.lower().endswith(".rpa"):
            continue
        rpa_path = extra.get(rel) or os.path.join(gamedir, *rel.split("/"))
        try:
            index = rpa_index(rpa_path) or {}
        except (ValueError, zlib.error, OSError, KeyError, IndexError, TypeError, AttributeError):
            continue
        for name in sorted(index, key=_script_priority):
            low = name.lower()
            if low.startswith("tl/"):
                continue
            offset, length, prefix = index[name]
            if length + len(prefix) > _MAX_COVER_SCRIPT:
                continue
            if low.endswith(_SCRIPT_TEXT_EXTS) and low not in seen_rpy:
                try:
                    raw = read_member(gamedir, (rel, offset, length, prefix), _MAX_COVER_SCRIPT)
                    add_rpy(name, raw.decode("utf-8", "replace"))
                except (OSError, ValueError):
                    continue
            elif low.endswith(_SCRIPT_COMPILED_EXTS) and low not in seen_rpyc:
                try:
                    raw = read_member(gamedir, (rel, offset, length, prefix), _MAX_COVER_SCRIPT)
                    add_rpyc(name, raw)
                except (OSError, ValueError):
                    continue

    rpy_items.sort(key=lambda item: _script_priority(item[0]))
    rpyc_items.sort(key=lambda item: _script_priority(item[0]))
    return [t for _, t in rpy_items], [t for _, t in rpyc_items]


def _menu_art_parts(gamedir, files, workdir, extra=None):
    """Find the game's cover artwork using general Ren'Py 6/7/8 conventions.
    Returns (display_name, source_path, overlay_path) or None. When a game uses a
    layered main menu without gui/main_menu.*, a title logo is paired over its menu
    background (overlay_path) instead of showing a blank background texture alone."""
    extra = extra or {}
    loose = {r.lower(): r for r, _ in files if not r.startswith("../")}
    for r in extra:
        if not r.startswith("../"):
            loose.setdefault(r.lower(), r)
    archived = {r.lower(): (r, member) for r, member in archive_media(gamedir, files, workdir, [])[0].items()}

    def resolve(name, slot="menu"):
        if not name:
            return None
        clean = name.strip().replace("\\", "/")
        while clean.startswith("./") or clean.startswith("/"):
            clean = clean[2:] if clean.startswith("./") else clean[1:]
        low = clean.lower()
        if not low.endswith(IMAGE_EXTS):
            return None
        candidates = [low]
        if low.startswith("images/"):
            candidates.append(low[7:])
        else:
            candidates.append("images/" + low)
        for key in candidates:
            if key in loose:
                rel = loose[key]
                return rel, extra.get(rel) or os.path.join(gamedir, *rel.split("/"))
            if key in archived:
                rel, member = archived[key]
                path = os.path.join(workdir, "art", slot + os.path.splitext(rel)[1])
                os.makedirs(os.path.dirname(path), exist_ok=True)
                extract_member(gamedir, member, path)
                return tr("%s（位于 %s）") % (rel, member[0]), path
        return None

    def candidate_area(key):
        try:
            if key in loose:
                rel = loose[key]
                size = image_size(extra.get(rel) or os.path.join(gamedir, *rel.split("/")))
            else:
                size = image_size(io.BytesIO(read_member(gamedir, archived[key][1], 256 << 10)))
            return (size[0] * size[1]) if size else 0
        except (OSError, struct.error):
            return 0

    def is_plain_bg(found):
        base = found[0].split("（", 1)[0].rsplit("/", 1)[-1]
        return bool(_PLAIN_BG_RE.match(base))

    # Fast path: direct gui.main_menu_background image path or standard gui/main_menu.* cover.
    gui_rpy_path = extra.get("gui.rpy") or os.path.join(gamedir, "gui.rpy")
    gui_text = read_text(gui_rpy_path) or ""
    m = _MAIN_MENU_BG_RE.search(gui_text)
    if m:
        found = resolve(m.group(2), "menu")
        if found and not is_plain_bg(found):
            return found[0], found[1], None
    for ext in IMAGE_EXTS:
        found = resolve("gui/main_menu" + ext, "menu")
        if found:
            return found[0], found[1], None

    rpy_texts, rpyc_texts = _collect_scripts(gamedir, files, extra)

    def find_background():
        bg_tags = []

        def note_bg_value(val):
            found = resolve(val, "menu_bg")
            if found:
                return found
            tag = val.strip()
            if tag and "." not in tag and _TAG_RE.match(tag) and tag not in bg_tags:
                bg_tags.append(tag)
            return None

        for text in rpy_texts + rpyc_texts:
            for pat in (_MAIN_MENU_BG_RE, _MM_ROOT_BG_RE):
                for match in pat.finditer(text):
                    found = note_bg_value(match.group(2))
                    if found:
                        return found
        for text in rpyc_texts:
            for match in re.finditer(r"main_menu_background", text):
                before = list(_QUOTED_STR_RE.finditer(text[max(0, match.start() - 250):match.start()]))
                after = list(_QUOTED_STR_RE.finditer(text[match.end():match.end() + 250]))
                for q in (list(reversed(before)) + after):
                    val = q.group(2)
                    if val.lower().endswith(IMAGE_EXTS):
                        found = resolve(val, "menu_bg")
                        if found:
                            return found
                    elif "." not in val and _TAG_RE.match(val):
                        if val not in bg_tags:
                            bg_tags.append(val)
                        break

        for tag in bg_tags:
            img_def_re = re.compile(r"^[ \t]*image\s+" + re.escape(tag) + r"\b[^\n]*", re.M)
            renpy_img_re = re.compile(r"renpy\.image\(\s*[uU]?(['\"])" + re.escape(tag) + r"\1\s*,")
            for text in rpy_texts:
                for match in img_def_re.finditer(text):
                    block = _indented_block(text, match)
                    for lit in _IMAGE_LITERAL_RE.finditer(block):
                        found = resolve(lit.group(2), "menu_bg")
                        if found:
                            return found
                for match in renpy_img_re.finditer(text):
                    for lit in _IMAGE_LITERAL_RE.finditer(text[match.end():match.end() + 500]):
                        found = resolve(lit.group(2), "menu_bg")
                        if found:
                            return found
            tag_tok_re = re.compile(r"(?<![A-Za-z0-9_])" + re.escape(tag) + r"(?![A-Za-z0-9_])")
            for text in rpyc_texts:
                for match in renpy_img_re.finditer(text):
                    for lit in _IMAGE_LITERAL_RE.finditer(text[match.end():match.end() + 500]):
                        found = resolve(lit.group(2), "menu_bg")
                        if found:
                            return found
                for match in tag_tok_re.finditer(text):
                    around = text[max(0, match.start() - 300):match.end() + 300]
                    for lit in _IMAGE_LITERAL_RE.finditer(around):
                        found = resolve(lit.group(2), "menu_bg")
                        if found:
                            return found

        screen_re = re.compile(r"^[ \t]*screen\s+main_menu\b[^\n]*:", re.M)
        func_re = re.compile(r"^[ \t]*def\s+\w*(?:main_menu|mm_\w*bg\w*)\w*\s*\(", re.M)
        bg_add_re = re.compile(r"\b(?:background|add)\s+[^\n]*?" + _IMAGE_LITERAL_RE.pattern, re.I)
        for text in rpy_texts:
            for match in screen_re.finditer(text):
                block = _indented_block(text, match)
                for m_ba in bg_add_re.finditer(block):
                    if "overlay/" not in m_ba.group(2).lower():
                        found = resolve(m_ba.group(2), "menu_bg")
                        if found:
                            return found
            for match in func_re.finditer(text):
                block = _indented_block(text, match)
                for lit in _IMAGE_LITERAL_RE.finditer(block):
                    if "overlay/" not in lit.group(2).lower():
                        found = resolve(lit.group(2), "menu_bg")
                        if found:
                            return found
        rpyc_mm_re = re.compile(r"(?:screen\s+main_menu\b[^\n]*:|def\s+\w*(?:main_menu|mm_\w*bg\w*)\w*\s*\()")
        for text in rpyc_texts:
            for match in rpyc_mm_re.finditer(text):
                block = _indented_block(text, match)
                for lit in _IMAGE_LITERAL_RE.finditer(block):
                    if "overlay/" not in lit.group(2).lower():
                        found = resolve(lit.group(2), "menu_bg")
                        if found:
                            return found

        for stem in _TEMPLATE_STEMS:
            for ext in IMAGE_EXTS:
                found = resolve(stem + ext, "menu_bg")
                if found:
                    return found

        template_matches = [
            key for key in sorted(set(loose) | set(archived))
            if key.endswith(IMAGE_EXTS) and not _CHROME_PATH_RE.search(key)
            and _TEMPLATE_BASENAME_RE.match(key.rsplit("/", 1)[-1])
        ]
        if template_matches:
            return resolve(max(template_matches, key=candidate_area), "menu_bg")
        return None

    def find_logo_or_icon():
        logo_matches = [
            key for key in sorted(set(loose) | set(archived))
            if key.endswith(IMAGE_EXTS) and not _CHROME_PATH_RE.search(key)
            and _LOGO_BASENAME_RE.search(key.rsplit("/", 1)[-1])
        ]
        if logo_matches:
            best = max(logo_matches, key=lambda key: (candidate_area(key), -len(key.rsplit("/", 1)[-1])))
            return resolve(best, "menu_logo")
        for text in rpy_texts + rpyc_texts:
            for match in _WINDOW_ICON_RE.finditer(text):
                found = resolve(match.group(2), "menu_logo")
                if found:
                    return found
        for text in rpyc_texts:
            for match in re.finditer(r"window_icon", text):
                for lit in _IMAGE_LITERAL_RE.finditer(text[match.end():match.end() + 300]):
                    found = resolve(lit.group(2), "menu_logo")
                    if found:
                        return found
        return None

    bg = find_background()
    if bg and not is_plain_bg(bg):
        return bg[0], bg[1], None
    logo = find_logo_or_icon()
    if logo and bg and logo[1] != bg[1]:
        return logo[0], bg[1], logo[1]
    if logo:
        return logo[0], logo[1], None
    if bg:
        return bg[0], bg[1], None
    return None


def menu_art(gamedir, files, workdir, extra=None):
    """Find the game's cover picture using general Ren'Py 6/7/8 conventions.
    Returns (name, path) or None."""
    parts = _menu_art_parts(gamedir, files, workdir, extra=extra)
    return (parts[0], parts[2] or parts[1]) if parts else None


def make_art(manifest, gamedir, files, workdir, cover=None, extra=None):
    """The cover that export() writes next to game/, made in workdir. cover is a picture
    the user chose (marked "cover_custom" in the manifest, which --replace keeps); without
    one it comes from the game's menu art. Names it in the manifest and returns
    {key: path}. A chosen picture that cannot be converted is refused; the game's own is a
    warning, and without one the loader shows a placeholder."""
    chosen = {"cover": cover}
    os.makedirs(os.path.join(workdir, "art"), exist_ok=True)
    menu = None if cover else _menu_art_parts(gamedir, files, workdir, extra=extra)
    art, failed = {}, {}
    for key, size in ART.items():
        source = chosen[key] or (menu and menu[1])
        overlay = None if chosen[key] else (menu and menu[2])
        if not source:
            continue
        path = os.path.join(workdir, "art", key + ".jpg")
        error = convert_art(source, path, size, overlay=overlay)
        if error and chosen[key]:
            raise Refused("%s %s: %s" % (key, chosen[key], error))
        if error:
            failed[key] = error
        else:
            art[key] = path
    for key in ART:
        manifest.pop(key, None)
        manifest.pop(key + "_custom", None)
        if key in art:
            manifest[key] = key + ".jpg"
            if chosen[key]:
                manifest[key + "_custom"] = True
    if failed:
        names = {"cover": tr("封面")}
        manifest["warnings"].append(tr("无法从 %s 生成 %s：%s") % (menu[0],
                                     tr(" 或 ").join(names.get(k, k) for k in failed),
                                     "；".join(sorted(set(failed.values())))))
    return art
