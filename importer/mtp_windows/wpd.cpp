#define NOMINMAX
#include <windows.h>
#include <initguid.h>
#include <portabledeviceapi.h>
#include <portabledevice.h>
#include <propvarutil.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

using Microsoft::WRL::ComPtr;
using U = uint32_t;
using Handle = uint64_t;
using Callback = int (*)(void *, uint64_t, uint64_t);
#define API extern "C" __declspec(dllexport) int32_t

struct DeviceInfo { char friendly[128], model[128], id[512]; U index, rpynx; };
struct Entry { char name[256]; U id; uint64_t size; U folder, reserved; };
struct Object { std::wstring id, name; uint64_t size; bool folder; };
struct Session {
    ComPtr<IPortableDevice> device;
    ComPtr<IPortableDeviceContent> content;
    ComPtr<IPortableDeviceProperties> properties;
    ComPtr<IPortableDeviceKeyCollection> keys;
    std::wstring id, root;
    std::vector<std::wstring> handles;
    std::unordered_map<std::wstring, std::vector<Object>> dir_cache;
    ~Session() { if (device) device->Close(); }
};
static thread_local char error_text[120];

static int error(HRESULT hr)
{
    snprintf(error_text, sizeof error_text, "HRESULT 0x%08lx", static_cast<unsigned long>(hr));
    return -10;
}

static std::wstring wide(const char *str)
{
    if (!str || !*str) return {};
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str, -1, nullptr, 0);
    if (!count) return {};
    std::wstring result(count, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str, -1, result.data(), count);
    result.pop_back();
    return result;
}

static bool utf8(const std::wstring &str, char *out, U cap)
{
    return WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1, out, cap, nullptr, nullptr) > 0;
}

static bool safe(const std::wstring &name)
{
    return !name.empty() && name.back() != L'.' && name.back() != L' ' &&
        std::none_of(name.begin(), name.end(), [](wchar_t c) {
            return c < 32 || std::wcschr(L"/\\:\"<>|?*", c) != nullptr;
        });
}

static std::wstring text(IPortableDeviceValues *values, REFPROPERTYKEY key)
{
    PWSTR raw = nullptr;
    if (FAILED(values->GetStringValue(key, &raw))) return {};
    std::wstring result(raw);
    CoTaskMemFree(raw);
    return result;
}

static HRESULT ids(std::vector<std::wstring> &result, ComPtr<IPortableDeviceManager> &manager)
{
    HRESULT hr = CoCreateInstance(CLSID_PortableDeviceManager, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&manager));
    if (FAILED(hr)) return hr;
    DWORD count = 0;
    hr = manager->GetDevices(nullptr, &count);
    if (FAILED(hr) || !count) return hr;
    std::vector<PWSTR> found(count, nullptr);
    hr = manager->GetDevices(found.data(), &count);
    if (SUCCEEDED(hr)) for (DWORD i = 0; i < count; ++i) if (found[i]) result.emplace_back(found[i]);
    for (PWSTR id : found) CoTaskMemFree(id);
    return hr;
}

static HRESULT children(Session &s, const std::wstring &parent, std::vector<Object> &result)
{
    ComPtr<IEnumPortableDeviceObjectIDs> iter;
    HRESULT hr = s.content->EnumObjects(0, parent.c_str(), nullptr, &iter);
    if (FAILED(hr)) return hr;
    for (;;) {
        PWSTR batch[64] = {};
        ULONG count = 0;
        hr = iter->Next(64, batch, &count);
        if (FAILED(hr) || !count) return hr;
        for (ULONG i = 0; i < count; ++i) {
            std::wstring id(batch[i]);
            CoTaskMemFree(batch[i]);
            ComPtr<IPortableDeviceValues> values;
            hr = s.properties->GetValues(id.c_str(), s.keys.Get(), &values);
            if (FAILED(hr)) {
                for (ULONG j = i + 1; j < count; ++j) CoTaskMemFree(batch[j]);
                return hr;
            }
            GUID kind = {};
            values->GetGuidValue(WPD_OBJECT_CONTENT_TYPE, &kind);
            auto name = text(values.Get(), WPD_OBJECT_ORIGINAL_FILE_NAME);
            if (name.empty()) name = text(values.Get(), WPD_OBJECT_NAME);
            ULONGLONG size = 0;
            values->GetUnsignedLargeIntegerValue(WPD_OBJECT_SIZE, &size);
            result.push_back({id, name, size, kind == WPD_CONTENT_TYPE_FOLDER});
        }
    }
}

