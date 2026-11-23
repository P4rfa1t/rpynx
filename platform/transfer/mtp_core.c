#define _POSIX_C_SOURCE 200809L
#include "mtp_core.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#define STORAGE_ID 0x00010001u
#define ROOT_HANDLE UINT32_MAX
#define CHUNK (1024 * 1024)
#define OBJECT_LIMIT 200000
#define PATH_LIMIT 1024
#define FILE_FORMAT 0x3000
#define FOLDER_FORMAT 0x3001

struct object {
    char *path;
    uint32_t parent;
    bool directory;
};

struct MtpCore {
    MtpRead read;
    MtpWrite write;
    MtpProgress progress;
    void *context;
    char *root;
    struct object *objects;
    size_t used, capacity;
    uint32_t session, transaction;
    uint16_t operation;
    uint32_t pending;
    uint32_t size;
    bool failed;
    unsigned char data[CHUNK];
};

struct packet {
    unsigned char *bytes;
    size_t size, capacity;
    bool failed;
};

static uint16_t get16(const unsigned char *p)
{
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}

static uint32_t get32(const unsigned char *p)
{
    return (uint32_t)get16(p) | (uint32_t)get16(p + 2) << 16;
}

static void raw(struct packet *p, const void *value, size_t size)
{
    if (size > p->capacity - p->size) {
        p->failed = true;
        return;
    }
    memcpy(p->bytes + p->size, value, size);
    p->size += size;
}

static void word(struct packet *p, uint16_t value)
{
    unsigned char bytes[] = { value & 255, value >> 8 };
    raw(p, bytes, sizeof bytes);
}

static void integer(struct packet *p, uint32_t value)
{
    word(p, value & 65535);
    word(p, value >> 16);
}

static void wide(struct packet *p, uint64_t value)
{
    integer(p, (uint32_t)value);
    integer(p, (uint32_t)(value >> 32));
}

