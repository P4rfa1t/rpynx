"""macOS and Linux MTP adapter using the system libmtp shared library."""
import contextlib
import ctypes as c
from ctypes.util import find_library
from functools import lru_cache
import os
import threading

from .common import Refused
from .i18n import language

U32 = c.c_uint32
PTR = c.c_void_p
STR = c.c_char_p
PROGRESS = c.CFUNCTYPE(c.c_int, c.c_uint64, c.c_uint64, PTR)
STORAGE = 0x00010001
ROOT = 0xFFFFFFFF
_LOCK = threading.RLock()


class Raw(c.Structure):
    _fields_ = [("vendor", STR), ("vendor_id", c.c_uint16), ("product", STR),
                ("product_id", c.c_uint16), ("flags", U32), ("bus", U32), ("device", c.c_uint8)]


class File(c.Structure):
    pass


FilePointer = c.POINTER(File)
File._fields_ = [("id", U32), ("parent", U32), ("storage", U32), ("name", STR),
                 ("size", c.c_uint64), ("modified", c.c_int64), ("kind", c.c_int), ("next", FilePointer)]


@lru_cache(maxsize=1)
def _library():
    for path in (find_library("mtp"), "/opt/homebrew/lib/libmtp.dylib", "/usr/local/lib/libmtp.dylib"):
        if path:
            try:
                lib = c.CDLL(path)
                break
            except OSError:
                continue
    else:
        message = {"zh-Hans": "USB MTP 需要 libmtp（macOS：brew install libmtp）。",
                   "zh-Hant": "USB MTP 需要 libmtp（macOS：brew install libmtp）。"}
        raise Refused(message.get(language(), "USB MTP requires libmtp (macOS: brew install libmtp)."))
    signatures = {
        "Init": (None, []), "Detect_Raw_Devices": (c.c_int, [c.POINTER(c.POINTER(Raw)), c.POINTER(c.c_int)]),
        "Open_Raw_Device_Uncached": (PTR, [c.POINTER(Raw)]), "Release_Device": (None, [PTR]),
        "Get_Modelname": (PTR, [PTR]), "FreeMemory": (None, [PTR]),
        "Get_Files_And_Folders": (FilePointer, [PTR, U32, U32]), "destroy_file_t": (None, [FilePointer]),
        "Get_Errorstack": (PTR, [PTR]), "Clear_Errorstack": (None, [PTR]),
        "Create_Folder": (U32, [PTR, STR, U32, U32]), "Delete_Object": (c.c_int, [PTR, U32]),
        "Set_Object_Filename": (c.c_int, [PTR, U32, STR]), "Move_Object": (c.c_int, [PTR, U32, U32, U32]),
        "Get_File_To_File": (c.c_int, [PTR, U32, STR, PROGRESS, PTR]),
        "Send_File_From_File": (c.c_int, [PTR, STR, FilePointer, PROGRESS, PTR]),
    }
    for name, (result, args) in signatures.items():
        function = getattr(lib, "LIBMTP_" + name)
        function.restype, function.argtypes = result, args
    lib.LIBMTP_Init()
    return lib


def _model(lib, handle):
    value = lib.LIBMTP_Get_Modelname(handle)
    if not value:
        return ""
    try:
        return c.string_at(value).decode("utf-8", "replace")
    finally:
        lib.LIBMTP_FreeMemory(value)


@contextlib.contextmanager
def _raw_devices(lib):
    raw, count = c.POINTER(Raw)(), c.c_int()
    result = lib.LIBMTP_Detect_Raw_Devices(c.byref(raw), c.byref(count))
    try:
        if result and count.value:
            raise OSError("libmtp device enumeration failed (%d)" % result)
        yield [(f"{raw[i].bus}-{raw[i].device}", raw[i]) for i in range(count.value)]
    finally:
        if raw:
            lib.LIBMTP_FreeMemory(raw)


def discover():
    with _LOCK:
        lib = _library()
        devices = []
        with _raw_devices(lib) as results:
            for key, raw in results:
                if (raw.vendor_id, raw.product_id) != (0x057E, 0x3000):
                    continue
                handle = lib.LIBMTP_Open_Raw_Device_Uncached(c.byref(raw))
                if not handle:
                    continue
                try:
                    if _model(lib, handle).casefold() == "rpynx":
                        devices.append((key, f"rpynx ({key})"))
                finally:
                    lib.LIBMTP_Release_Device(handle)
        return devices