static int cached_children(Session &s, const std::wstring &parent, const std::vector<Object> **out)
{
    auto it = s.dir_cache.find(parent);
    if (it != s.dir_cache.end()) {
        *out = &it->second;
        return 0;
    }
    std::vector<Object> objects;
    HRESULT hr = children(s, parent, objects);
    if (FAILED(hr)) return error(hr);
    *out = &s.dir_cache.emplace(parent, std::move(objects)).first->second;
    return 0;
}

static int resolve(Session &s, const std::wstring &path, Object &result)
{
    result = {s.root, L"", 0, true};
    for (size_t start = 0; start < path.size();) {
        size_t end = path.find(L'/', start);
        auto name = path.substr(start, end - start);
        if (!safe(name) || (end != std::wstring::npos && end + 1 == path.size())) return -13;
        if (!result.folder) return -4;
        const std::vector<Object> *objects = nullptr;
        int rc = cached_children(s, result.id, &objects);
        if (rc) return rc;
        auto found = std::find_if(objects->begin(), objects->end(), [&](const Object &o) { return o.name == name; });
        if (found == objects->end()) return -3;
        result = *found;
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    return 0;
}

static HRESULT open_device(Session &s, const std::wstring &id)
{
    ComPtr<IPortableDeviceValues> client;
    HRESULT hr = CoCreateInstance(CLSID_PortableDeviceValues, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&client));
    if (SUCCEEDED(hr)) hr = client->SetStringValue(WPD_CLIENT_NAME, L"rpynx Importer");
    if (SUCCEEDED(hr)) hr = CoCreateInstance(CLSID_PortableDeviceFTM, nullptr, CLSCTX_INPROC_SERVER,
                                              IID_PPV_ARGS(&s.device));
    if (SUCCEEDED(hr)) hr = s.device->Open(id.c_str(), client.Get());
    if (SUCCEEDED(hr)) hr = s.device->Content(&s.content);
    if (SUCCEEDED(hr)) hr = s.content->Properties(&s.properties);
    if (SUCCEEDED(hr)) hr = CoCreateInstance(CLSID_PortableDeviceKeyCollection, nullptr, CLSCTX_INPROC_SERVER,
                                              IID_PPV_ARGS(&s.keys));
    if (FAILED(hr)) return hr;
    s.id = id;
    const PROPERTYKEY keys[] = {WPD_OBJECT_CONTENT_TYPE, WPD_OBJECT_NAME, WPD_OBJECT_ORIGINAL_FILE_NAME,
                                WPD_OBJECT_SIZE, WPD_FUNCTIONAL_OBJECT_CATEGORY, WPD_DEVICE_MODEL};
    for (const auto &key : keys) if (FAILED(hr = s.keys->Add(key))) return hr;
    ComPtr<IPortableDeviceValues> values;
    hr = s.properties->GetValues(WPD_DEVICE_OBJECT_ID, s.keys.Get(), &values);
    if (FAILED(hr)) return hr;
    if (_wcsicmp(text(values.Get(), WPD_DEVICE_MODEL).c_str(), L"rpynx")) return E_ACCESSDENIED;
    std::vector<Object> objects;
    hr = children(s, WPD_DEVICE_OBJECT_ID, objects);
    if (FAILED(hr)) return hr;
    for (const auto &o : objects) {
        values.Reset();
        GUID category = {};
        hr = s.properties->GetValues(o.id.c_str(), s.keys.Get(), &values);
        if (SUCCEEDED(hr) && SUCCEEDED(values->GetGuidValue(WPD_FUNCTIONAL_OBJECT_CATEGORY, &category)) &&
            category == WPD_FUNCTIONAL_CATEGORY_STORAGE) {
            if (!s.root.empty()) return E_ACCESSDENIED;
            s.root = o.id;
        }
    }
    return s.root.empty() ? E_FAIL : S_OK;
}

static Session &session(Handle h) { return *reinterpret_cast<Session *>(h); }
API rpynx_mtp_abi_version() { return 1; }
API rpynx_mtp_last_error(Handle, char *out, U cap) { snprintf(out, cap, "%s", error_text); return 0; }

