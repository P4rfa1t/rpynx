"""Adapter for the Windows Portable Devices bridge shipped with the importer."""

import ctypes
import os
from pathlib import Path
import sys
from functools import lru_cache

from .common import Refused
from .i18n import tr


U32 = ctypes.c_uint32
U64 = ctypes.c_uint64
I32 = ctypes.c_int32
STR = ctypes.c_char_p
PROGRESS = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, U64, U64)


class DeviceInfo(ctypes.Structure):
    _fields_ = [("friendly_name", ctypes.c_char * 128), ("model", ctypes.c_char * 128),
                ("device_id", ctypes.c_char * 512), ("index", U32), ("is_rpynx", U32)]


class Entry(ctypes.Structure):
    _fields_ = [("name", ctypes.c_char * 256), ("id", U32), ("size", U64),
                ("is_dir", U32), ("reserved", U32)]


@lru_cache(maxsize=1)
def _dll():
    filename = "rpynx_mtp.dll"
    source = Path(__file__).resolve().parents[1]
    roots = [source / "mtp_windows" / "build", source.parent / "build" / "mtp-windows" / "package"]
    if getattr(sys, "frozen", False):
        roots.insert(0, Path(sys._MEIPASS))
    for root in roots:
        try:
            dll = ctypes.CDLL(str(root / filename))
            break
        except OSError:
            continue
    else:
        raise Refused(tr("缺少 Windows MTP 适配器。请使用完整的导入器安装包。"))
    pointer = ctypes.POINTER
    functions = {
        "abi_version": [], "enumerate": [pointer(DeviceInfo), U32, pointer(U32)],
        "open": [U32, pointer(U64)], "close": [U64],
        "device_path": [U64, STR, U32],
        "list": [U64, STR, pointer(Entry), U32, pointer(U32)],
        "mkdir": [U64, STR, STR, pointer(U32)],
        "read_file_to": [U64, STR, STR, PROGRESS, ctypes.c_void_p, pointer(U64)],
        "put_file": [U64, STR, STR, PROGRESS, ctypes.c_void_p],
        "rename": [U64, STR, STR], "move": [U64, STR, STR], "delete": [U64, U32, I32],
        "last_error": [U64, STR, U32],
    }
    try:
        for name, args in functions.items():
            function = getattr(dll, "rpynx_mtp_" + name)
            function.argtypes, function.restype = args, I32
        if dll.rpynx_mtp_abi_version() == 1:
            return dll
    except AttributeError:
        pass
    raise Refused(tr("Windows MTP 适配器的版本与此导入器不兼容。"))


def _check(dll, handle, result):
    if result == 0:
        return
    if result == -3:
        raise FileNotFoundError(tr("找不到 MTP 对象或设备。"))
    if result == -5:
        raise FileExistsError(tr("MTP 目标已存在。"))
    message = ctypes.create_string_buffer(1024)
    dll.rpynx_mtp_last_error(handle, message, len(message))
    raise OSError(tr("Windows MTP 错误 %d：%s") % (result, message.value.decode("utf-8", "replace")))


def _items(dll, handle, function, structure, *args):
    count = U32()
    result = function(*args, None, 0, ctypes.byref(count))
    if result != -14:
        _check(dll, handle, result)
    if count.value > 1000000:
        raise OSError(tr("MTP 列表过大。"))
    if not count.value:
        return []
    items = (structure * count.value)()
    _check(dll, handle, function(*args, items, len(items), ctypes.byref(count)))
    return items[:count.value]


def _device_info(dll):
    return [entry for entry in _items(dll, 0, dll.rpynx_mtp_enumerate, DeviceInfo) if entry.is_rpynx]


def discover():
    return [(entry.device_id.decode("utf-8"), entry.friendly_name.decode("utf-8") or "rpynx")
            for entry in _device_info(_dll())]


class Device:
    def __init__(self, key):
        self.dll = _dll()
        self.handle = U64()
        found = next((entry for entry in _device_info(self.dll) if entry.device_id.decode("utf-8") == key), None)
        if found is None:
            raise FileNotFoundError(tr("找不到 rpynx USB 设备。"))
        _check(self.dll, 0, self.dll.rpynx_mtp_open(found.index, ctypes.byref(self.handle)))
        try:
            actual = ctypes.create_string_buffer(512)
            self._call("device_path", actual, len(actual))
            if actual.value.decode("utf-8") != key:
                raise OSError(tr("USB 设备已更换，请重新扫描。"))
        except BaseException:
            self.close()
            raise

    def _call(self, name, *args):
        result = getattr(self.dll, "rpynx_mtp_" + name)(self.handle, *args)
        _check(self.dll, self.handle, result)

    def entries(self, path):
        items = _items(self.dll, self.handle, self.dll.rpynx_mtp_list, Entry, self.handle, path.encode())
        return {item.name.decode("utf-8"): (item.id, bool(item.is_dir), item.size) for item in items}

    def mkdir(self, parent, name):
        self._call("mkdir", parent.encode(), name.encode(), ctypes.byref(U32()))

    def _transfer(self, method, remote, local, tick, *args):
        failure = []

        @PROGRESS
        def progress(_user, done, total):
            try:
                tick(min(done / max(total, 1), 1.0))
                return 0
            except BaseException as error:
                failure.append(error)
                return 1

        result = getattr(self.dll, "rpynx_mtp_" + method)(self.handle, remote.encode(),
                 os.path.abspath(local).encode("utf-8"), progress, None, *args)
        if failure:
            raise failure[0]
        _check(self.dll, self.handle, result)

    def get(self, remote, local, tick):
        self._transfer("read_file_to", remote, local, tick, ctypes.byref(U64()))

    def put(self, remote, local, tick):
        self._transfer("put_file", remote, local, tick)

    def rename(self, old, new):
        parent, _, name = new.rpartition("/")
        if old.rpartition("/")[0] == parent:
            self._call("rename", old.encode(), name.encode())
        else:
            self._call("move", old.encode(), new.encode())

    def remove(self, path):
        parent, _, name = path.rpartition("/")
        item = self.entries(parent).get(name)
        if item is None:
            raise FileNotFoundError(path)
        self._call("delete", item[0], 0)

    def close(self):
        if self.handle.value:
            self.dll.rpynx_mtp_close(self.handle)
            self.handle = U64()