class Device:
    def __init__(self, key):
        _LOCK.acquire()
        try:
            self.lib = _library()
            self.handle = None
            with _raw_devices(self.lib) as results:
                for candidate, raw in results:
                    if candidate == key and (raw.vendor_id, raw.product_id) == (0x057E, 0x3000):
                        self.handle = self.lib.LIBMTP_Open_Raw_Device_Uncached(c.byref(raw))
                        break
            if not self.handle or _model(self.lib, self.handle).casefold() != "rpynx":
                raise FileNotFoundError("rpynx USB device not found")
        except BaseException:
            if getattr(self, "handle", None):
                self.lib.LIBMTP_Release_Device(self.handle)
            _LOCK.release()
            raise

    def close(self):
        if self.handle:
            try:
                self.lib.LIBMTP_Release_Device(self.handle)
                self.handle = None
            finally:
                _LOCK.release()

    def _call(self, name, *args):
        return getattr(self.lib, "LIBMTP_" + name)(self.handle, *args)

    def _entries(self, parent):
        self._call("Clear_Errorstack")
        current = self._call("Get_Files_And_Folders", STORAGE, parent)
        if not current and self._call("Get_Errorstack"):
            raise OSError("libmtp could not list the folder")
        result = {}
        while current:
            item = current.contents
            following = c.cast(c.cast(item.next, PTR).value, FilePointer)
            if item.name:
                result[item.name.decode("utf-8", "replace")] = (item.id, item.kind == 0, item.size)
            self.lib.LIBMTP_destroy_file_t(current)
            current = following
        return result

    def _object(self, path):
        parent = (ROOT, True, 0)
        for name in path.split("/") if path else ():
            if not parent[1]:
                raise NotADirectoryError(path)
            item = self._entries(parent[0]).get(name)
            if item is None:
                raise FileNotFoundError(path)
            parent = item
        return parent

    def entries(self, path):
        object_id, directory, _ = self._object(path)
        if not directory:
            raise NotADirectoryError(path)
        return self._entries(object_id)

    def mkdir(self, parent, name):
        parent_id, directory, _ = self._object(parent)
        if not directory:
            raise NotADirectoryError(parent)
        if not self._call("Create_Folder", name.encode(), parent_id, STORAGE):
            raise OSError("libmtp could not create folder: " + name)

    def _transfer(self, method, args, tick):
        failures = []

        @PROGRESS
        def progress(done, total, user):
            try:
                tick(min(done / max(total, 1), 1.0))
                return 0
            except BaseException as error:
                failures.append(error)
                return 1

        result = self._call(method, *args, progress, None)
        if failures:
            raise failures[0]
        if result:
            raise OSError("libmtp transfer failed (%d)" % result)

    def get(self, remote, local, tick):
        self._transfer("Get_File_To_File", (self._object(remote)[0], os.fsencode(local)), tick)

    def put(self, remote, local, tick):
        parent, _, name = remote.rpartition("/")
        parent_id, folder, _ = self._object(parent)
        if not folder:
            raise NotADirectoryError(parent)
        if name in self._entries(parent_id):
            raise FileExistsError(remote)
        size = os.path.getsize(local)
        if size > ROOT - 12:
            raise OSError("File exceeds the USB MTP object size limit")
        meta = File(parent=parent_id, storage=STORAGE, name=name.encode(), size=size, kind=44)
        self._transfer("Send_File_From_File", (os.fsencode(local), c.byref(meta)), tick)

    def rename(self, old, new):
        parent, _, name = new.rpartition("/")
        old_parent, _, old_name = old.rpartition("/")
        if name in self.entries(parent):
            raise FileExistsError(new)
        object_id = self._object(old)[0]
        if parent != old_parent:
            if name != old_name:
                raise OSError("Cannot change name and parent in a single MTP move")
            result = self._call("Move_Object", object_id, STORAGE, self._object(parent)[0])
        else:
            result = self._call("Set_Object_Filename", object_id, name.encode())
        if result:
            raise OSError("libmtp could not move object: " + old)

    def remove(self, path):
        if self._call("Delete_Object", self._object(path)[0]):
            raise OSError("libmtp could not delete object: " + path)