API rpynx_mtp_enumerate(DeviceInfo *out, U cap, U *count)
{
    HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) return error(init);
    HRESULT hr;
    {
        ComPtr<IPortableDeviceManager> manager;
        std::vector<std::wstring> devices;
        hr = ids(devices, manager);
        *count = 0;
        if (SUCCEEDED(hr)) for (U i = 0; i < devices.size(); ++i) {
            Session s;
            if (FAILED(open_device(s, devices[i]))) continue;
            DeviceInfo item = {};
            if (!utf8(devices[i], item.id, sizeof item.id)) continue;
            strcpy(item.friendly, "rpynx"); strcpy(item.model, "rpynx");
            item.index = i; item.rpynx = 1;
            if (*count < cap) out[*count] = item;
            ++*count;
        }
    }
    if (SUCCEEDED(init)) CoUninitialize();
    return FAILED(hr) ? error(hr) : *count > cap ? -14 : 0;
}

API rpynx_mtp_open(U index, Handle *handle)
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) return error(hr);
    std::vector<std::wstring> devices;
    auto s = std::make_unique<Session>();
    {
        ComPtr<IPortableDeviceManager> manager;
        hr = ids(devices, manager);
    }
    if (SUCCEEDED(hr) && index < devices.size()) hr = open_device(*s, devices[index]);
    else if (SUCCEEDED(hr)) hr = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    if (FAILED(hr)) {
        s.reset(); CoUninitialize(); return error(hr);
    }
    *handle = reinterpret_cast<Handle>(s.release());
    return 0;
}

API rpynx_mtp_close(Handle h) { delete &session(h); CoUninitialize(); return 0; }
API rpynx_mtp_device_path(Handle h, char *out, U cap) { return utf8(session(h).id, out, cap) ? 0 : -14; }

API rpynx_mtp_list(Handle h, const char *path, Entry *out, U cap, U *count)
{
    Session &s = session(h);
    Object parent;
    const std::vector<Object> *objects = nullptr;
    int rc = resolve(s, wide(path), parent);
    if (rc) return rc;
    if (!parent.folder) return -4;
    s.dir_cache.erase(parent.id);
    rc = cached_children(s, parent.id, &objects);
    if (rc) return rc;
    *count = 0;
    for (const auto &o : *objects) {
        Entry entry = {};
        if (!safe(o.name) || !utf8(o.name, entry.name, sizeof entry.name)) continue;
        if (*count < cap) {
            auto it = std::find(s.handles.begin(), s.handles.end(), o.id);
            if (it == s.handles.end()) {
                s.handles.push_back(o.id);
                entry.id = static_cast<U>(s.handles.size());
            } else entry.id = static_cast<U>(it - s.handles.begin()) + 1;
            entry.size = o.size; entry.folder = o.folder;
            out[*count] = entry;
        }
        ++*count;
    }
    return *count > cap ? -14 : 0;
}

static HRESULT metadata(ComPtr<IPortableDeviceValues> &values, const Object &parent,
                        const std::wstring &name, bool directory, uint64_t size)
{
    HRESULT hr = CoCreateInstance(CLSID_PortableDeviceValues, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&values));
    if (SUCCEEDED(hr)) hr = values->SetStringValue(WPD_OBJECT_PARENT_ID, parent.id.c_str());
    if (SUCCEEDED(hr)) hr = values->SetStringValue(WPD_OBJECT_NAME, name.c_str());
    if (SUCCEEDED(hr)) hr = values->SetStringValue(WPD_OBJECT_ORIGINAL_FILE_NAME, name.c_str());
    if (SUCCEEDED(hr)) hr = values->SetGuidValue(WPD_OBJECT_CONTENT_TYPE,
                                                directory ? WPD_CONTENT_TYPE_FOLDER : WPD_CONTENT_TYPE_GENERIC_FILE);
    if (SUCCEEDED(hr)) hr = values->SetGuidValue(WPD_OBJECT_FORMAT,
                                                directory ? WPD_OBJECT_FORMAT_PROPERTIES_ONLY : WPD_OBJECT_FORMAT_UNSPECIFIED);
    if (SUCCEEDED(hr) && !directory) hr = values->SetUnsignedLargeIntegerValue(WPD_OBJECT_SIZE, size);
    return hr;
}

