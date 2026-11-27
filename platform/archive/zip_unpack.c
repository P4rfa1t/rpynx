#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L

#include "zip_unpack.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>
#include <zlib.h>

#include "cJSON.h"
#include "cover_extract.h"
#include "fs.h"

#define IO_BUF_SIZE   (256u * 1024u)
#define MAX_META_READ (256u * 1024u)
#define RPA_SPLIT_MAX (2ULL << 30)

enum {
    ACT_SKIP = 0,
    ACT_EXTRACT_GAME = 1,
    ACT_EXTRACT_ROOT = 2,
    ACT_PACK = 3
};

struct zip_entry {
    char *name;                /* normalized relative path (no leading /, no \) */
    size_t name_len;
    uint16_t method;
    uint16_t dos_time;
    uint16_t dos_date;
    uint32_t crc32;
    uint64_t comp_size;
    uint64_t uncomp_size;
    uint64_t local_offset;
    bool is_dir;
    bool is_symlink;
    bool is_ascii;
    bool is_junk;
    int action;
    size_t rel_offset;         /* offset inside name for target relpath */
};

struct zip_archive {
    FILE *f;
    uint64_t file_size;
    struct zip_entry *entries;
    size_t count;
    size_t cap;
};

struct rpa_member {
    char *name;                /* UTF-8 relpath under game/ */
    uint64_t offset;
    uint64_t length;
};

struct rpa_writer {
    FILE *f;
    char path[1024];
    struct rpa_member *members;
    size_t count;
    size_t cap;
    uint64_t pos;
    int index;
};

struct byte_buf {
    unsigned char *data;
    size_t len;
    size_t cap;
    bool oom;
};

/* ---------------------------------------------------------- little-endian */

static uint16_t rd16(const unsigned char *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t rd64(const unsigned char *p)
{
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}

static uint64_t monotonic_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
    return 0;
}

static int seek_u64(FILE *f, uint64_t off)
{
    return fseeko(f, (off_t)off, SEEK_SET);
}

/* ----------------------------------------------------------- string utils */

static bool ends_with_ci(const char *s, size_t slen, const char *suf)
{
    size_t n = strlen(suf);
    if (slen < n) return false;
    const char *p = s + slen - n;
    for (size_t i = 0; i < n; i++)
        if (tolower((unsigned char)p[i]) != tolower((unsigned char)suf[i]))
            return false;
    return true;
}

static bool eq_ci(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return false;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static bool starts_with(const char *s, const char *prefix, size_t plen)
{
    return strncmp(s, prefix, plen) == 0;
}

static bool is_ascii_str(const char *s)
{
    for (const unsigned char *p = (const unsigned char *)s; *p; p++)
        if (*p >= 0x80)
            return false;
    return true;
}

static bool is_script_ext(const char *s, size_t len)
{
    return ends_with_ci(s, len, ".rpy") || ends_with_ci(s, len, ".rpyc") ||
           ends_with_ci(s, len, ".rpa") || ends_with_ci(s, len, ".rpym") ||
           ends_with_ci(s, len, ".rpymc");
}

static bool is_non_packable_script(const char *s, size_t len)
{
    return ends_with_ci(s, len, ".rpy") || ends_with_ci(s, len, ".rpym") ||
           ends_with_ci(s, len, "_ren.py") || ends_with_ci(s, len, ".rpa");
}

static bool is_junk_segment(const char *seg, size_t len)
{
    if (len == 0) return false;
    if (len == 8 && strncmp(seg, "__MACOSX", 8) == 0) return true;
    if (len == 9 && strncmp(seg, ".DS_Store", 9) == 0) return true;
    if (len == 9) {
        char tmp[10];
        memcpy(tmp, seg, 9); tmp[9] = '\0';
        if (eq_ci(tmp, "Thumbs.db")) return true;
    }
    if (len == 11) {
        char tmp[12];
        memcpy(tmp, seg, 11); tmp[11] = '\0';
        if (eq_ci(tmp, "desktop.ini")) return true;
    }
    return false;
}

static bool path_has_junk(const char *p)
{
    const char *s = p;
    while (*s) {
        const char *sl = strchr(s, '/');
        size_t len = sl ? (size_t)(sl - s) : strlen(s);
        if (is_junk_segment(s, len))
            return true;
        if (!sl) break;
        s = sl + 1;
    }
    return false;
}

/* Normalize slashes and validate safety: rejects "..", ":", control chars. */
static bool normalize_zip_path(const char *raw, size_t raw_len, char *out, size_t cap, bool *is_dir)
{
    size_t n = 0;
    *is_dir = false;
    if (raw_len == 0) return false;
    if (raw[raw_len - 1] == '/' || raw[raw_len - 1] == '\\')
        *is_dir = true;

    for (size_t i = 0; i < raw_len; i++) {
        unsigned char c = (unsigned char)raw[i];
        if (c == '\\') c = '/';
        if (c < 0x20 || c == ':') return false;
        if (c == '/' && (n == 0 || out[n - 1] == '/'))
            continue;
        if (n + 1 >= cap) return false;
        out[n++] = (char)c;
    }
    while (n >= 2 && out[0] == '.' && out[1] == '/') {
        memmove(out, out + 2, n - 2);
        n -= 2;
        while (n > 0 && out[0] == '/') {
            memmove(out, out + 1, n - 1);
            n--;
        }
    }
    while (n > 0 && out[n - 1] == '/')
        n--;
    out[n] = '\0';
    if (n == 0) return true; /* root directory entry */

    /* Reject any "." or ".." path segment */
    const char *s = out;
    while (*s) {
        const char *sl = strchr(s, '/');
        size_t seglen = sl ? (size_t)(sl - s) : strlen(s);
        if ((seglen == 1 && s[0] == '.') || (seglen == 2 && s[0] == '.' && s[1] == '.'))
            return false;
        if (!sl) break;
        s = sl + 1;
    }
    return true;
}

/* ----------------------------------------------------------- FS helpers */

static void rm_rf(const char *path)
{
    struct stat st;
    if (lstat(path, &st) != 0)
        return;
    if (!S_ISDIR(st.st_mode)) {
        remove(path);
        return;
    }
    DIR *d = opendir(path);
    if (d) {
        struct dirent *e;
        char child[1024];
        while ((e = readdir(d)) != NULL) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
                continue;
            if (snprintf(child, sizeof child, "%s/%s", path, e->d_name) < (int)sizeof child)
                rm_rf(child);
        }
        closedir(d);
    }
    rmdir(path);
}

static bool ensure_parent_dirs(const char *filepath)
{
    char dir[1024];
    const char *sl = strrchr(filepath, '/');
    if (!sl) return true;
    size_t len = (size_t)(sl - filepath);
    if (len == 0 || len >= sizeof dir) return false;
    memcpy(dir, filepath, len);
    dir[len] = '\0';
    return rsl_mkdirs(dir);
}

/* Ensure only ASCII directory components up to the first non-ASCII component. */
static bool ensure_ascii_parent_dirs(const char *base_dir, const char *rel)
{
    char path[1024];
    size_t pos = strlen(base_dir);
    if (pos + 2 >= sizeof path) return false;
    memcpy(path, base_dir, pos);
    if (!rsl_mkdirs(path)) return false;

    const char *s = rel;
    while (*s) {
        const char *sl = strchr(s, '/');
        if (!sl) break; /* last component is filename */
        size_t seglen = (size_t)(sl - s);
        bool seg_ascii = true;
        for (size_t i = 0; i < seglen; i++) {
            if ((unsigned char)s[i] >= 0x80) {
                seg_ascii = false;
                break;
            }
        }
        if (!seg_ascii) break; /* never mkdir non-ASCII on Switch SD */
        if (seglen > 0) {
            if (pos + 1 + seglen >= sizeof path) return false;
            path[pos++] = '/';
            memcpy(path + pos, s, seglen);
            pos += seglen;
            path[pos] = '\0';
            mkdir(path, 0777);
        }
        s = sl + 1;
    }
    return true;
}

/* -------------------------------------------------------- options.rpy title */

