"""USB device discovery and the game library's path-based MTP transport."""

import contextlib
import os
import posixpath
import sys
import tempfile
import urllib.parse
import uuid

from .common import Refused
from .i18n import tr


def is_mtp(target):
    return target.lower().startswith("mtp://")


def _backend():
    if sys.platform == "win32":
        from . import mtp_wpd as backend
    else:
        from . import mtp_libusb as backend
    return backend


def devices():
    return [{"name": name, "target": "mtp://" + urllib.parse.quote(key, safe="")}
            for key, name in _backend().discover()]


def _parts(path):
    parts = path.split("/") if path else []
    if any(not part or part in (".", "..") or part[-1] in ". " or
           any(ord(c) < 32 or c in '\\\":<>|?*' for c in part) for part in parts):
        raise Refused(tr("不安全的 MTP 路径：%r") % path)
    return parts


class Mtp:
    """Paths are relative to the rpynx storage root, like Ftp's library API."""

    def __init__(self, target):
        key = target[6:].rstrip("/")
        if not key or "/" in key or "?" in key or "#" in key:
            raise Refused(tr("请从已连接的 rpynx USB 设备中选择一个 MTP 目标。"))
        self.device = _backend().Device(urllib.parse.unquote(key))
        self._dirs = set()

    def path(self, path):
        return "/".join(_parts(path))

    def entries(self, path):
        try:
            return {name: info[1] for name, info in self.device.entries(self.path(path)).items()}
        except FileNotFoundError:
            return {}

    def mkdirs(self, path):
        parent = ""
        for name in _parts(path):
            full = posixpath.join(parent, name)
            if full not in self._dirs:
                entries = self.entries(parent)
                if name not in entries:
                    self.device.mkdir(parent, name)
                elif not entries[name]:
                    raise FileExistsError(full)
                self._dirs.add(full)
            parent = full

    def read(self, path):
        path = self.path(path)
        parent, name = posixpath.split(path)
        try:
            entry = self.device.entries(parent).get(name)
        except FileNotFoundError:
            return None
        if entry is None or entry[1] or entry[2] > 16 << 20:
            return None
        with tempfile.TemporaryDirectory(prefix="rpynx-mtp-") as tmp:
            local = os.path.join(tmp, "read")
            self.device.get(path, local, lambda fraction: None)
            with open(local, "rb") as f:
                return f.read(16 << 20)

    def put(self, local, path, tick=lambda fraction: None):
        path = self.path(path)
        self.mkdirs(posixpath.dirname(path))
        self.device.put(path, local, tick)

    def write(self, path, data):
        path = self.path(path)
        parent, name = posixpath.split(path)
        token = uuid.uuid4().hex
        staging = posixpath.join(parent, ".writing-" + token)
        backup = posixpath.join(parent, ".previous-" + token)
        with tempfile.TemporaryDirectory(prefix="rpynx-mtp-") as tmp:
            local = os.path.join(tmp, "write")
            with open(local, "wb") as f:
                f.write(data)
            self.put(local, staging)
        moved = name in self.entries(parent)
        if moved:
            self.rename(path, backup)
        try:
            self.rename(staging, path)
        except OSError:
            if moved:
                with contextlib.suppress(OSError):
                    self.rename(backup, path)
            raise
        if moved:
            self.rmtree(backup)

    def announce(self, size):
        pass  # USB reports progress as data streams arrive.

    def rename(self, old, new):
        self._dirs.clear()
        self.device.rename(self.path(old), self.path(new))

    def rmtree(self, path):
        self._dirs.clear()
        path = self.path(path)
        if not path:
            raise Refused(tr("无法删除 MTP 存储根目录。"))
        parent, name = posixpath.split(path)
        if self.entries(parent).get(name):
            for child in self.entries(path):
                self.rmtree(posixpath.join(path, child))
        with contextlib.suppress(FileNotFoundError):
            self.device.remove(path)

    def close(self):
        self.device.close()