static uint32_t utf8_next(const unsigned char **text)
{
    uint32_t code = *(*text)++;
    if (code < 0x80) return code;
    int extra = code >= 0xf0 ? 3 : code >= 0xe0 ? 2 : code >= 0xc2 ? 1 : 0;
    if (!extra || code > 0xf4) return 0xfffd;
    code &= 0x7f >> (extra + 1);
    for (int i = 0; i < extra; ++i) {
        if ((**text & 0xc0) != 0x80) return 0xfffd;
        code = code << 6 | (*(*text)++ & 63);
    }
    if (code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return 0xfffd;
    return code;
}

static void string(struct packet *p, const char *text)
{
    const unsigned char *it = (const unsigned char *)text;
    uint16_t chars[255];
    unsigned char size = 0;
    while (*it && size < 253) {
        uint32_t code = utf8_next(&it);
        if (code > 0xffff) {
            code -= 0x10000;
            chars[size++] = 0xd800 | (code >> 10);
            chars[size++] = 0xdc00 | (code & 1023);
        } else {
            chars[size++] = (uint16_t)code;
        }
    }
    if (size) {
        chars[size++] = 0;
        raw(p, &size, 1);
        for (unsigned i = 0; i < size; ++i) word(p, chars[i]);
    } else {
        raw(p, &size, 1);
    }
}

static bool decode_name(const unsigned char *data, size_t size, char *out)
{
    if (size < 3 || data[0] < 2 || (size_t)data[0] * 2 + 1 > size) return false;
    unsigned count = data[0] - 1;
    if (get16(data + 1 + count * 2)) return false;
    size_t n = 0;
    for (unsigned i = 0; i < count; ++i) {
        uint32_t cp = get16(data + 1 + i * 2);
        if (cp < 32 || (cp < 128 && strchr("/\\:\"<>|?*", (int)cp))) return false;
        if (cp >= 0xd800 && cp <= 0xdbff) {
            if (++i >= count) return false;
            uint16_t low = get16(data + 1 + i * 2);
            if (low < 0xdc00 || low > 0xdfff) return false;
            cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
        } else if (cp >= 0xdc00 && cp <= 0xdfff) {
            return false;
        }
        if (n > 251) return false;
        if (cp <= 127) out[n++] = (char)cp;
        else if (cp <= 0x7ff) {
            out[n++] = (char)(0xc0 | (cp >> 6));
            out[n++] = (char)(0x80 | (cp & 63));
        } else if (cp <= 0xffff) {
            out[n++] = (char)(0xe0 | (cp >> 12));
            out[n++] = (char)(0x80 | (cp >> 6 & 63));
            out[n++] = (char)(0x80 | (cp & 63));
        } else {
            out[n++] = (char)(0xf0 | (cp >> 18));
            out[n++] = (char)(0x80 | (cp >> 12 & 63));
            out[n++] = (char)(0x80 | (cp >> 6 & 63));
            out[n++] = (char)(0x80 | (cp & 63));
        }
    }
    out[n] = 0;
    return n && out[n - 1] != '.' && out[n - 1] != ' ' && strncmp(out, ".rsl-mtp-", 9) != 0;
}

static bool send_bytes(MtpCore *c, const void *p, size_t n)
{
    if (c->failed || !c->write(c->context, p, n)) {
        c->failed = true;
        return false;
    }
    return true;
}

static bool receive(MtpCore *c, void *p, size_t n)
{
    if (!c->read(c->context, p, n)) {
        c->failed = true;
        return false;
    }
    return true;
}

static bool header(MtpCore *c, uint16_t type, uint16_t code, uint32_t length)
{
    unsigned char bytes[12];
    struct packet p = { bytes, 0, sizeof bytes, false };
    integer(&p, length + 12);
    word(&p, type);
    word(&p, code);
    integer(&p, c->transaction);
    return send_bytes(c, bytes, sizeof bytes);
}

static bool reply(MtpCore *c, uint16_t code, const uint32_t *values, size_t count)
{
    unsigned char bytes[32];
    struct packet p = { bytes, 0, sizeof bytes, false };
    integer(&p, (uint32_t)(12 + 4 * count));
    word(&p, 3);
    word(&p, code);
    integer(&p, c->transaction);
    for (size_t i = 0; i < count; ++i) integer(&p, values[i]);
    return send_bytes(c, bytes, p.size);
}

static uint16_t dataset(MtpCore *c, struct packet *p)
{
    if (p->failed) return 0x2002;
    if (header(c, 2, c->operation, (uint32_t)p->size) && p->size) send_bytes(c, p->bytes, p->size);
    return 0x2001;
}

static bool receive_header(MtpCore *c, uint32_t *left)
{
    unsigned char bytes[12];
    if (!receive(c, bytes, sizeof bytes)) return false;
    uint32_t length = get32(bytes);
    if (length < 12 || get16(bytes + 4) != 2 || get16(bytes + 6) != c->operation ||
        get32(bytes + 8) != c->transaction) {
        c->failed = true;
        return false;
    }
    *left = length - 12;
    return true;
}

static bool receive_data(MtpCore *c, uint32_t *length)
{
    if (!receive_header(c, length)) return false;
    if (*length > sizeof c->data) {
        c->failed = true;
        return false;
    }
    return receive(c, c->data, *length);
}

static struct object *object(MtpCore *c, uint32_t id)
{
    if (!id || id > c->used || !c->objects[id - 1].path) return NULL;
    return &c->objects[id - 1];
}

static const char *name_of(const char *path)
{
    const char *name = strrchr(path, '/');
    return name ? name + 1 : path;
}

static const char *parent_of(MtpCore *c, uint32_t id)
{
    if (!id || id == ROOT_HANDLE) return c->root;
    struct object *entry = object(c, id);
    return entry && entry->directory ? entry->path : NULL;
}

static bool joined(char *out, const char *parent, const char *name)
{
    return parent && snprintf(out, PATH_LIMIT, "%s/%s", parent, name) < PATH_LIMIT;
}

static uint32_t remember(MtpCore *c, const char *path, uint32_t parent, bool dir)
{
    for (size_t i = 0; i < c->used; ++i) {
        if (c->objects[i].path && strcmp(c->objects[i].path, path) == 0)
            return (uint32_t)i + 1;
    }
    if (c->used == OBJECT_LIMIT) return 0;
    if (c->used == c->capacity) {
        size_t capacity = c->capacity ? c->capacity * 2 : 64;
        struct object *objects = realloc(c->objects, capacity * sizeof *objects);
        if (!objects) return 0;
        c->objects = objects;
        c->capacity = capacity;
    }
    char *copy = strdup(path);
    if (!copy) return 0;
    c->objects[c->used++] = (struct object){ copy, parent, dir };
    return (uint32_t)c->used;
}

static bool exists(const char *path, struct stat *st)
{
    return lstat(path, st) == 0 && (S_ISREG(st->st_mode) || S_ISDIR(st->st_mode));
}

static void forget_tree(MtpCore *c, const char *path)
{
    char prefix[PATH_LIMIT];
    size_t len = strlen(path);
    if (len >= sizeof prefix) return;
    memcpy(prefix, path, len + 1);
    for (size_t i = 0; i < c->used; ++i) {
        char *entry = c->objects[i].path;
        if (entry && strncmp(prefix, entry, len) == 0 && (entry[len] == 0 || entry[len] == '/')) {
            free(entry);
            c->objects[i].path = NULL;
        }
    }
}

static void reset(MtpCore *c)
{
    for (size_t i = 0; i < c->used; ++i) free(c->objects[i].path);
    c->used = 0;
    c->session = c->pending = 0;
}

static bool scan(MtpCore *c, uint32_t parent, bool recurse, unsigned depth,
                 uint16_t format, struct packet *p, uint32_t *count)
{
    const char *base = parent_of(c, parent);
    if (!base || depth > 64) return false;
    DIR *dir = opendir(base);
    if (!dir) return false;
    bool ok = true;
    struct dirent *entry;
    while (ok && !p->failed && (entry = readdir(dir))) {
        const char *name = entry->d_name;
        if (!strcmp(name, ".") || !strcmp(name, "..") || !strncmp(name, ".rsl-mtp-", 9)) continue;
        char path[PATH_LIMIT];
        struct stat st;
        if (!joined(path, base, name) || !exists(path, &st)) continue;
        bool directory = S_ISDIR(st.st_mode);
        uint32_t id = remember(c, path, parent, directory);
        if (!id) {
            ok = false;
        } else {
            if (!format || format == (directory ? FOLDER_FORMAT : FILE_FORMAT)) {
                ++*count;
                integer(p, id);
            }
            if (directory && recurse) ok = scan(c, id, true, depth + 1, format, p, count);
        }
    }
    closedir(dir);
    return ok;
}

static void codes(struct packet *p, const uint16_t *items, size_t count)
{
    integer(p, (uint32_t)count);
    for (size_t i = 0; i < count; ++i) word(p, items[i]);
}

static const uint16_t properties[] = { 0xdc01, 0xdc02, 0xdc03, 0xdc04, 0xdc07, 0xdc0b, 0xdc41, 0xdc44 };

static uint16_t property_type(uint16_t prop)
{
    switch (prop) {
    case 0xdc01: case 0xdc0b: return 6;
    case 0xdc02: case 0xdc03: return 4;
    case 0xdc04: return 8;
    case 0xdc07: case 0xdc44: return 0xffff;
    case 0xdc41: return 10;
    default: return 0;
    }
}

static void property_value(struct packet *p, uint16_t prop, const struct object *entry,
                           uint32_t id, uint64_t size)
{
    switch (prop) {
    case 0xdc01: integer(p, STORAGE_ID); break;
    case 0xdc02: word(p, entry ? entry->directory ? FOLDER_FORMAT : FILE_FORMAT : 0); break;
    case 0xdc03: word(p, 0); break;
    case 0xdc04: wide(p, size); break;
    case 0xdc07: case 0xdc44: string(p, entry ? name_of(entry->path) : ""); break;
    case 0xdc0b: integer(p, entry && entry->parent != ROOT_HANDLE ? entry->parent : 0); break;
    case 0xdc41: wide(p, id); wide(p, STORAGE_ID); break;
    }
}

static uint16_t device_info(MtpCore *c, struct packet *p)
{
    static const uint16_t ops[] = { 0x1001, 0x1002, 0x1003, 0x1004, 0x1005, 0x1006, 0x1007, 0x1008,
        0x1009, 0x100b, 0x100c, 0x100d, 0x1019, 0x9801, 0x9802, 0x9803, 0x9804, 0x9805 };
    static const uint16_t formats[] = { FILE_FORMAT, FOLDER_FORMAT };
    word(p, 100);
    integer(p, 6);
    word(p, 100);
    string(p, "microsoft.com: 1.0;");
    word(p, 0);
    codes(p, ops, sizeof ops / sizeof ops[0]);
    integer(p, 0);
    integer(p, 0);
    integer(p, 0);
    codes(p, formats, sizeof formats / sizeof formats[0]);
    string(p, "rpynx");
    string(p, "rpynx");
    string(p, "1.0");
    string(p, "rpynx-1");
    return dataset(c, p);
}

static uint16_t object_info(MtpCore *c, struct packet *p, struct object *entry, const struct stat *st)
{
    integer(p, STORAGE_ID);
    word(p, entry->directory ? FOLDER_FORMAT : FILE_FORMAT);
    word(p, 0);
    integer(p, entry->directory ? 0 : st->st_size >= UINT32_MAX ? UINT32_MAX : (uint32_t)st->st_size);
    word(p, 0);
    for (int i = 0; i < 6; ++i) integer(p, 0);
    integer(p, entry->parent == ROOT_HANDLE ? 0 : entry->parent);
    word(p, entry->directory ? 1 : 0);
    integer(p, 0);
    integer(p, 0);
    string(p, name_of(entry->path));
    string(p, ""); string(p, ""); string(p, "");
    return dataset(c, p);
}

static uint16_t download(MtpCore *c, const char *path, uint64_t size)
{
    if (size > UINT32_MAX - 12u) return 0x200a;
    FILE *file = fopen(path, "rb");
    if (!file) return 0x2002;
    setvbuf(file, NULL, _IOFBF, CHUNK);
    uint64_t left = size;
    if (header(c, 2, c->operation, (uint32_t)size)) {
        while (left) {
            size_t count = left > sizeof c->data ? sizeof c->data : (size_t)left;
            if (fread(c->data, 1, count, file) != count) {
                c->failed = true;
                break;
            }
            if (!send_bytes(c, c->data, count)) break;
            left -= count;
        }
    }
    fclose(file);
    return 0x2001;
}

static uint16_t prepare_upload(MtpCore *c, uint32_t parent, uint32_t *params, size_t *n)
{
    uint32_t size;
    char name[256], path[PATH_LIMIT];
    c->pending = 0;
    if (!receive_data(c, &size) || size < 55 || !decode_name(c->data + 52, size - 52, name)) return 0x201d;
    if (parent == 0) parent = ROOT_HANDLE;
    if (!joined(path, parent_of(c, parent), name)) return 0x201a;
    struct stat st;
    if (lstat(path, &st) == 0) return 0x200f;
    bool dir = get16(c->data + 4) == FOLDER_FORMAT;
    uint32_t id = remember(c, path, parent, dir);
    if (!id) return 0x2002;
    if (dir && mkdir(path, 0777) < 0) {
        forget_tree(c, path);
        return errno == ENOSPC ? 0x200c : 0x2002;
    }
    c->size = get32(c->data + 8);
    c->pending = dir ? 0 : id;
    params[0] = STORAGE_ID;
    params[1] = parent;
    params[2] = id;
    *n = 3;
    return 0x2001;
}

static uint16_t upload(MtpCore *c)
{
    uint32_t left;
    struct object *entry = object(c, c->pending);
    c->pending = 0;
    if (!receive_header(c, &left)) return 0x2002;
    uint32_t total = left;
    char temporary[PATH_LIMIT];
    int fd = -1;
    if (entry && total == c->size) {
        const char *parent = parent_of(c, entry->parent);
        if (joined(temporary, parent, ".rsl-mtp-XXXXXX")) fd = mkstemp(temporary);
    }
    FILE *file = fd < 0 ? NULL : fdopen(fd, "wb");
    bool ok = file != NULL;
    if (file) setvbuf(file, NULL, _IOFBF, CHUNK);
    if (!file && fd >= 0) close(fd);
    if (ok && c->progress) c->progress(c->context, entry->path, 0, total, false);
    while (left) {
        size_t count = left > sizeof c->data ? sizeof c->data : left;
        if (!receive(c, c->data, count)) {
            ok = false;
            break;
        }
        if (ok && fwrite(c->data, 1, count, file) != count) ok = false;
        left -= (uint32_t)count;
        if (ok && c->progress) c->progress(c->context, entry->path, total - left, total, false);
    }
    if (file && fclose(file)) ok = false;
    struct stat st;
    if (ok && (lstat(entry->path, &st) == 0 || rename(temporary, entry->path) != 0)) ok = false;
    if (!ok && fd >= 0) unlink(temporary);
    if (c->progress) c->progress(c->context, entry ? entry->path : "", ok ? total : 0, total, true);
    if (!ok && entry) forget_tree(c, entry->path);
    return ok ? 0x2001 : 0x2007;
}

static uint16_t move_object(MtpCore *c, uint32_t id, struct object *entry, uint32_t parent, const char *name)
{
    const char *base = parent_of(c, parent);
    char target[PATH_LIMIT];
    struct stat st;
    if (!joined(target, base, name)) return 0x201a;
    if (!strcmp(target, entry->path)) return 0x2001;
    size_t len = strlen(entry->path);
    if (entry->directory && !strncmp(entry->path, target, len) && target[len] == '/') return 0x201d;
    if (lstat(target, &st) == 0) return 0x200f;
    char *next = strdup(target);
    if (!next || rename(entry->path, target)) {
        free(next);
        return 0x2002;
    }
    char *old = entry->path;
    entry->path = next;
    entry->parent = parent;
    for (size_t i = 0; i < c->used; ++i) {
        struct object *child = &c->objects[i];
        if (!child->path || i + 1 == id || strncmp(old, child->path, len) || child->path[len] != '/') continue;
        size_t total = strlen(target) + strlen(child->path + len) + 1;
        char *updated = malloc(total);
        if (updated) snprintf(updated, total, "%s%s", target, child->path + len);
        free(child->path);
        child->path = updated;
    }
    free(old);
    return 0x2001;
}

static uint16_t handle(MtpCore *c, uint32_t *args, uint32_t *response, size_t *count)
{
    struct packet p = { c->data, 0, sizeof c->data, false };
    if (c->operation == 0x1001) return device_info(c, &p);
    if (c->operation == 0x1002) {
        if (!args[0]) return 0x201d;
        if (c->session) return 0x201e;
        c->session = args[0];
        return 0x2001;
    }
    if (!c->session) return 0x2003;
    struct object *entry = object(c, args[0]);
    struct stat st;
    bool present = entry && exists(entry->path, &st);
    uint16_t prop = (uint16_t)args[1];
    switch (c->operation) {
    case 0x1003: reset(c); return 0x2001;
    case 0x1004: integer(&p, 1); integer(&p, STORAGE_ID); break;
    case 0x1005: {
        if (args[0] != STORAGE_ID) return 0x2008;
        struct statvfs volume = { 0 };
        statvfs(c->root, &volume);
        word(&p, 4); word(&p, 2); word(&p, 0);
        wide(&p, (uint64_t)volume.f_blocks * volume.f_frsize);
        wide(&p, (uint64_t)volume.f_bavail * volume.f_frsize);
        integer(&p, UINT32_MAX);
        string(&p, "rpynx"); string(&p, "rpynx");
        break;
    }
    case 0x1006: case 0x1007: {
        if (args[0] != STORAGE_ID && args[0] != UINT32_MAX && args[0] != 0) return 0x2008;
        uint32_t n = 0;
        integer(&p, 0);
        bool all = !args[2];
        if (!scan(c, all ? ROOT_HANDLE : args[2], all, 0, prop, &p, &n)) return 0x2009;
        if (p.failed) return 0x2002;
        struct packet prefix = { p.bytes, 0, 4, false };
        integer(&prefix, n);
        if (c->operation == 0x1006) {
            response[0] = n;
            *count = 1;
            return 0x2001;
        }
        break;
    }
    case 0x1008: return present ? object_info(c, &p, entry, &st) : 0x2009;
    case 0x1009: return present && !entry->directory ? download(c, entry->path, (uint64_t)st.st_size) : 0x2009;
    case 0x100b: {
        if (!present) return 0x2009;
        int rc = entry->directory ? rmdir(entry->path) : unlink(entry->path);
        if (rc != 0) return 0x200f;
        forget_tree(c, entry->path);
        return 0x2001;
    }
    case 0x100c: return prepare_upload(c, args[1], response, count);
    case 0x100d: return upload(c);
    case 0x1019:
        if (!present) return 0x2009;
        if (args[1] != STORAGE_ID && args[1] != 0) return 0x2008;
        return move_object(c, args[0], entry, args[2] ? args[2] : ROOT_HANDLE, name_of(entry->path));
    case 0x9801:
        codes(&p, properties, sizeof properties / sizeof properties[0]);
        break;
    case 0x9802: {
        uint16_t type = property_type((uint16_t)args[0]);
        if (!type) return 0xa801;
        word(&p, (uint16_t)args[0]); word(&p, type);
        const unsigned char writable = args[0] == 0xdc07 || args[0] == 0xdc44;
        raw(&p, &writable, 1);
        property_value(&p, (uint16_t)args[0], NULL, 0, 0);
        integer(&p, 0);
        const unsigned char form = 0;
        raw(&p, &form, 1);
        break;
    }
    case 0x9803:
        if (!present) return 0x2009;
        if (!property_type(prop)) return 0xa801;
        property_value(&p, prop, entry, args[0], entry->directory ? 0 : (uint64_t)st.st_size);
        break;
    case 0x9804: {
        uint32_t length;
        char name[256];
        if (!receive_data(c, &length)) return 0x2002;
        if (!present) return 0x2009;
        if (prop != 0xdc07 && prop != 0xdc44) return 0xa801;
        if (!decode_name(c->data, length, name)) return 0x201d;
        return move_object(c, args[0], entry, entry->parent, name);
    }
    case 0x9805: {
        if (!present) return 0x2009;
        uint32_t requested = args[2];
        if (requested != UINT32_MAX && !property_type((uint16_t)requested)) return 0xa801;
        integer(&p, requested == UINT32_MAX ? sizeof properties / sizeof properties[0] : 1);
        for (size_t i = 0; i < sizeof properties / sizeof properties[0]; ++i) {
            uint16_t code = properties[i];
            if (requested != UINT32_MAX && requested != code) continue;
            integer(&p, args[0]); word(&p, code); word(&p, property_type(code));
            property_value(&p, code, entry, args[0], entry->directory ? 0 : (uint64_t)st.st_size);
        }
        break;
    }
    default: return 0x2005;
    }
    return dataset(c, &p);
}

MtpCore *mtp_core_new(const char *root, MtpRead read, MtpWrite write, MtpProgress progress, void *context)
{
    MtpCore *c = calloc(1, sizeof *c);
    if (!c) return NULL;
    c->root = strdup(root);
    if (!c->root) {
        free(c);
        return NULL;
    }
    c->read = read;
    c->write = write;
    c->progress = progress;
    c->context = context;
    return c;
}

bool mtp_core_step(MtpCore *c)
{
    unsigned char bytes[20];
    if (!receive(c, bytes, 12)) return false;
    uint32_t length = get32(bytes);
    if (length < 12 || length > 32 || length % 4 || get16(bytes + 4) != 1) return false;
    c->operation = get16(bytes + 6);
    c->transaction = get32(bytes + 8);
    uint32_t args[5] = { 0 }, result[5];
    if (length > 12 && !receive(c, bytes, length - 12)) return false;
    for (size_t i = 0; i < (length - 12) / 4; ++i) args[i] = get32(bytes + i * 4);
    size_t count = 0;
    uint16_t code = handle(c, args, result, &count);
    return !c->failed && reply(c, code, result, count);
}

void mtp_core_free(MtpCore *c)
{
    if (!c) return;
    reset(c);
    free(c->objects);
    free(c->root);
    free(c);
}