bool rsl_extract_options_title(const char *text, char *out, size_t cap)
{
    if (!text || !out || cap == 0) return false;
    out[0] = '\0';
    const char *p = text;
    while ((p = strstr(p, "config.name")) != NULL) {
        /* Make sure identifier boundary holds before config.name and skip comments */
        bool valid_start = (p == text || p[-1] == '\n' || p[-1] == '\r' || p[-1] == ' ' || p[-1] == '\t');
        if (valid_start) {
            const char *ls = p;
            while (ls > text && ls[-1] != '\n' && ls[-1] != '\r')
                ls--;
            while (*ls == ' ' || *ls == '\t')
                ls++;
            if (*ls == '#')
                valid_start = false;
        }
        p += 11;
        if (!valid_start) continue;
        const char *q = p;
        while (*q == ' ' || *q == '\t') q++;
        if (*q != '=') continue;
        q++;
        while (*q == ' ' || *q == '\t') q++;
        if (*q == '_' && q[1] == '(') {
            q += 2;
            while (*q == ' ' || *q == '\t') q++;
        }
        if ((*q == 'u' || *q == 'U' || *q == 'r' || *q == 'R') && (q[1] == '"' || q[1] == '\''))
            q++;
        if (*q != '"' && *q != '\'') continue;
        char quote = *q++;
        size_t n = 0;
        while (*q && *q != quote && *q != '\n' && *q != '\r') {
            if (*q == '\\' && q[1]) {
                q++;
                char ch = *q;
                if (ch == 'n') ch = ' ';
                else if (ch == 't') ch = ' ';
                if (n + 1 < cap) out[n++] = ch;
                q++;
                continue;
            }
            if (n + 1 < cap) out[n++] = *q;
            q++;
        }
        if (*q != quote) {
            out[0] = '\0';
            continue;
        }
        out[n] = '\0';
        /* trim trailing spaces */
        while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\t'))
            out[--n] = '\0';
        size_t lead = 0;
        while (out[lead] == ' ' || out[lead] == '\t') lead++;
        if (lead > 0)
            memmove(out, out + lead, n - lead + 1);
        if (out[0]) return true;
    }
    return false;
}

/* ----------------------------------------------------- ZIP Central Dir */

static void zip_close_archive(struct zip_archive *z)
{
    if (!z) return;
    if (z->f) fclose(z->f);
    for (size_t i = 0; i < z->count; i++)
        free(z->entries[i].name);
    free(z->entries);
    memset(z, 0, sizeof *z);
}

static bool zip_open_archive(const char *path, struct zip_archive *z, char *err, size_t errlen)
{
    memset(z, 0, sizeof *z);
    z->f = fopen(path, "rb");
    if (!z->f) {
        snprintf(err, errlen, "无法打开压缩包: %s", strerror(errno));
        return false;
    }
    if (fseeko(z->f, 0, SEEK_END) != 0) {
        snprintf(err, errlen, "无法读取压缩包大小");
        goto fail;
    }
    off_t end_off = ftello(z->f);
    if (end_off < 22) {
        snprintf(err, errlen, "文件过小，不是有效的 ZIP 压缩包");
        goto fail;
    }
    z->file_size = (uint64_t)end_off;

    uint64_t tail_len = z->file_size < (65536ULL + 22ULL) ? z->file_size : (65536ULL + 22ULL);
    uint64_t tail_start = z->file_size - tail_len;
    unsigned char *tail = malloc((size_t)tail_len);
    if (!tail) {
        snprintf(err, errlen, "内存不足");
        goto fail;
    }
    if (seek_u64(z->f, tail_start) != 0 || fread(tail, 1, (size_t)tail_len, z->f) != (size_t)tail_len) {
        free(tail);
        snprintf(err, errlen, "读取 ZIP 尾部失败");
        goto fail;
    }

    int64_t eocd_pos = -1;
    for (int64_t i = (int64_t)tail_len - 22; i >= 0; i--) {
        if (tail[i] == 0x50 && tail[i + 1] == 0x4b && tail[i + 2] == 0x05 && tail[i + 3] == 0x06) {
            uint16_t clen = rd16(tail + i + 20);
            if ((uint64_t)i + 22ULL + clen == tail_len) {
                eocd_pos = i;
                break;
            }
        }
    }
    if (eocd_pos < 0) {
        free(tail);
        snprintf(err, errlen, "未找到 ZIP 目录结束标识");
        goto fail;
    }

    const unsigned char *eocd = tail + eocd_pos;
    uint16_t disk = rd16(eocd + 4);
    uint16_t cd_disk = rd16(eocd + 6);
    uint64_t entry_count = rd16(eocd + 10);
    uint64_t cd_size = rd32(eocd + 12);
    uint64_t cd_offset = rd32(eocd + 16);
    uint64_t eocd_abs = tail_start + (uint64_t)eocd_pos;

    bool maybe_zip64 = (entry_count == 0xffff || cd_size == 0xffffffffu || cd_offset == 0xffffffffu);
    free(tail);

    if (disk != 0 || cd_disk != 0) {
        snprintf(err, errlen, "不支持分卷 ZIP 压缩包");
        goto fail;
    }

    if (maybe_zip64 && eocd_abs >= 20) {
        unsigned char loc[20];
        if (seek_u64(z->f, eocd_abs - 20) == 0 && fread(loc, 1, 20, z->f) == 20 &&
            rd32(loc) == 0x07064b50u) {
            uint64_t z64_off = rd64(loc + 8);
            unsigned char z64[56];
            if (z64_off + 56 <= z->file_size && seek_u64(z->f, z64_off) == 0 &&
                fread(z64, 1, 56, z->f) == 56 && rd32(z64) == 0x06064b50u) {
                entry_count = rd64(z64 + 32);
                cd_size = rd64(z64 + 40);
                cd_offset = rd64(z64 + 48);
            }
        }
    }

    if (cd_offset + cd_size > z->file_size || cd_size > (256ULL << 20) || entry_count > 500000) {
        snprintf(err, errlen, "ZIP 中央目录大小或偏移异常");
        goto fail;
    }

    unsigned char *cd = malloc((size_t)(cd_size ? cd_size : 1));
    if (!cd) {
        snprintf(err, errlen, "内存不足，无法读取 ZIP 目录");
        goto fail;
    }
    if (cd_size && (seek_u64(z->f, cd_offset) != 0 || fread(cd, 1, (size_t)cd_size, z->f) != (size_t)cd_size)) {
        free(cd);
        snprintf(err, errlen, "读取 ZIP 中央目录失败");
        goto fail;
    }

    z->cap = (size_t)(entry_count > 64 ? entry_count : 64);
    z->entries = calloc(z->cap, sizeof *z->entries);
    if (!z->entries) {
        free(cd);
        snprintf(err, errlen, "内存不足");
        goto fail;
    }

    size_t pos = 0;
    for (uint64_t idx = 0; idx < entry_count && pos + 46 <= cd_size; idx++) {
        const unsigned char *h = cd + pos;
        if (rd32(h) != 0x02014b50u) {
            free(cd);
            snprintf(err, errlen, "ZIP 目录条目损坏");
            goto fail;
        }
        uint16_t ver_made = rd16(h + 4);
        uint16_t flags = rd16(h + 8);
        uint16_t method = rd16(h + 10);
        uint16_t dos_time = rd16(h + 12);
        uint16_t dos_date = rd16(h + 14);
        uint32_t crc = rd32(h + 16);
        uint64_t comp_sz = rd32(h + 20);
        uint64_t uncomp_sz = rd32(h + 24);
        uint16_t nlen = rd16(h + 28);
        uint16_t elen = rd16(h + 30);
        uint16_t clen = rd16(h + 32);
        uint32_t ext_attr = rd32(h + 38);
        uint64_t l_off = rd32(h + 42);
        size_t next = pos + 46 + nlen + elen + clen;
        if (next > cd_size) {
            free(cd);
            snprintf(err, errlen, "ZIP 目录条目越界");
            goto fail;
        }

        const unsigned char *ex = h + 46 + nlen;
        size_t epos = 0;
        while (epos + 4 <= elen) {
            uint16_t eid = rd16(ex + epos);
            uint16_t esz = rd16(ex + epos + 2);
            epos += 4;
            if (epos + esz > elen) break;
            if (eid == 0x0001u) {
                size_t k = 0;
                if (uncomp_sz == 0xffffffffu && k + 8 <= esz) { uncomp_sz = rd64(ex + epos + k); k += 8; }
                if (comp_sz == 0xffffffffu && k + 8 <= esz) { comp_sz = rd64(ex + epos + k); k += 8; }
                if (l_off == 0xffffffffu && k + 8 <= esz) { l_off = rd64(ex + epos + k); k += 8; }
            }
            epos += esz;
        }

        char norm[1024];
        bool is_dir = false;
        if (!normalize_zip_path((const char *)(h + 46), nlen, norm, sizeof norm, &is_dir)) {
            free(cd);
            snprintf(err, errlen, "压缩包包含不安全路径");
            goto fail;
        }
        pos = next;
        if (norm[0] == '\0')
            continue;

        if ((flags & 1u) != 0 && !is_dir) {
            free(cd);
            snprintf(err, errlen, "不支持加密的 ZIP 压缩包");
            goto fail;
        }

        if (z->count == z->cap) {
            size_t nc = z->cap * 2;
            struct zip_entry *ne = realloc(z->entries, nc * sizeof *ne);
            if (!ne) {
                free(cd);
                snprintf(err, errlen, "内存不足");
                goto fail;
            }
            z->entries = ne;
            z->cap = nc;
        }
        struct zip_entry *e = &z->entries[z->count++];
        memset(e, 0, sizeof *e);
        e->name = strdup(norm);
        if (!e->name) {
            z->count--;
            free(cd);
            snprintf(err, errlen, "内存不足");
            goto fail;
        }
        e->name_len = strlen(norm);
        e->method = method;
        e->dos_time = dos_time;
        e->dos_date = dos_date;
        e->crc32 = crc;
        e->comp_size = comp_sz;
        e->uncomp_size = uncomp_sz;
        e->local_offset = l_off;
        e->is_dir = is_dir;
        e->is_symlink = ((ver_made >> 8) == 3) && (((ext_attr >> 16) & 0170000u) == 0120000u);
        e->is_ascii = is_ascii_str(norm);
        e->is_junk = path_has_junk(norm);
    }
    free(cd);
    return true;
fail:
    zip_close_archive(z);
    return false;
}