API rpynx_mtp_mkdir(Handle h, const char *parent_path, const char *name, U *id)
{
    Session &s = session(h);
    Object parent;
    int rc = resolve(s, wide(parent_path), parent);
    if (rc) return rc;
    if (!parent.folder || !safe(wide(name))) return -13;
    ComPtr<IPortableDeviceValues> values;
    HRESULT hr = metadata(values, parent, wide(name), true, 0);
    PWSTR created = nullptr;
    if (SUCCEEDED(hr)) hr = s.content->CreateObjectWithPropertiesOnly(values.Get(), &created);
    if (SUCCEEDED(hr) && created) {
        std::wstring created_id(created);
        s.handles.emplace_back(created_id);
        *id = static_cast<U>(s.handles.size());
        auto it = s.dir_cache.find(parent.id);
        if (it != s.dir_cache.end()) it->second.push_back({created_id, wide(name), 0, true});
        s.dir_cache.emplace(created_id, std::vector<Object>{});
    }
    CoTaskMemFree(created);
    return FAILED(hr) ? error(hr) : 0;
}

API rpynx_mtp_read_file_to(Handle h, const char *path, const char *local, Callback cb, void *user, uint64_t *bytes)
{
    Session &s = session(h);
    Object object;
    int rc = resolve(s, wide(path), object);
    if (rc) return rc;
    if (object.folder) return -1;
    ComPtr<IPortableDeviceResources> resources;
    ComPtr<IStream> stream;
    DWORD optimal = 0;
    HRESULT hr = s.content->Transfer(&resources);
    if (SUCCEEDED(hr)) hr = resources->GetStream(object.id.c_str(), WPD_RESOURCE_DEFAULT, STGM_READ, &optimal, &stream);
    if (FAILED(hr)) return error(hr);
    auto filename = wide(local);
    FILE *file = _wfopen(filename.c_str(), L"wb");
    if (!file) return -10;
    std::vector<unsigned char> buffer(std::min<size_t>(std::max<size_t>(optimal, 1024 * 1024), 4 * 1024 * 1024));
    *bytes = 0;
    ULONG got = 0;
    while (SUCCEEDED(hr = stream->Read(buffer.data(), static_cast<ULONG>(buffer.size()), &got)) && got) {
        if (fwrite(buffer.data(), 1, got, file) != got) { rc = -10; break; }
        *bytes += got;
        if (cb && cb(user, *bytes, object.size)) { rc = -8; break; }
    }
    if (fclose(file) || *bytes != object.size) { if (!rc) rc = -10; }
    if (FAILED(hr)) rc = error(hr);
    if (rc) DeleteFileW(filename.c_str());
    return rc;
}

static std::wstring split(std::wstring &path)
{
    size_t slash = path.rfind(L'/');
    if (slash == std::wstring::npos) return {};
    auto parent = path.substr(0, slash);
    path.erase(0, slash + 1);
    return parent;
}

API rpynx_mtp_put_file(Handle h, const char *path, const char *local, Callback cb, void *user)
{
    Session &s = session(h);
    Object object, parent;
    int rc = resolve(s, wide(path), object);
    if (rc != -3) return rc ? rc : -5;
    auto name = wide(path);
    rc = resolve(s, split(name), parent);
    if (rc) return rc;
    if (!safe(name) || !parent.folder) return -13;
    std::unique_ptr<FILE, decltype(&fclose)> file(_wfopen(wide(local).c_str(), L"rb"), fclose);
    if (!file || _fseeki64(file.get(), 0, SEEK_END)) return -10;
    const auto size = _ftelli64(file.get());
    if (size < 0 || static_cast<uint64_t>(size) > UINT32_MAX - 12u || _fseeki64(file.get(), 0, SEEK_SET)) return -10;
    ComPtr<IPortableDeviceValues> values;
    HRESULT hr = metadata(values, parent, name, false, size);
    ComPtr<IStream> stream;
    DWORD optimal = 0;
    if (SUCCEEDED(hr)) hr = s.content->CreateObjectWithPropertiesAndData(values.Get(), &stream, &optimal, nullptr);
    if (FAILED(hr)) return error(hr);
    std::vector<unsigned char> buffer(std::min<size_t>(std::max<size_t>(optimal, 1024 * 1024), 4 * 1024 * 1024));
    uint64_t sent = 0;
    if (cb && cb(user, 0, size)) { stream->Revert(); return -8; }
    while (sent < static_cast<uint64_t>(size)) {
        size_t want = static_cast<size_t>(std::min<uint64_t>(buffer.size(), size - sent));
        if (fread(buffer.data(), 1, want, file.get()) != want) { stream->Revert(); return -10; }
        ULONG written = 0;
        hr = stream->Write(buffer.data(), static_cast<ULONG>(want), &written);
        if (FAILED(hr) || written != want) { stream->Revert(); return FAILED(hr) ? error(hr) : -10; }
        sent += written;
        if (cb && cb(user, sent, size)) { stream->Revert(); return -8; }
    }
    hr = stream->Commit(STGC_DEFAULT);
    if (SUCCEEDED(hr)) {
        auto it = s.dir_cache.find(parent.id);
        if (it != s.dir_cache.end()) it->second.push_back({L"", name, static_cast<uint64_t>(size), false});
    }
    return FAILED(hr) ? error(hr) : 0;
}