static bool zip_local_data_offset(FILE *f, uint64_t file_size, uint64_t local_off, uint64_t *data_off)
{
    unsigned char lh[30];
    if (local_off + 30 > file_size || seek_u64(f, local_off) != 0 || fread(lh, 1, 30, f) != 30)
        return false;
    if (rd32(lh) != 0x04034b50u)
        return false;
    uint16_t nlen = rd16(lh + 26);
    uint16_t elen = rd16(lh + 28);
    uint64_t off = local_off + 30 + nlen + elen;
    if (off > file_size) return false;
    *data_off = off;
    return true;
}

static const struct zip_entry *zip_find(const struct zip_archive *z, const char *path)
{
    for (size_t i = 0; i < z->count; i++) {
        if (!z->entries[i].is_dir && !z->entries[i].is_symlink && strcmp(z->entries[i].name, path) == 0)
            return &z->entries[i];
    }
    return NULL;
}

/* Decompress an entry up to max_bytes into malloc'd NUL-terminated buffer. */
static char *zip_read_mem(const struct zip_archive *z, const struct zip_entry *e,
                          size_t max_bytes, size_t *out_len)
{
    if (!e || e->is_dir || e->is_symlink || e->uncomp_size > max_bytes)
        return NULL;
    if (e->method != 0 && e->method != 8)
        return NULL;
    uint64_t data_off = 0;
    if (!zip_local_data_offset(z->f, z->file_size, e->local_offset, &data_off))
        return NULL;
    if (data_off + e->comp_size > z->file_size || seek_u64(z->f, data_off) != 0)
        return NULL;

    size_t ulen = (size_t)e->uncomp_size;
    char *out = malloc(ulen + 1);
    if (!out) return NULL;
    if (ulen == 0) {
        out[0] = '\0';
        if (out_len) *out_len = 0;
        return out;
    }

    if (e->method == 0) {
        if (fread(out, 1, ulen, z->f) != ulen) {
            free(out);
            return NULL;
        }
        out[ulen] = '\0';
        if (out_len) *out_len = ulen;
        return out;
    }

    unsigned char *comp = malloc((size_t)(e->comp_size ? e->comp_size : 1));
    if (!comp) {
        free(out);
        return NULL;
    }
    if (e->comp_size && fread(comp, 1, (size_t)e->comp_size, z->f) != (size_t)e->comp_size) {
        free(comp);
        free(out);
        return NULL;
    }
    z_stream zs = { 0 };
    zs.next_in = comp;
    zs.avail_in = (uInt)e->comp_size;
    zs.next_out = (unsigned char *)out;
    zs.avail_out = (uInt)ulen;
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) {
        free(comp);
        free(out);
        return NULL;
    }
    int r = inflate(&zs, Z_FINISH);
    size_t got = (size_t)zs.total_out;
    inflateEnd(&zs);
    free(comp);
    if (r != Z_STREAM_END && got != ulen) {
        free(out);
        return NULL;
    }
    out[got] = '\0';
    if (out_len) *out_len = got;
    return out;
}

/* Read up to cap compressed bytes prefix and inflate up to max_out bytes (for .rpyc header check). */
static size_t zip_read_partial(const struct zip_archive *z, const struct zip_entry *e,
                               unsigned char *out, size_t max_out)
{
    if (!e || e->is_dir || e->is_symlink || (e->method != 0 && e->method != 8))
        return 0;
    uint64_t data_off = 0;
    if (!zip_local_data_offset(z->f, z->file_size, e->local_offset, &data_off))
        return 0;
    size_t to_read = (size_t)(e->comp_size < max_out ? e->comp_size : max_out);
    if (seek_u64(z->f, data_off) != 0)
        return 0;
    if (e->method == 0)
        return fread(out, 1, to_read, z->f);

    unsigned char *comp = malloc(to_read ? to_read : 1);
    if (!comp) return 0;
    size_t nr = fread(comp, 1, to_read, z->f);
    z_stream zs = { 0 };
    zs.next_in = comp;
    zs.avail_in = (uInt)nr;
    zs.next_out = out;
    zs.avail_out = (uInt)max_out;
    size_t got = 0;
    if (inflateInit2(&zs, -MAX_WBITS) == Z_OK) {
        inflate(&zs, Z_SYNC_FLUSH);
        got = (size_t)zs.total_out;
        inflateEnd(&zs);
    }
    free(comp);
    return got;
}

/* ----------------------------------------------------- layout detection */

struct dir_child {
    char name[256];
    bool is_dir;
    bool has_file;
};

static int cmp_child(const void *a, const void *b)
{
    const struct dir_child *ca = a, *cb = b;
    return strcmp(ca->name, cb->name);
}

static bool has_prefix_segment(const struct zip_archive *z, const char *prefix, const char *sub)
{
    char full[1024];
    snprintf(full, sizeof full, "%s%s/", prefix, sub);
    size_t flen = strlen(full);
    for (size_t i = 0; i < z->count; i++) {
        const struct zip_entry *e = &z->entries[i];
        if (e->is_junk || e->is_symlink) continue;
        if (starts_with(e->name, full, flen))
            return true;
    }
    return false;
}

static int collect_children(const struct zip_archive *z, const char *prefix,
                            struct dir_child **out_list)
{
    size_t plen = strlen(prefix);
    int cap = 32, n = 0;
    struct dir_child *list = calloc((size_t)cap, sizeof *list);
    if (!list) return -1;

    for (size_t i = 0; i < z->count; i++) {
        const struct zip_entry *e = &z->entries[i];
        if (e->is_junk || e->is_symlink) continue;
        if (plen > 0 && !starts_with(e->name, prefix, plen))
            continue;
        const char *rest = e->name + plen;
        if (*rest == '\0') continue;
        const char *sl = strchr(rest, '/');
        size_t seglen = sl ? (size_t)(sl - rest) : strlen(rest);
        if (seglen == 0 || seglen >= sizeof list[0].name) continue;
        char seg[256];
        memcpy(seg, rest, seglen);
        seg[seglen] = '\0';
        bool seg_dir = (sl != NULL) || e->is_dir;

        int found = -1;
        for (int k = 0; k < n; k++) {
            if (strcmp(list[k].name, seg) == 0) {
                found = k;
                break;
            }
        }
        if (found < 0) {
            if (n == cap) {
                cap *= 2;
                struct dir_child *nl = realloc(list, (size_t)cap * sizeof *nl);
                if (!nl) { free(list); return -1; }
                list = nl;
            }
            found = n++;
            memset(&list[found], 0, sizeof list[found]);
            memcpy(list[found].name, seg, seglen + 1);
        }
        if (seg_dir) list[found].is_dir = true;
        else list[found].has_file = true;
    }
    qsort(list, (size_t)n, sizeof *list, cmp_child);
    *out_list = list;
    return n;
}

static bool has_direct_scripts(const struct zip_archive *z, const char *prefix)
{
    size_t plen = strlen(prefix);
    for (size_t i = 0; i < z->count; i++) {
        const struct zip_entry *e = &z->entries[i];
        if (e->is_dir || e->is_junk || e->is_symlink) continue;
        if (plen > 0 && !starts_with(e->name, prefix, plen)) continue;
        const char *rest = e->name + plen;
        if (strchr(rest, '/') != NULL) continue;
        if (is_script_ext(rest, strlen(rest))) return true;
    }
    return false;
}

static bool dir_exists_in_children(const struct dir_child *list, int n, const char *name)
{
    if (!name || !name[0] || strcmp(name, "renpy") == 0) return false;
    for (int i = 0; i < n; i++)
        if (list[i].is_dir && strcmp(list[i].name, name) == 0)
            return true;
    return false;
}

static bool find_gamedir_zip(const struct zip_archive *z, const char *prefix, int depth,
                             char *basedir, size_t bcap, char *gamedir, size_t gcap)
{
    if (depth < 3 && has_prefix_segment(z, prefix, "Contents/Resources/autorun")) {
        char next[1024];
        if (snprintf(next, sizeof next, "%sContents/Resources/autorun/", prefix) < (int)sizeof next)
            return find_gamedir_zip(z, next, depth + 1, basedir, bcap, gamedir, gcap);
    }

    struct dir_child *children = NULL;
    int n = collect_children(z, prefix, &children);
    if (n < 0) return false;

    /* Check launcher stems first, then "game", then "data" */
    for (int i = 0; i < n; i++) {
        if (!children[i].has_file && !ends_with_ci(children[i].name, strlen(children[i].name), ".app"))
            continue;
        size_t len = strlen(children[i].name);
        if (!(ends_with_ci(children[i].name, len, ".exe") ||
              ends_with_ci(children[i].name, len, ".sh") ||
              ends_with_ci(children[i].name, len, ".py") ||
              ends_with_ci(children[i].name, len, ".app")))
            continue;
        const char *dot = strrchr(children[i].name, '.');
        size_t slen = dot ? (size_t)(dot - children[i].name) : len;
        if (slen > 0 && slen < 256) {
            char stem[256];
            memcpy(stem, children[i].name, slen);
            stem[slen] = '\0';
            if (dir_exists_in_children(children, n, stem)) {
                snprintf(basedir, bcap, "%s", prefix);
                snprintf(gamedir, gcap, "%s%s/", prefix, stem);
                free(children);
                return true;
            }
            for (size_t k = 0; k < slen; k++) {
                if ((stem[k] == ' ' || stem[k] == '_') && stem[k + 1] &&
                    dir_exists_in_children(children, n, stem + k + 1)) {
                    snprintf(basedir, bcap, "%s", prefix);
                    snprintf(gamedir, gcap, "%s%s/", prefix, stem + k + 1);
                    free(children);
                    return true;
                }
            }
        }
    }

    const char *fallbacks[] = { "game", "data" };
    for (int f = 0; f < 2; f++) {
        if (dir_exists_in_children(children, n, fallbacks[f])) {
            snprintf(basedir, bcap, "%s", prefix);
            snprintf(gamedir, gcap, "%s%s/", prefix, fallbacks[f]);
            free(children);
            return true;
        }
    }

    if (has_direct_scripts(z, prefix)) {
        snprintf(basedir, bcap, "%s", prefix);
        snprintf(gamedir, gcap, "%s", prefix);
        free(children);
        return true;
    }

    if (depth < 3) {
        for (int i = 0; i < n; i++) {
            size_t len = strlen(children[i].name);
            if (children[i].is_dir && ends_with_ci(children[i].name, len, ".app")) {
                char sub[1024];
                if (snprintf(sub, sizeof sub, "%s%s/Contents/Resources/autorun/", prefix, children[i].name) < (int)sizeof sub &&
                    has_prefix_segment(z, prefix, children[i].name)) {
                    /* check autorun exists */
                    char chk[1024];
                    snprintf(chk, sizeof chk, "%s/Contents/Resources/autorun", children[i].name);
                    if (has_prefix_segment(z, prefix, chk)) {
                        char app_prefix[1024];
                        snprintf(app_prefix, sizeof app_prefix, "%s%s/", prefix, children[i].name);
                        free(children);
                        return find_gamedir_zip(z, app_prefix, depth + 1, basedir, bcap, gamedir, gcap);
                    }
                }
            }
        }
        int dir_count = 0, dir_idx = -1;
        for (int i = 0; i < n; i++) {
            if (children[i].is_dir) {
                dir_count++;
                dir_idx = i;
            }
        }
        if (dir_count == 1) {
            char next[1024];
            if (snprintf(next, sizeof next, "%s%s/", prefix, children[dir_idx].name) < (int)sizeof next) {
                free(children);
                return find_gamedir_zip(z, next, depth + 1, basedir, bcap, gamedir, gcap);
            }
        }
    }

    free(children);
    return false;
}

/* ----------------------------------------------------- version & metadata */

static void detect_version_and_python(const struct zip_archive *z, const char *basedir,
                                      const char *gamedir, char *ver_out, size_t ver_cap,
                                      int *py_out)
{
    size_t blen = strlen(basedir), glen = strlen(gamedir);
    bool hint2 = false, hint3 = false;
    ver_out[0] = '\0';

    char lib_pref[1032];
    snprintf(lib_pref, sizeof lib_pref, "%.1020slib/", basedir);
    size_t lplen = strlen(lib_pref);

    for (size_t i = 0; i < z->count; i++) {
        const struct zip_entry *e = &z->entries[i];
        if (e->is_junk || e->is_symlink) continue;
        if (blen > 0 && !starts_with(e->name, basedir, blen)) continue;
        const char *rel = e->name + blen;
        if (strcmp(rel, "python27.dll") == 0)
            hint2 = true;
        if (starts_with(e->name, lib_pref, lplen)) {
            const char *lr = e->name + lplen;
            const char *sl = strchr(lr, '/');
            size_t seg = sl ? (size_t)(sl - lr) : strlen(lr);
            char tmp[128];
            if (seg < sizeof tmp) {
                memcpy(tmp, lr, seg);
                tmp[seg] = '\0';
                for (size_t k = 0; k < seg; k++)
                    tmp[k] = (char)tolower((unsigned char)tmp[k]);
                if (strncmp(tmp, "py3-", 4) == 0 || strncmp(tmp, "python3", 7) == 0)
                    hint3 = true;
                else if (strncmp(tmp, "py2-", 4) == 0 || strcmp(tmp, "python2.7") == 0 ||
                         strcmp(tmp, "pythonlib2.7") == 0 || strcmp(tmp, "python27.dll") == 0)
                    hint2 = true;
            }
        }
    }

    int maj = 0, min = 0, pat = 0;
    char path[1024];

    /* 1. renpy/vc_version.py (8.1+: version = '8.x.y') */
    if (snprintf(path, sizeof path, "%srenpy/vc_version.py", basedir) < (int)sizeof path) {
        const struct zip_entry *vc = zip_find(z, path);
        char *txt = zip_read_mem(z, vc, 65536, NULL);
        if (txt) {
            const char *p = txt;
            while ((p = strstr(p, "version")) != NULL) {
                if ((p == txt || p[-1] == '\n' || p[-1] == '\r') && (p[7] == ' ' || p[7] == '\t' || p[7] == '=')) {
                    const char *q = p + 7;
                    while (*q == ' ' || *q == '\t') q++;
                    if (*q == '=') {
                        q++;
                        while (*q == ' ' || *q == '\t') q++;
                        if (*q == '\'' || *q == '"') {
                            char qc = *q++;
                            char tmp[24];
                            size_t k = 0;
                            while (*q && *q != qc && k + 1 < sizeof tmp && (isdigit((unsigned char)*q) || *q == '.'))
                                tmp[k++] = *q++;
                            tmp[k] = '\0';
                            if (sscanf(tmp, "%d.%d.%d", &maj, &min, &pat) >= 2) {
                                snprintf(ver_out, ver_cap, "%s", tmp);
                                break;
                            }
                            maj = min = pat = 0;
                        }
                    }
                }
                p += 7;
            }
            free(txt);
        }
    }

    /* 2. game/script_version.txt: "(8, 0, 3)" or "(7, 4, 11)" */
    if (!ver_out[0] && snprintf(path, sizeof path, "%sscript_version.txt", gamedir) < (int)sizeof path) {
        const struct zip_entry *sv = zip_find(z, path);
        char *txt = zip_read_mem(z, sv, 1024, NULL);
        if (txt) {
            if (sscanf(txt, " (%d , %d , %d", &maj, &min, &pat) == 3 ||
                sscanf(txt, "(%d, %d, %d", &maj, &min, &pat) == 3) {
                snprintf(ver_out, ver_cap, "%d.%d.%d", maj, min, pat);
            }
            free(txt);
        }
    }

    /* 3. renpy/__init__.py: version_tuple = (M, m, p, ...) */
    if (!ver_out[0] && snprintf(path, sizeof path, "%srenpy/__init__.py", basedir) < (int)sizeof path) {
        const struct zip_entry *ri = zip_find(z, path);
        char *txt = zip_read_mem(z, ri, 131072, NULL);
        if (txt) {
            int best_maj = 0, best_min = 0, best_pat = 0;
            const char *p = txt;
            while ((p = strstr(p, "version_tuple")) != NULL) {
                const char *q = strchr(p, '(');
                if (q) {
                    int a = 0, b = 0, c = 0;
                    if (sscanf(q + 1, " %d , %d , %d", &a, &b, &c) == 3 && a >= 5 && a <= 15) {
                        bool match = true;
                        if (hint2 && !hint3 && a >= 8) match = false;
                        if (hint3 && !hint2 && a < 8) match = false;
                        if (match && (a > best_maj || (a == best_maj && (b > best_min || (b == best_min && c > best_pat))))) {
                            best_maj = a; best_min = b; best_pat = c;
                        }
                    }
                }
                p += 13;
            }
            if (best_maj > 0) {
                maj = best_maj; min = best_min; pat = best_pat;
                snprintf(ver_out, ver_cap, "%d.%d.%d", maj, min, pat);
            }
            free(txt);
        }
    }

    /* 4. Fallback: .rpyc slot 1 pickle protocol */
    int rpyc_hint = 0;
    if (maj == 0 && !(hint2 ^ hint3)) {
        for (size_t i = 0; i < z->count; i++) {
            const struct zip_entry *e = &z->entries[i];
            if (e->is_dir || e->is_junk || e->is_symlink) continue;
            if (glen > 0 && !starts_with(e->name, gamedir, glen)) continue;
            if (!ends_with_ci(e->name, e->name_len, ".rpyc")) continue;
            unsigned char hdr[64];
            size_t got = zip_read_partial(z, e, hdr, sizeof hdr);
            if (got >= 12) {
                if (memcmp(hdr, "RENPY RPC2", 10) == 0 && got >= 46) {
                    for (int s = 0; s < 3; s++) {
                        uint32_t slot = rd32(hdr + 10 + s * 12);
                        uint32_t off = rd32(hdr + 14 + s * 12);
                        if (slot == 1 && off >= 46 && off + 2 <= got) {
                            /* Check uncompressed or zlib prefix in slot 1 */
                            uint64_t doff = 0;
                            if (e->method == 0 && zip_local_data_offset(z->f, z->file_size, e->local_offset, &doff)) {
                                unsigned char zbuf[256];
                                if (seek_u64(z->f, doff + off) == 0) {
                                    size_t nr = fread(zbuf, 1, sizeof zbuf, z->f);
                                    z_stream zs = { .next_in = zbuf, .avail_in = (uInt)nr };
                                    unsigned char pb[4] = { 0 };
                                    zs.next_out = pb; zs.avail_out = 4;
                                    if (inflateInit(&zs) == Z_OK) {
                                        inflate(&zs, Z_SYNC_FLUSH);
                                        if (zs.total_out >= 2 && pb[0] == 0x80)
                                            rpyc_hint = pb[1] >= 3 ? 3 : 2;
                                        else if (zs.total_out >= 1 && (pb[0] == '(' || pb[0] == '}' || pb[0] == ']' || pb[0] == 'c'))
                                            rpyc_hint = 2;
                                        inflateEnd(&zs);
                                    }
                                }
                            }
                            break;
                        }
                    }
                } else {
                    if (hdr[0] == 0x80) rpyc_hint = hdr[1] >= 3 ? 3 : 2;
                    else if (hdr[0] == '(' || hdr[0] == '}' || hdr[0] == ']' || hdr[0] == 'c') rpyc_hint = 2;
                }
            }
            if (rpyc_hint) break;
        }
    }

    if (maj >= 8) *py_out = 3;
    else if (maj >= 1 && maj <= 7) *py_out = 2;
    else if (hint2 && !hint3) *py_out = 2;
    else if (hint3 && !hint2) *py_out = 3;
    else if (rpyc_hint == 2) *py_out = 2;
    else *py_out = 3;
}