static HRESULT id_collection(const std::wstring &id, ComPtr<IPortableDevicePropVariantCollection> &items)
{
    HRESULT hr = CoCreateInstance(CLSID_PortableDevicePropVariantCollection, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&items));
    if (FAILED(hr)) return hr;
    PROPVARIANT value;
    PropVariantInit(&value);
    hr = InitPropVariantFromString(id.c_str(), &value);
    if (SUCCEEDED(hr)) hr = items->Add(&value);
    PropVariantClear(&value);
    return hr;
}

static HRESULT object_result(IPortableDevicePropVariantCollection *result)
{
    if (!result) return S_OK;
    PROPVARIANT value;
    PropVariantInit(&value);
    HRESULT hr = result->GetAt(0, &value);
    if (SUCCEEDED(hr) && value.vt == VT_ERROR) hr = value.scode;
    PropVariantClear(&value);
    return hr;
}

API rpynx_mtp_delete(Handle h, U id, int32_t recursive)
{
    Session &s = session(h);
    if (!id || id > s.handles.size()) return -3;
    ComPtr<IPortableDevicePropVariantCollection> items, result;
    HRESULT hr = id_collection(s.handles[id - 1], items);
    if (SUCCEEDED(hr)) hr = s.content->Delete(recursive ? PORTABLE_DEVICE_DELETE_WITH_RECURSION :
                                               PORTABLE_DEVICE_DELETE_NO_RECURSION, items.Get(), &result);
    if (SUCCEEDED(hr)) hr = object_result(result.Get());
    s.dir_cache.clear();
    return FAILED(hr) ? error(hr) : 0;
}

API rpynx_mtp_rename(Handle h, const char *path, const char *new_name)
{
    Session &s = session(h);
    Object object;
    int rc = resolve(s, wide(path), object);
    if (rc) return rc;
    auto name = wide(new_name);
    if (!*path || !safe(name)) return -13;
    ComPtr<IPortableDeviceValues> values, result;
    HRESULT hr = CoCreateInstance(CLSID_PortableDeviceValues, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&values));
    if (SUCCEEDED(hr)) hr = values->SetStringValue(WPD_OBJECT_ORIGINAL_FILE_NAME, name.c_str());
    if (SUCCEEDED(hr)) hr = s.properties->SetValues(object.id.c_str(), values.Get(), &result);
    SCODE property = E_FAIL;
    if (SUCCEEDED(hr)) hr = result->GetErrorValue(WPD_OBJECT_ORIGINAL_FILE_NAME, &property);
    if (FAILED(property) && SUCCEEDED(hr)) hr = property;
    s.dir_cache.clear();
    return FAILED(hr) ? error(hr) : 0;
}

API rpynx_mtp_move(Handle h, const char *old_path, const char *new_path)
{
    Session &s = session(h);
    Object object, parent;
    int rc = resolve(s, wide(old_path), object);
    if (rc) return rc;
    auto name = wide(new_path);
    auto parent_path = split(name);
    if (!*old_path || name != object.name) return -13;
    rc = resolve(s, parent_path, parent);
    if (rc || !parent.folder) return rc ? rc : -4;
    ComPtr<IPortableDevicePropVariantCollection> items, result;
    HRESULT hr = id_collection(object.id, items);
    if (SUCCEEDED(hr)) hr = s.content->Move(items.Get(), parent.id.c_str(), &result);
    if (SUCCEEDED(hr)) hr = object_result(result.Get());
    s.dir_cache.clear();
    return FAILED(hr) ? error(hr) : 0;
}