static void strip_zip_basename(const char *zip_path, char *out, size_t cap)
{
    const char *sl = strrchr(zip_path, '/');
#ifdef _WIN32
    const char *bsl = strrchr(zip_path, '\\');
    if (bsl && (!sl || bsl > sl)) sl = bsl;
#endif
    const char *base = sl ? sl + 1 : zip_path;
    snprintf(out, cap, "%s", base);
    size_t n = strlen(out);
    if (ends_with_ci(out, n, ".zip")) {
        out[n - 4] = '\0';
        n -= 4;
    }
    if (ends_with_ci(out, n, ".app"))
        out[n - 4] = '\0';
}

static bool is_ignored_launcher_stem(const char *stem)
{
    return eq_ci(stem, "python") || eq_ci(stem, "pythonw") || eq_ci(stem, "renpy") ||
           eq_ci(stem, "zsync") || eq_ci(stem, "zsyncmake") || eq_ci(stem, "game") ||
           eq_ci(stem, "launcher");
}

static void detect_title_and_id(const struct zip_archive *z, const char *zip_path,
                                const char *games_dir, const char *basedir, const char *gamedir,
                                char *title_out, size_t title_cap, char *id_out, size_t id_cap)
{
    title_out[0] = '\0';
    char opt_path[1024];
    if (snprintf(opt_path, sizeof opt_path, "%soptions.rpy", gamedir) < (int)sizeof opt_path) {
        const struct zip_entry *opt = zip_find(z, opt_path);
        char *txt = zip_read_mem(z, opt, 131072, NULL);
        if (txt) {
            rsl_extract_options_title(txt, title_out, title_cap);
            free(txt);
        }
    }

    size_t blen = strlen(basedir);
    if (!title_out[0]) {
        /* Look for launcher *.exe / *.app / *.sh in basedir */
        for (size_t i = 0; i < z->count; i++) {
            const struct zip_entry *e = &z->entries[i];
            if (e->is_junk || e->is_symlink) continue;
            if (blen > 0 && !starts_with(e->name, basedir, blen)) continue;
            const char *rest = e->name + blen;
            const char *sl = strchr(rest, '/');
            size_t seg = sl ? (size_t)(sl - rest) : strlen(rest);
            char name[128];
            if (seg == 0 || seg >= sizeof name) continue;
            memcpy(name, rest, seg);
            name[seg] = '\0';
            if ((sl == NULL && (ends_with_ci(name, seg, ".exe") || ends_with_ci(name, seg, ".sh"))) ||
                ends_with_ci(name, seg, ".app")) {
                char *dot = strrchr(name, '.');
                if (dot) *dot = '\0';
                if (name[0] && !is_ignored_launcher_stem(name)) {
                    snprintf(title_out, title_cap, "%.*s", (int)(title_cap - 1), name);
                    break;
                }
            }
        }
    }

    char zip_stem[128];
    strip_zip_basename(zip_path, zip_stem, sizeof zip_stem);
    if (!title_out[0]) {
        if (basedir[0]) {
            char btmp[1024];
            snprintf(btmp, sizeof btmp, "%s", basedir);
            size_t bn = strlen(btmp);
            while (bn > 0 && btmp[bn - 1] == '/') btmp[--bn] = '\0';
            const char *last = strrchr(btmp, '/');
            const char *folder = last ? last + 1 : btmp;
            if (strcmp(folder, "autorun") == 0) {
                /* check for <Name>.app/Contents/Resources/autorun */
                const char *ap = strstr(btmp, ".app/");
                if (ap) {
                    const char *s = ap;
                    while (s > btmp && s[-1] != '/') s--;
                    size_t ln = (size_t)(ap - s);
                    if (ln > 0 && ln < title_cap) {
                        memcpy(title_out, s, ln);
                        title_out[ln] = '\0';
                    }
                }
            }
            if (!title_out[0] && strcmp(folder, "game") != 0 && strcmp(folder, "autorun") != 0)
                snprintf(title_out, title_cap, "%s", folder);
        }
        if (!title_out[0])
            snprintf(title_out, title_cap, "%s", zip_stem[0] ? zip_stem : "game");
    }

    /* Derive ASCII id <= 48 chars */
    const char *src = title_out;
    for (int pass = 0; pass < 2; pass++) {
        size_t n = 0;
        bool prev_dash = true;
        for (const char *p = src; *p && n < 48; p++) {
            unsigned char c = (unsigned char)*p;
            if (isalnum(c)) {
                id_out[n++] = (char)tolower(c);
                prev_dash = false;
            } else if (!prev_dash && n > 0 && (c == ' ' || c == '-' || c == '_' || c == '.')) {
                id_out[n++] = '-';
                prev_dash = true;
            }
        }
        while (n > 0 && id_out[n - 1] == '-') n--;
        id_out[n] = '\0';
        if (id_out[0] && strcmp(id_out, "game") != 0)
            break;
        src = zip_stem;
    }
    if (!id_out[0])
        snprintf(id_out, id_cap, "game");

    /* Deduplicate against existing folders in games_dir */
    char cand[64];
    snprintf(cand, sizeof cand, "%s", id_out);
    for (int suffix = 2; suffix <= 99; suffix++) {
        char probe[1024];
        struct stat st;
        if (snprintf(probe, sizeof probe, "%s/%s", games_dir, cand) < (int)sizeof probe &&
            stat(probe, &st) != 0) {
            snprintf(id_out, id_cap, "%s", cand);
            return;
        }
        snprintf(cand, sizeof cand, "%.44s-%d", id_out, suffix);
    }
}

/* ----------------------------------------------------- entry classification */

static bool is_desktop_sibling_skip(const char *rel, size_t len, const char *gamedir_seg)
{
    if (starts_with(rel, "renpy/", 6) || strcmp(rel, "renpy") == 0) return true;
    if (starts_with(rel, "lib/", 4) || strcmp(rel, "lib") == 0) return true;
    if (starts_with(rel, "saves/", 6) || strcmp(rel, "saves") == 0) return true;
    if (starts_with(rel, "cache/", 6) || strcmp(rel, "cache") == 0) return true;
    if (gamedir_seg && gamedir_seg[0]) {
        size_t gl = strlen(gamedir_seg);
        if (strncmp(rel, gamedir_seg, gl) == 0 && (rel[gl] == '/' || rel[gl] == '\0'))
            return true;
    }
    /* Skip any macOS .app bundle anywhere in basedir */
    const char *sl = strchr(rel, '/');
    size_t top_len = sl ? (size_t)(sl - rel) : len;
    if (ends_with_ci(rel, top_len, ".app"))
        return true;

    if (sl == NULL) {
        if (ends_with_ci(rel, len, ".exe") || ends_with_ci(rel, len, ".sh") ||
            ends_with_ci(rel, len, ".py") || ends_with_ci(rel, len, ".dll") ||
            ends_with_ci(rel, len, ".so") || ends_with_ci(rel, len, ".dylib") ||
            ends_with_ci(rel, len, ".bat") || ends_with_ci(rel, len, ".cmd") ||
            ends_with_ci(rel, len, ".nro") || ends_with_ci(rel, len, ".ico") ||
            ends_with_ci(rel, len, ".icns"))
            return true;
        if (eq_ci(rel, "log.txt") || eq_ci(rel, "traceback.txt") || eq_ci(rel, "errors.txt") ||
            eq_ci(rel, "manifest.json") || eq_ci(rel, "cover.jpg") || eq_ci(rel, "cover.bmp"))
            return true;
    }
    return false;
}

static bool has_segment_ci(const char *rel, const char *seg_name)
{
    size_t sn = strlen(seg_name);
    const char *s = rel;
    while (*s) {
        const char *sl = strchr(s, '/');
        size_t len = sl ? (size_t)(sl - s) : strlen(s);
        if (len == sn) {
            bool match = true;
            for (size_t k = 0; k < sn; k++) {
                if (tolower((unsigned char)s[k]) != tolower((unsigned char)seg_name[k])) {
                    match = false;
                    break;
                }
            }
            if (match) return true;
        }
        if (!sl) break;
        s = sl + 1;
    }
    return false;
}

static uint64_t classify_entries(struct zip_archive *z, const char *basedir, const char *gamedir,
                                 unsigned *total_files, unsigned *skipped_files)
{
    size_t blen = strlen(basedir), glen = strlen(gamedir);
    bool flat = (strcmp(basedir, gamedir) == 0);
    char gseg[256] = "";
    if (!flat) {
        snprintf(gseg, sizeof gseg, "%s", gamedir + blen);
        size_t gn = strlen(gseg);
        while (gn > 0 && gseg[gn - 1] == '/') gseg[--gn] = '\0';
    }

    uint64_t total_bytes = 0;
    *total_files = 0;
    *skipped_files = 0;

    for (size_t i = 0; i < z->count; i++) {
        struct zip_entry *e = &z->entries[i];
        e->action = ACT_SKIP;
        if (e->is_dir) continue;
        if (e->is_junk || e->is_symlink) {
            (*skipped_files)++;
            continue;
        }
        if (glen > 0 && starts_with(e->name, gamedir, glen)) {
            const char *rel = e->name + glen;
            size_t rlen = e->name_len - glen;
            if (rlen == 0) continue;
            if (starts_with(rel, "cache/", 6) || starts_with(rel, "saves/", 6) ||
                has_segment_ci(rel, ".vscode")) {
                (*skipped_files)++;
                continue;
            }
            if (flat && (starts_with(rel, "renpy/", 6) || starts_with(rel, "lib/", 4) ||
                         ends_with_ci(rel, rlen, ".exe") || ends_with_ci(rel, rlen, ".sh") ||
                         ends_with_ci(rel, rlen, ".py") || ends_with_ci(rel, rlen, ".dll") ||
                         ends_with_ci(rel, rlen, ".so") || ends_with_ci(rel, rlen, ".dylib") ||
                         strstr(rel, ".app/") != NULL)) {
                (*skipped_files)++;
                continue;
            }
            e->rel_offset = glen;
            if (!e->is_ascii) {
                if (is_non_packable_script(rel, rlen)) {
                    (*skipped_files)++;
                    continue;
                }
                e->action = ACT_PACK;
            } else {
                e->action = ACT_EXTRACT_GAME;
            }
            total_bytes += e->uncomp_size;
            (*total_files)++;
        } else if (!flat && (blen == 0 || starts_with(e->name, basedir, blen))) {
            const char *rel = e->name + blen;
            size_t rlen = e->name_len - blen;
            if (rlen == 0 || is_desktop_sibling_skip(rel, rlen, gseg) || !e->is_ascii) {
                (*skipped_files)++;
                continue;
            }
            e->rel_offset = blen;
            e->action = ACT_EXTRACT_ROOT;
            total_bytes += e->uncomp_size;
            (*total_files)++;
        }
    }
    return total_bytes;
}

/* ----------------------------------------------------- RPA-3.0 writer */

static void bb_put(struct byte_buf *b, const void *p, size_t n)
{
    if (b->oom) return;
    if (b->len + n > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 4096;
        while (nc < b->len + n) nc *= 2;
        unsigned char *nd = realloc(b->data, nc);
        if (!nd) { b->oom = true; return; }
        b->data = nd;
        b->cap = nc;
    }
    memcpy(b->data + b->len, p, n);
    b->len += n;
}

static void bb_u8(struct byte_buf *b, unsigned char v) { bb_put(b, &v, 1); }

static void bb_le32(struct byte_buf *b, uint32_t v)
{
    unsigned char d[4] = {
        (unsigned char)(v & 0xff), (unsigned char)((v >> 8) & 0xff),
        (unsigned char)((v >> 16) & 0xff), (unsigned char)((v >> 24) & 0xff),
    };
    bb_put(b, d, 4);
}

static void pickle_uint64(struct byte_buf *b, uint64_t v)
{
    if (v <= 0x7fffffffULL) {
        bb_u8(b, 0x4a); /* BININT */
        bb_le32(b, (uint32_t)v);
        return;
    }
    unsigned char raw[9];
    size_t n = 0;
    uint64_t t = v;
    while (t > 0) {
        raw[n++] = (unsigned char)(t & 0xff);
        t >>= 8;
    }
    if (raw[n - 1] >= 0x80)
        raw[n++] = 0x00; /* two's complement sign guard */
    bb_u8(b, 0x8a); /* LONG1 */
    bb_u8(b, (unsigned char)n);
    bb_put(b, raw, n);
}

static bool rpa_open(struct rpa_writer *w, const char *game_dir, int index)
{
    memset(w, 0, sizeof *w);
    w->index = index;
    if (index <= 1)
        snprintf(w->path, sizeof w->path, "%s/rsl-nonascii.rpa", game_dir);
    else
        snprintf(w->path, sizeof w->path, "%s/rsl-nonascii-%d.rpa", game_dir, index);
    w->f = fopen(w->path, "wb");
    if (!w->f) return false;
    char hdr[40];
    int hn = snprintf(hdr, sizeof hdr, "RPA-3.0 %016llx %08x\n", 0ULL, 0u);
    if (hn != 34 || fwrite(hdr, 1, 34, w->f) != 34) {
        fclose(w->f);
        w->f = NULL;
        return false;
    }
    w->pos = 34;
    return true;
}

static bool rpa_add_begin(struct rpa_writer *w, const char *rel, uint64_t *offset_out)
{
    if (w->count == w->cap) {
        size_t nc = w->cap ? w->cap * 2 : 64;
        struct rpa_member *nm = realloc(w->members, nc * sizeof *nm);
        if (!nm) return false;
        w->members = nm;
        w->cap = nc;
    }
    char *copy = strdup(rel);
    if (!copy) return false;
    *offset_out = w->pos;
    w->members[w->count].name = copy;
    w->members[w->count].offset = w->pos;
    w->members[w->count].length = 0;
    return true;
}

static void rpa_add_finish(struct rpa_writer *w, uint64_t written)
{
    w->members[w->count].length = written;
    w->pos += written;
    w->count++;
}

static void rpa_abort(struct rpa_writer *w)
{
    if (w->f) {
        fclose(w->f);
        w->f = NULL;
    }
    for (size_t i = 0; i < w->count; i++)
        free(w->members[i].name);
    free(w->members);
    w->members = NULL;
    w->count = w->cap = 0;
}

static bool rpa_close(struct rpa_writer *w)
{
    if (!w->f) return true;
    bool ok = false;
    struct byte_buf pb = { 0 };
    uLongf zlen = 0;
    unsigned char *zbuf = NULL;

    bb_u8(&pb, 0x80); bb_u8(&pb, 0x02); /* PROTO 2 */
    bb_u8(&pb, 0x7d);                   /* EMPTY_DICT */
    bb_u8(&pb, 0x28);                   /* MARK */
    for (size_t i = 0; i < w->count && !pb.oom; i++) {
        size_t slen = strlen(w->members[i].name);
        bb_u8(&pb, 0x58); /* BINUNICODE */
        bb_le32(&pb, (uint32_t)slen);
        bb_put(&pb, w->members[i].name, slen);
        bb_u8(&pb, 0x5d); /* EMPTY_LIST */
        pickle_uint64(&pb, w->members[i].offset);
        pickle_uint64(&pb, w->members[i].length);
        bb_u8(&pb, 0x86); /* TUPLE2 */
        bb_u8(&pb, 0x61); /* APPEND */
    }
    bb_u8(&pb, 0x75); /* SETITEMS */
    bb_u8(&pb, 0x2e); /* STOP */
    if (pb.oom) goto done;

    zlen = compressBound((uLong)pb.len);
    zbuf = malloc(zlen);
    if (!zbuf) goto done;
    if (compress2(zbuf, &zlen, pb.data, (uLong)pb.len, 6) != Z_OK) goto done;

    uint64_t idx_off = w->pos;
    if (fwrite(zbuf, 1, zlen, w->f) != zlen || fflush(w->f) != 0) goto done;
    if (seek_u64(w->f, 0) != 0) goto done;
    char hdr[40];
    int hn = snprintf(hdr, sizeof hdr, "RPA-3.0 %016llx %08x\n", (unsigned long long)idx_off, 0u);
    if (hn != 34 || fwrite(hdr, 1, 34, w->f) != 34) goto done;
    if (fclose(w->f) == 0) {
        w->f = NULL;
        ok = true;
    }

done:
    if (w->f) {
        fclose(w->f);
        w->f = NULL;
    }
    free(zbuf);
    free(pb.data);
    for (size_t i = 0; i < w->count; i++)
        free(w->members[i].name);
    free(w->members);
    w->members = NULL;
    w->count = w->cap = 0;
    return ok;
}

/* ----------------------------------------------------- streaming unpack */

static void apply_dos_mtime(const char *path, uint16_t dos_date, uint16_t dos_time)
{
    int year = 1980 + ((dos_date >> 9) & 0x7f);
    int mon = (dos_date >> 5) & 0x0f;
    int day = dos_date & 0x1f;
    if (mon < 1 || mon > 12 || day < 1 || day > 31)
        return;
    struct tm tm = { 0 };
    tm.tm_year = year - 1900;
    tm.tm_mon = mon - 1;
    tm.tm_mday = day;
    tm.tm_hour = (dos_time >> 11) & 0x1f;
    tm.tm_min = (dos_time >> 5) & 0x3f;
    tm.tm_sec = (dos_time & 0x1f) * 2;
    tm.tm_isdst = -1;
    time_t t = mktime(&tm);
    if (t != (time_t)-1) {
        struct utimbuf ub = { .actime = t, .modtime = t };
        utime(path, &ub);
    }
}

static bool stream_entry(const struct zip_archive *z, const struct zip_entry *e,
                         FILE *dst, unsigned char *in_buf, unsigned char *out_buf,
                         volatile bool *cancel, rsl_zip_progress *prog,
                         rsl_zip_progress_cb cb, void *ctx, uint64_t *last_cb_ms,
                         char *err, size_t errlen)
{
    if (e->method != 0 && e->method != 8) {
        snprintf(err, errlen, "不支持的 ZIP 压缩方式 (%u): %s", e->method, e->name);
        return false;
    }
    uint64_t data_off = 0;
    if (!zip_local_data_offset(z->f, z->file_size, e->local_offset, &data_off) ||
        data_off + e->comp_size > z->file_size || seek_u64(z->f, data_off) != 0) {
        snprintf(err, errlen, "读取条目数据失败: %s", e->name);
        return false;
    }

    uLong crc = crc32(0L, Z_NULL, 0);
    uint64_t written = 0;

    if (e->method == 0) {
        uint64_t left = e->comp_size;
        while (left > 0) {
            if (cancel && *cancel) {
                snprintf(err, errlen, "已取消");
                return false;
            }
            size_t want = left > IO_BUF_SIZE ? IO_BUF_SIZE : (size_t)left;
            size_t nr = fread(in_buf, 1, want, z->f);
            if (nr != want) {
                snprintf(err, errlen, "读取 ZIP 数据中断: %s", e->name);
                return false;
            }
            if (fwrite(in_buf, 1, nr, dst) != nr) {
                snprintf(err, errlen, "写入 SD 卡失败（空间不足？）: %s", e->name);
                return false;
            }
            crc = crc32(crc, in_buf, (uInt)nr);
            left -= nr;
            written += nr;
            prog->done_bytes += nr;
            if (cb) {
                uint64_t now = monotonic_ms();
                if (now - *last_cb_ms >= 100) {
                    *last_cb_ms = now;
                    cb(prog, ctx);
                }
            }
        }
    } else {
        z_stream zs = { 0 };
        if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) {
            snprintf(err, errlen, "zlib 初始化失败");
            return false;
        }
        uint64_t left = e->comp_size;
        bool stream_end = false;
        while (!stream_end) {
            if (cancel && *cancel) {
                inflateEnd(&zs);
                snprintf(err, errlen, "已取消");
                return false;
            }
            if (zs.avail_in == 0 && left > 0) {
                size_t want = left > IO_BUF_SIZE ? IO_BUF_SIZE : (size_t)left;
                size_t nr = fread(in_buf, 1, want, z->f);
                if (nr != want) {
                    inflateEnd(&zs);
                    snprintf(err, errlen, "读取压缩数据失败: %s", e->name);
                    return false;
                }
                left -= nr;
                zs.next_in = in_buf;
                zs.avail_in = (uInt)nr;
            }
            zs.next_out = out_buf;
            zs.avail_out = IO_BUF_SIZE;
            int r = inflate(&zs, Z_NO_FLUSH);
            size_t have = IO_BUF_SIZE - zs.avail_out;
            if (have > 0) {
                if (fwrite(out_buf, 1, have, dst) != have) {
                    inflateEnd(&zs);
                    snprintf(err, errlen, "写入 SD 卡失败（空间不足？）: %s", e->name);
                    return false;
                }
                crc = crc32(crc, out_buf, (uInt)have);
                written += have;
                prog->done_bytes += have;
                if (cb) {
                    uint64_t now = monotonic_ms();
                    if (now - *last_cb_ms >= 100) {
                        *last_cb_ms = now;
                        cb(prog, ctx);
                    }
                }
            }
            if (r == Z_STREAM_END) {
                stream_end = true;
            } else if (r != Z_OK && r != Z_BUF_ERROR) {
                inflateEnd(&zs);
                snprintf(err, errlen, "ZIP 解压校验错误: %s", e->name);
                return false;
            } else if (r == Z_BUF_ERROR && zs.avail_in == 0 && left == 0) {
                break;
            }
        }
        inflateEnd(&zs);
    }

    if (written != e->uncomp_size) {
        snprintf(err, errlen, "文件大小不匹配: %s", e->name);
        return false;
    }
    if ((e->crc32 != 0 || e->uncomp_size != 0) && crc != e->crc32) {
        snprintf(err, errlen, "CRC32 校验失败: %s", e->name);
        return false;
    }
    return true;
}

static bool write_manifest_file(const char *stage_dir, const char *zip_path, const rsl_zip_result *res,
                                bool has_cover, char *err, size_t errlen)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        snprintf(err, errlen, "内存不足");
        return false;
    }
    const char *sl = strrchr(zip_path, '/');
#ifdef _WIN32
    const char *bsl = strrchr(zip_path, '\\');
    if (bsl && (!sl || bsl > sl)) sl = bsl;
#endif
    const char *zip_name = sl ? sl + 1 : zip_path;

    cJSON_AddStringToObject(root, "format", "renpy-switch-loader/manifest");
    cJSON_AddNumberToObject(root, "version", 1);
    cJSON_AddStringToObject(root, "id", res->game_id);
    cJSON_AddStringToObject(root, "title", res->title);
    if (has_cover) {
        cJSON_AddStringToObject(root, "cover", "cover.bmp");
        cJSON_AddTrueToObject(root, "cover_auto_tried");
        cJSON_AddNumberToObject(root, "cover_auto_ver", 4);
    }
    if (res->renpy_version[0])
        cJSON_AddStringToObject(root, "renpy_version", res->renpy_version);
    else
        cJSON_AddNullToObject(root, "renpy_version");
    cJSON_AddStringToObject(root, "python", res->python_gen == 2 ? "2" : "3");
    cJSON_AddStringToObject(root, "runtime", res->runtime);
    cJSON_AddStringToObject(root, "status", "experimental");
    cJSON_AddItemToObject(root, "reasons", cJSON_CreateArray());
    cJSON_AddItemToObject(root, "warnings", cJSON_CreateArray());
    cJSON_AddFalseToObject(root, "recompile");

    cJSON *perf = cJSON_CreateObject();
    cJSON_AddItemToObject(perf, "issues", cJSON_CreateArray());
    cJSON_AddItemToObject(root, "performance", perf);

    cJSON *src = cJSON_CreateObject();
    cJSON_AddStringToObject(src, "type", "zip");
    cJSON_AddStringToObject(src, "name", zip_name);
    cJSON_AddItemToObject(root, "source", src);

    char *text = cJSON_Print(root);
    cJSON_Delete(root);
    if (!text) {
        snprintf(err, errlen, "内存不足");
        return false;
    }
    char path[1024];
    snprintf(path, sizeof path, "%s/manifest.json", stage_dir);
    bool ok = rsl_write_atomic(path, text, strlen(text), err, errlen);
    free(text);
    return ok;
}

bool rsl_zip_unpack(const char *zip_path, const char *games_dir, bool delete_zip,
                    volatile bool *cancel, rsl_zip_progress_cb cb, void *ctx,
                    rsl_zip_result *out)
{
    struct zip_archive z = { 0 };
    struct rpa_writer rpa = { 0 };
    unsigned char *in_buf = NULL, *out_buf = NULL;
    char stage[1024] = "", dest[1024] = "", stage_game[1024] = "";
    bool ok = false;

    rsl_zip_result res = { 0 };
    rsl_zip_progress prog = { 0 };
    uint64_t last_cb_ms = 0;

    snprintf(prog.stage, sizeof prog.stage, "正在分析压缩包");
    if (cb) cb(&prog, ctx);

    if (!zip_open_archive(zip_path, &z, res.error, sizeof res.error))
        goto done;

    char basedir[1024] = "", gamedir[1024] = "";
    if (!find_gamedir_zip(&z, "", 0, basedir, sizeof basedir, gamedir, sizeof gamedir)) {
        snprintf(res.error, sizeof res.error, "压缩包中未找到 Ren'Py 游戏目录（需要含 .rpy/.rpyc/.rpa 的 game/）");
        goto done;
    }

    /* Ensure gamedir has at least one script/rpa file */
    bool has_any_script = false;
    size_t glen = strlen(gamedir);
    for (size_t i = 0; i < z.count; i++) {
        const struct zip_entry *e = &z.entries[i];
        if (e->is_dir || e->is_junk || e->is_symlink) continue;
        if (glen > 0 && !starts_with(e->name, gamedir, glen)) continue;
        if (is_script_ext(e->name, e->name_len)) {
            has_any_script = true;
            break;
        }
    }
    if (!has_any_script) {
        snprintf(res.error, sizeof res.error, "游戏目录中没有 Ren'Py 脚本（.rpy/.rpyc/.rpa）");
        goto done;
    }

    detect_version_and_python(&z, basedir, gamedir, res.renpy_version, sizeof res.renpy_version, &res.python_gen);
    snprintf(res.runtime, sizeof res.runtime, "%s", res.python_gen == 2 ? "renpy-7.8.7" : "renpy-8.5.3");
    detect_title_and_id(&z, zip_path, games_dir, basedir, gamedir,
                        res.title, sizeof res.title, res.game_id, sizeof res.game_id);

    prog.total_bytes = classify_entries(&z, basedir, gamedir, &prog.total_files, &res.skipped_files);
    if (prog.total_files == 0) {
        snprintf(res.error, sizeof res.error, "没有可提取的游戏文件");
        goto done;
    }

    snprintf(stage, sizeof stage, "%s/.unpacking-%s", games_dir, res.game_id);
    snprintf(stage_game, sizeof stage_game, "%s/game", stage);
    snprintf(dest, sizeof dest, "%s/%s", games_dir, res.game_id);
    rm_rf(stage);
    if (!rsl_mkdirs(stage_game)) {
        snprintf(res.error, sizeof res.error, "无法创建临时目录 %s", stage_game);
        goto done;
    }

    in_buf = malloc(IO_BUF_SIZE);
    out_buf = malloc(IO_BUF_SIZE);
    if (!in_buf || !out_buf) {
        snprintf(res.error, sizeof res.error, "内存不足");
        goto done;
    }

    snprintf(prog.stage, sizeof prog.stage, "正在解压游戏文件");
    if (cb) cb(&prog, ctx);

    for (size_t i = 0; i < z.count; i++) {
        if (cancel && *cancel) {
            snprintf(res.error, sizeof res.error, "已取消");
            goto done;
        }
        const struct zip_entry *e = &z.entries[i];
        if (e->action == ACT_SKIP) continue;
        const char *rel = e->name + e->rel_offset;
        snprintf(prog.current_file, sizeof prog.current_file, "%s", rel);
        if (cb) cb(&prog, ctx);

        if (e->action == ACT_PACK) {
            snprintf(prog.stage, sizeof prog.stage, "正在打包非 ASCII 资源");
            if (!ensure_ascii_parent_dirs(stage_game, rel)) {
                snprintf(res.error, sizeof res.error, "创建目录失败: %s", rel);
                goto done;
            }
            if (rpa.f && (rpa.pos + e->uncomp_size > RPA_SPLIT_MAX)) {
                int next_idx = rpa.index + 1;
                if (!rpa_close(&rpa)) {
                    snprintf(res.error, sizeof res.error, "写入 RPA 索引失败");
                    goto done;
                }
                if (!rpa_open(&rpa, stage_game, next_idx)) {
                    snprintf(res.error, sizeof res.error, "创建分卷 RPA 失败");
                    goto done;
                }
            }
            if (!rpa.f && !rpa_open(&rpa, stage_game, 1)) {
                snprintf(res.error, sizeof res.error, "创建 rsl-nonascii.rpa 失败");
                goto done;
            }
            uint64_t start_off = 0;
            if (!rpa_add_begin(&rpa, rel, &start_off)) {
                snprintf(res.error, sizeof res.error, "内存不足");
                goto done;
            }
            if (!stream_entry(&z, e, rpa.f, in_buf, out_buf, cancel, &prog, cb, ctx,
                              &last_cb_ms, res.error, sizeof res.error))
                goto done;
            rpa_add_finish(&rpa, e->uncomp_size);
            res.packed_files++;
        } else {
            snprintf(prog.stage, sizeof prog.stage, "正在解压游戏文件");
            char target[1024];
            int tn = snprintf(target, sizeof target, "%s/%s",
                              e->action == ACT_EXTRACT_GAME ? stage_game : stage, rel);
            if (tn < 0 || tn >= (int)sizeof target || !ensure_parent_dirs(target)) {
                snprintf(res.error, sizeof res.error, "目标路径无效: %s", rel);
                goto done;
            }
            FILE *dst = fopen(target, "wb");
            if (!dst) {
                snprintf(res.error, sizeof res.error, "无法写入 %s: %s", rel, strerror(errno));
                goto done;
            }
            setvbuf(dst, NULL, _IOFBF, IO_BUF_SIZE);
            bool sok = stream_entry(&z, e, dst, in_buf, out_buf, cancel, &prog, cb, ctx,
                                    &last_cb_ms, res.error, sizeof res.error);
            if (fclose(dst) != 0 && sok) {
                sok = false;
                snprintf(res.error, sizeof res.error, "关闭文件失败: %s", rel);
            }
            if (!sok) goto done;
            apply_dos_mtime(target, e->dos_date, e->dos_time);
            res.extracted_files++;
        }
        prog.done_files++;
    }

    if (rpa.f && !rpa_close(&rpa)) {
        snprintf(res.error, sizeof res.error, "完成 rsl-nonascii.rpa 索引失败");
        goto done;
    }

    snprintf(prog.stage, sizeof prog.stage, "正在提取游戏封面");
    prog.current_file[0] = '\0';
    if (cb) cb(&prog, ctx);

    char cover_bmp[1024];
    snprintf(cover_bmp, sizeof cover_bmp, "%s/cover.bmp", stage);
    bool has_cover = rsl_extract_game_cover(stage, cover_bmp);

    snprintf(prog.stage, sizeof prog.stage, "正在生成配置清单");
    if (cb) cb(&prog, ctx);

    if (!write_manifest_file(stage, zip_path, &res, has_cover, res.error, sizeof res.error))
        goto done;

    /* Close zip before deleting or renaming */
    zip_close_archive(&z);

    if (rename(stage, dest) != 0) {
        snprintf(res.error, sizeof res.error, "移动游戏目录失败: %s", strerror(errno));
        goto done;
    }
    stage[0] = '\0'; /* committed */

    if (delete_zip)
        remove(zip_path);
    ok = true;

done:
    rpa_abort(&rpa);
    zip_close_archive(&z);
    free(in_buf);
    free(out_buf);
    if (!ok && stage[0])
        rm_rf(stage);
    if (out)
        *out = res;
    return ok;
}
