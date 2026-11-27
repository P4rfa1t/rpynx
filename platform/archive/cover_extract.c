#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L

#include "cover_extract.h"

#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>

#ifndef RSL_COVER_NO_DECODE
#include <png.h>
#include <jpeglib.h>
#include <webp/decode.h>
#endif

#include "fs.h"

#define COVER_W 300
#define COVER_H 450
#define MAX_RPA_IDX (16u << 20)
#define MAX_ART_BYTES (16u << 20)
#define MAX_PICKLE_STACK 4096
#define MAX_PICKLE_MEMO  65536

/* ----------------------------------------------------------- string utils */

static bool eq_ci_s(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return false;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

static bool starts_ci(const char *s, const char *p)
{
    while (*p) {
        if (tolower((unsigned char)*s) != tolower((unsigned char)*p))
            return false;
        s++; p++;
    }
    return true;
}

static bool ends_ci(const char *s, const char *suf)
{
    size_t sl = strlen(s), nl = strlen(suf);
    if (sl < nl) return false;
    return eq_ci_s(s + sl - nl, suf);
}

static bool contains_ci(const char *s, const char *sub)
{
    size_t nl = strlen(sub);
    if (!nl) return true;
    for (const char *p = s; *p; p++) {
        size_t i = 0;
        while (i < nl && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)sub[i]))
            i++;
        if (i == nl) return true;
    }
    return false;
}

static bool is_image_name(const char *rel)
{
    return ends_ci(rel, ".png") || ends_ci(rel, ".jpg") ||
           ends_ci(rel, ".jpeg") || ends_ci(rel, ".webp");
}

static bool is_ui_chrome(const char *rel)
{
    static const char *const skip_dirs[] = {
        "overlay/", "button/", "buttons/", "scrollbar/", "slider/", "bar/", "phone/",
        "/overlay/", "/button/", "/buttons/", "/scrollbar/", "/slider/", "/bar/", "/phone/"
    };
    for (size_t i = 0; i < sizeof skip_dirs / sizeof skip_dirs[0]; i++)
        if (contains_ci(rel, skip_dirs[i]))
            return true;
    static const char *const skip_words[] = {
        "hover", "idle", "selected", "locked", "ctc", "namebox", "notify", "nvl", "frame"
    };
    for (size_t i = 0; i < sizeof skip_words / sizeof skip_words[0]; i++)
        if (contains_ci(rel, skip_words[i]))
            return true;
    const char *base = strrchr(rel, '/');
    base = base ? base + 1 : rel;
    if (starts_ci(base, "end."))
        return true;
    return false;
}

static uint64_t rd_le_u64(const unsigned char *p, size_t n)
{
    uint64_t v = 0;
    for (size_t i = 0; i < n && i < 8; i++)
        v |= (uint64_t)p[i] << (8 * i);
    return v;
}

/* ----------------------------------------------------- safe RPA pickle VM */

enum pkind {
    PK_NONE, PK_BOOL, PK_INT, PK_STR, PK_BYTES, PK_LIST, PK_TUPLE, PK_DICT, PK_MARK
};

struct pval {
    enum pkind kind;
    union {
        int64_t iv;
        struct { char *s; size_t len; } str;
        struct { struct pval **items; size_t count, cap; } seq;
        struct { char **keys; struct pval **vals; size_t count, cap; } dict;
    } u;
};

static struct pval *pv_new(enum pkind k)
{
    struct pval *v = calloc(1, sizeof *v);
    if (v) v->kind = k;
    return v;
}

static void pv_free(struct pval *v)
{
    if (!v) return;
    if (v->kind == PK_STR || v->kind == PK_BYTES) {
        free(v->u.str.s);
    } else if (v->kind == PK_LIST || v->kind == PK_TUPLE) {
        for (size_t i = 0; i < v->u.seq.count; i++)
            pv_free(v->u.seq.items[i]);
        free(v->u.seq.items);
    } else if (v->kind == PK_DICT) {
        for (size_t i = 0; i < v->u.dict.count; i++) {
            free(v->u.dict.keys[i]);
            pv_free(v->u.dict.vals[i]);
        }
        free(v->u.dict.keys);
        free(v->u.dict.vals);
    }
    free(v);
}

static bool pv_seq_push(struct pval *seq, struct pval *item)
{
    if (seq->u.seq.count == seq->u.seq.cap) {
        size_t nc = seq->u.seq.cap ? seq->u.seq.cap * 2 : 4;
        struct pval **ni = realloc(seq->u.seq.items, nc * sizeof *ni);
        if (!ni) { pv_free(item); return false; }
        seq->u.seq.items = ni;
        seq->u.seq.cap = nc;
    }
    seq->u.seq.items[seq->u.seq.count++] = item;
    return true;
}

static void pv_dict_set(struct pval *d, struct pval *k, struct pval *val)
{
    if (!d || d->kind != PK_DICT || !k || (k->kind != PK_STR && k->kind != PK_BYTES) || !val) {
        pv_free(k);
        pv_free(val);
        return;
    }
    /* Optimize for building cover candidate index: we only need RPA tuples/dicts of
     * dicts/lists/tuples/ints/strings; drop non-image keys early if desired or keep small. */
    if (d->u.dict.count == d->u.dict.cap) {
        size_t nc = d->u.dict.cap ? d->u.dict.cap * 2 : 16;
        char **nk = realloc(d->u.dict.keys, nc * sizeof *nk);
        struct pval **nv = realloc(d->u.dict.vals, nc * sizeof *nv);
        if (!nk || !nv) {
            if (nk) d->u.dict.keys = nk;
            if (nv) d->u.dict.vals = nv;
            pv_free(k); pv_free(val);
            return;
        }
        d->u.dict.keys = nk;
        d->u.dict.vals = nv;
        d->u.dict.cap = nc;
    }
    char *ks = malloc(k->u.str.len + 1);
    if (!ks) { pv_free(k); pv_free(val); return; }
    memcpy(ks, k->u.str.s, k->u.str.len);
    ks[k->u.str.len] = '\0';
    pv_free(k);
    d->u.dict.keys[d->u.dict.count] = ks;
    d->u.dict.vals[d->u.dict.count] = val;
    d->u.dict.count++;
}

static struct pval *pv_clone(const struct pval *v)
{
    if (!v) return NULL;
    struct pval *c = pv_new(v->kind);
    if (!c) return NULL;
    if (v->kind == PK_INT || v->kind == PK_BOOL) {
        c->u.iv = v->u.iv;
    } else if (v->kind == PK_STR || v->kind == PK_BYTES) {
        c->u.str.s = malloc(v->u.str.len + 1);
        if (!c->u.str.s) { free(c); return NULL; }
        memcpy(c->u.str.s, v->u.str.s, v->u.str.len);
        c->u.str.s[v->u.str.len] = '\0';
        c->u.str.len = v->u.str.len;
    } else if (v->kind == PK_LIST || v->kind == PK_TUPLE) {
        for (size_t i = 0; i < v->u.seq.count; i++)
            pv_seq_push(c, pv_clone(v->u.seq.items[i]));
    } else if (v->kind == PK_DICT) {
        for (size_t i = 0; i < v->u.dict.count; i++) {
            struct pval *k = pv_new(PK_STR);
            if (!k) break;
            size_t kl = strlen(v->u.dict.keys[i]);
            k->u.str.s = malloc(kl + 1);
            if (!k->u.str.s) { free(k); break; }
            memcpy(k->u.str.s, v->u.dict.keys[i], kl + 1);
            k->u.str.len = kl;
            pv_dict_set(c, k, pv_clone(v->u.dict.vals[i]));
        }
    }
    return c;
}

static struct pval *parse_plain_pickle(const unsigned char *buf, size_t len)
{
    struct pval *stack[MAX_PICKLE_STACK];
    size_t sp = 0;
    struct pval **memo = calloc(MAX_PICKLE_MEMO, sizeof *memo);
    if (!memo) return NULL;
    size_t memo_next = 0;
    size_t p = 0;
    bool stopped = false;

    #define FAIL_PK() do { goto fail; } while (0)
    #define PUSH(v) do { if (sp >= MAX_PICKLE_STACK || !(v)) FAIL_PK(); stack[sp++] = (v); } while (0)
    #define POP(dst) do { if (sp == 0) FAIL_PK(); (dst) = stack[--sp]; } while (0)
    #define NEED(k) do { if (p + (k) > len) FAIL_PK(); } while (0)

    while (p < len && !stopped) {
        unsigned char op = buf[p++];
        switch (op) {
        case 0x80: /* PROTO */
            NEED(1); p++;
            break;
        case 0x95: /* FRAME */
            NEED(8); p += 8;
            break;
        case '.': /* STOP */
            stopped = true;
            break;
        case '(': /* MARK */
            PUSH(pv_new(PK_MARK));
            break;
        case 'N': /* NONE */
            PUSH(pv_new(PK_NONE));
            break;
        case 0x88: { /* NEWTRUE */
            struct pval *v = pv_new(PK_BOOL);
            if (v) v->u.iv = 1;
            PUSH(v);
            break;
        }
        case 0x89: { /* NEWFALSE */
            struct pval *v = pv_new(PK_BOOL);
            if (v) v->u.iv = 0;
            PUSH(v);
            break;
        }
        case '}': /* EMPTY_DICT */
            PUSH(pv_new(PK_DICT));
            break;
        case ']': /* EMPTY_LIST */
            PUSH(pv_new(PK_LIST));
            break;
        case ')': /* EMPTY_TUPLE */
            PUSH(pv_new(PK_TUPLE));
            break;
        case 'K': { /* BININT1 */
            NEED(1);
            struct pval *v = pv_new(PK_INT);
            if (v) v->u.iv = buf[p++];
            PUSH(v);
            break;
        }
        case 'M': { /* BININT2 */
            NEED(2);
            struct pval *v = pv_new(PK_INT);
            if (v) v->u.iv = (int64_t)rd_le_u64(buf + p, 2);
            p += 2;
            PUSH(v);
            break;
        }
        case 'J': { /* BININT */
            NEED(4);
            struct pval *v = pv_new(PK_INT);
            if (v) v->u.iv = (int32_t)rd_le_u64(buf + p, 4);
            p += 4;
            PUSH(v);
            break;
        }
        case 0x8a: { /* LONG1 */
            NEED(1);
            unsigned char nb = buf[p++];
            NEED(nb);
            int64_t val = 0;
            if (nb > 0 && nb <= 8) {
                uint64_t uv = rd_le_u64(buf + p, nb);
                if (nb < 8 && (buf[p + nb - 1] & 0x80))
                    uv |= (~0ULL) << (nb * 8);
                val = (int64_t)uv;
            }
            p += nb;
            struct pval *v = pv_new(PK_INT);
            if (v) v->u.iv = val;
            PUSH(v);
            break;
        }
        case 0x8b: { /* LONG4 */
            NEED(4);
            uint32_t nb = (uint32_t)rd_le_u64(buf + p, 4);
            p += 4;
            NEED(nb);
            int64_t val = 0;
            if (nb > 0 && nb <= 8)
                val = (int64_t)rd_le_u64(buf + p, nb);
            p += nb;
            struct pval *v = pv_new(PK_INT);
            if (v) v->u.iv = val;
            PUSH(v);
            break;
        }
        case 'I': case 'L': { /* INT / LONG ASCII line */
            size_t start = p;
            while (p < len && buf[p] != '\n') p++;
            if (p >= len) FAIL_PK();
            char tmp[64];
            size_t nl = p - start;
            if (nl >= sizeof tmp) nl = sizeof tmp - 1;
            memcpy(tmp, buf + start, nl);
            tmp[nl] = '\0';
            p++;
            struct pval *v = pv_new(PK_INT);
            if (v) v->u.iv = strtoll(tmp, NULL, 10);
            PUSH(v);
            break;
        }
        case 0x8c: case 'C': case 'U': { /* SHORT_BINUNICODE / SHORT_BINBYTES / SHORT_BINSTRING */
            NEED(1);
            size_t sl = buf[p++];
            NEED(sl);
            struct pval *v = pv_new(op == 'C' ? PK_BYTES : PK_STR);
            if (!v) FAIL_PK();
            v->u.str.s = malloc(sl + 1);
            if (!v->u.str.s) { free(v); FAIL_PK(); }
            memcpy(v->u.str.s, buf + p, sl);
            v->u.str.s[sl] = '\0';
            v->u.str.len = sl;
            p += sl;
            PUSH(v);
            break;
        }
        case 'X': case 'B': case 'T': { /* BINUNICODE / BINBYTES / BINSTRING */
            NEED(4);
            size_t sl = (size_t)rd_le_u64(buf + p, 4);
            p += 4;
            NEED(sl);
            struct pval *v = pv_new(op == 'B' ? PK_BYTES : PK_STR);
            if (!v) FAIL_PK();
            v->u.str.s = malloc(sl + 1);
            if (!v->u.str.s) { free(v); FAIL_PK(); }
            memcpy(v->u.str.s, buf + p, sl);
            v->u.str.s[sl] = '\0';
            v->u.str.len = sl;
            p += sl;
            PUSH(v);
            break;
        }
        case 0x8d: case 0x8e: { /* BINUNICODE8 / BINBYTES8 */
            NEED(8);
            uint64_t sl64 = rd_le_u64(buf + p, 8);
            p += 8;
            if (sl64 > len - p) FAIL_PK();
            size_t sl = (size_t)sl64;
            struct pval *v = pv_new(op == 0x8e ? PK_BYTES : PK_STR);
            if (!v) FAIL_PK();
            v->u.str.s = malloc(sl + 1);
            if (!v->u.str.s) { free(v); FAIL_PK(); }
            memcpy(v->u.str.s, buf + p, sl);
            v->u.str.s[sl] = '\0';
            v->u.str.len = sl;
            p += sl;
            PUSH(v);
            break;
        }
        case 'V': case 'S': { /* UNICODE / STRING line */
            size_t start = p;
            while (p < len && buf[p] != '\n') p++;
            if (p >= len) FAIL_PK();
            size_t sl = p - start;
            if (op == 'S' && sl >= 2 && (buf[start] == '\'' || buf[start] == '"')) {
                start++; sl -= 2;
            }
            struct pval *v = pv_new(PK_STR);
            if (!v) FAIL_PK();
            v->u.str.s = malloc(sl + 1);
            if (!v->u.str.s) { free(v); FAIL_PK(); }
            memcpy(v->u.str.s, buf + start, sl);
            v->u.str.s[sl] = '\0';
            v->u.str.len = sl;
            p++;
            PUSH(v);
            break;
        }
        case 0x85: case 0x86: case 0x87: { /* TUPLE1 / TUPLE2 / TUPLE3 */
            size_t k = (size_t)(op - 0x85 + 1);
            if (sp < k) FAIL_PK();
            struct pval *t = pv_new(PK_TUPLE);
            if (!t) FAIL_PK();
            for (size_t i = 0; i < k; i++)
                pv_seq_push(t, stack[sp - k + i]);
            sp -= k;
            PUSH(t);
            break;
        }
        case 't': case 'l': { /* TUPLE / LIST to MARK */
            size_t mk = sp;
            while (mk > 0 && stack[mk - 1]->kind != PK_MARK) mk--;
            if (mk == 0) FAIL_PK();
            struct pval *seq = pv_new(op == 't' ? PK_TUPLE : PK_LIST);
            if (!seq) FAIL_PK();
            for (size_t i = mk; i < sp; i++)
                pv_seq_push(seq, stack[i]);
            pv_free(stack[mk - 1]);
            sp = mk - 1;
            PUSH(seq);
            break;
        }
        case 'd': { /* DICT to MARK */
            size_t mk = sp;
            while (mk > 0 && stack[mk - 1]->kind != PK_MARK) mk--;
            if (mk == 0 || ((sp - mk) & 1)) FAIL_PK();
            struct pval *d = pv_new(PK_DICT);
            if (!d) FAIL_PK();
            for (size_t i = mk; i + 1 < sp; i += 2)
                pv_dict_set(d, stack[i], stack[i + 1]);
            pv_free(stack[mk - 1]);
            sp = mk - 1;
            PUSH(d);
            break;
        }
        case 'a': { /* APPEND */
            struct pval *val = NULL;
            POP(val);
            if (sp == 0 || stack[sp - 1]->kind != PK_LIST) { pv_free(val); FAIL_PK(); }
            pv_seq_push(stack[sp - 1], val);
            break;
        }
        case 'e': { /* APPENDS */
            size_t mk = sp;
            while (mk > 0 && stack[mk - 1]->kind != PK_MARK) mk--;
            if (mk < 2 || stack[mk - 2]->kind != PK_LIST) FAIL_PK();
            struct pval *lst = stack[mk - 2];
            for (size_t i = mk; i < sp; i++)
                pv_seq_push(lst, stack[i]);
            pv_free(stack[mk - 1]);
            sp = mk - 1;
            break;
        }
        case 's': { /* SETITEM */
            struct pval *val = NULL, *key = NULL;
            POP(val); POP(key);
            if (sp == 0 || stack[sp - 1]->kind != PK_DICT) {
                pv_free(key); pv_free(val); FAIL_PK();
            }
            pv_dict_set(stack[sp - 1], key, val);
            break;
        }
        case 'u': { /* SETITEMS */
            size_t mk = sp;
            while (mk > 0 && stack[mk - 1]->kind != PK_MARK) mk--;
            if (mk < 2 || stack[mk - 2]->kind != PK_DICT || ((sp - mk) & 1)) FAIL_PK();
            struct pval *d = stack[mk - 2];
            for (size_t i = mk; i + 1 < sp; i += 2)
                pv_dict_set(d, stack[i], stack[i + 1]);
            pv_free(stack[mk - 1]);
            sp = mk - 1;
            break;
        }
        case 'p': { /* PUT ASCII */
            size_t start = p;
            while (p < len && buf[p] != '\n') p++;
            if (p >= len) FAIL_PK();
            char tmp[32];
            size_t nl = p - start;
            if (nl >= sizeof tmp) nl = sizeof tmp - 1;
            memcpy(tmp, buf + start, nl); tmp[nl] = '\0';
            p++;
            size_t idx = strtoul(tmp, NULL, 10);
            if (sp > 0 && idx < MAX_PICKLE_MEMO) {
                pv_free(memo[idx]);
                memo[idx] = pv_clone(stack[sp - 1]);
            }
            break;
        }
        case 'q': { /* BINPUT */
            NEED(1);
            size_t idx = buf[p++];
            if (sp > 0 && idx < MAX_PICKLE_MEMO) {
                pv_free(memo[idx]);
                memo[idx] = pv_clone(stack[sp - 1]);
            }
            break;
        }
        case 'r': { /* LONG_BINPUT */
            NEED(4);
            size_t idx = (size_t)rd_le_u64(buf + p, 4);
            p += 4;
            if (sp > 0 && idx < MAX_PICKLE_MEMO) {
                pv_free(memo[idx]);
                memo[idx] = pv_clone(stack[sp - 1]);
            }
            break;
        }
        case 0x94: /* MEMOIZE */
            if (sp > 0 && memo_next < MAX_PICKLE_MEMO) {
                pv_free(memo[memo_next]);
                memo[memo_next++] = pv_clone(stack[sp - 1]);
            }
            break;
        case 'g': case 'h': case 'j': { /* GET / BINGET / LONG_BINGET */
            size_t idx = 0;
            if (op == 'g') {
                size_t start = p;
                while (p < len && buf[p] != '\n') p++;
                if (p >= len) FAIL_PK();
                char tmp[32];
                size_t nl = p - start;
                if (nl >= sizeof tmp) nl = sizeof tmp - 1;
                memcpy(tmp, buf + start, nl); tmp[nl] = '\0';
                p++;
                idx = strtoul(tmp, NULL, 10);
            } else if (op == 'h') {
                NEED(1); idx = buf[p++];
            } else {
                NEED(4); idx = (size_t)rd_le_u64(buf + p, 4); p += 4;
            }
            if (idx >= MAX_PICKLE_MEMO || !memo[idx]) FAIL_PK();
            PUSH(pv_clone(memo[idx]));
            break;
        }
        default:
            FAIL_PK();
        }
    }

    if (!stopped || sp == 0) FAIL_PK();
    struct pval *ret = stack[--sp];
    while (sp > 0) pv_free(stack[--sp]);
    for (size_t i = 0; i < MAX_PICKLE_MEMO; i++) pv_free(memo[i]);
    free(memo);
    return ret;

fail:
    while (sp > 0) pv_free(stack[--sp]);
    for (size_t i = 0; i < MAX_PICKLE_MEMO; i++) pv_free(memo[i]);
    free(memo);
    return NULL;
    #undef FAIL_PK
    #undef PUSH
    #undef POP
    #undef NEED
}

/* ----------------------------------------------------- candidate discovery */

struct art_cand {
    char *rel;                 /* normalized relative path under game/ */
    char *rpa_path;            /* NULL if loose file */
    char *loose_path;          /* full path if loose */
    uint64_t offset;
    uint64_t length;
    unsigned char *prefix;
    size_t prefix_len;
};

struct cand_list {
    struct art_cand *items;
    size_t count;
    size_t cap;
};

static void cand_push(struct cand_list *cl, const struct art_cand *c)
{
    if (cl->count == cl->cap) {
        size_t nc = cl->cap ? cl->cap * 2 : 64;
        struct art_cand *ni = realloc(cl->items, nc * sizeof *ni);
        if (!ni) return;
        cl->items = ni;
        cl->cap = nc;
    }
    cl->items[cl->count++] = *c;
}

static void cand_list_free(struct cand_list *cl)
{
    for (size_t i = 0; i < cl->count; i++) {
        free(cl->items[i].rel);
        free(cl->items[i].rpa_path);
        free(cl->items[i].loose_path);
        free(cl->items[i].prefix);
    }
    free(cl->items);
    memset(cl, 0, sizeof *cl);
}

static void scan_loose_dir(struct cand_list *cl, const char *base_dir, const char *sub, int depth)
{
    if (depth > 4) return;
    char dir[1024];
    if (sub[0]) snprintf(dir, sizeof dir, "%s/%s", base_dir, sub);
    else snprintf(dir, sizeof dir, "%s", base_dir);
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char rel[512], full[1024];
        struct stat st;
        if (sub[0]) snprintf(rel, sizeof rel, "%s/%s", sub, e->d_name);
        else snprintf(rel, sizeof rel, "%s", e->d_name);
        snprintf(full, sizeof full, "%s/%s", base_dir, rel);
        if (stat(full, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (eq_ci_s(e->d_name, "saves") || eq_ci_s(e->d_name, "cache") || eq_ci_s(e->d_name, "tl"))
                continue;
            scan_loose_dir(cl, base_dir, rel, depth + 1);
        } else if (S_ISREG(st.st_mode) && is_image_name(rel) &&
                   (uint64_t)st.st_size <= MAX_ART_BYTES) {
            struct art_cand c = {
                .rel = strdup(rel),
                .loose_path = strdup(full),
                .length = (uint64_t)st.st_size,
            };
            if (c.rel && c.loose_path) cand_push(cl, &c);
            else { free(c.rel); free(c.loose_path); }
        }
    }
    closedir(d);
}

static void scan_rpa_file(struct cand_list *cl, const char *rpa_path)
{
    FILE *f = fopen(rpa_path, "rb");
    if (!f) return;
    if (fseeko(f, 0, SEEK_END) != 0) { fclose(f); return; }
    off_t fsz = ftello(f);
    if (fsz < 40) { fclose(f); return; }
    uint64_t arch_size = (uint64_t)fsz;
    rewind(f);

    char head[48] = { 0 };
    if (fread(head, 1, 40, f) != 40) { fclose(f); return; }
    uint64_t idx_off = 0, key = 0;
    if (strncmp(head, "RPA-3.0 ", 8) == 0) {
        if (sscanf(head + 8, "%16llx %8llx", (unsigned long long *)&idx_off, (unsigned long long *)&key) != 2) {
            fclose(f);
            return;
        }
    } else if (strncmp(head, "RPA-2.0 ", 8) == 0) {
        if (sscanf(head + 8, "%16llx", (unsigned long long *)&idx_off) != 1) {
            fclose(f);
            return;
        }
    } else {
        fclose(f);
        return;
    }
    if (idx_off >= arch_size || arch_size - idx_off > MAX_RPA_IDX) {
        fclose(f);
        return;
    }
    size_t comp_len = (size_t)(arch_size - idx_off);
    unsigned char *comp = malloc(comp_len ? comp_len : 1);
    if (!comp) { fclose(f); return; }
    if (fseeko(f, (off_t)idx_off, SEEK_SET) != 0 || fread(comp, 1, comp_len, f) != comp_len) {
        free(comp);
        fclose(f);
        return;
    }
    fclose(f);

    /* Decompress index cleanly in chunks up to MAX_RPA_IDX */
    z_stream zs = { .next_in = comp, .avail_in = (uInt)comp_len };
    if (inflateInit(&zs) != Z_OK) { free(comp); return; }
    size_t raw_cap = comp_len * 4 + 4096;
    if (raw_cap > MAX_RPA_IDX) raw_cap = MAX_RPA_IDX;
    unsigned char *raw = malloc(raw_cap);
    if (!raw) { inflateEnd(&zs); free(comp); return; }
    size_t raw_len = 0;
    for (;;) {
        if (raw_len == raw_cap) {
            if (raw_cap >= MAX_RPA_IDX) { raw_len = 0; break; }
            size_t nc = raw_cap * 2;
            if (nc > MAX_RPA_IDX) nc = MAX_RPA_IDX;
            unsigned char *nr = realloc(raw, nc);
            if (!nr) { raw_len = 0; break; }
            raw = nr;
            raw_cap = nc;
        }
        zs.next_out = raw + raw_len;
        zs.avail_out = (uInt)(raw_cap - raw_len);
        int r = inflate(&zs, Z_NO_FLUSH);
        raw_len = (size_t)zs.total_out;
        if (r == Z_STREAM_END) break;
        if (r != Z_OK && r != Z_BUF_ERROR) { raw_len = 0; break; }
        if (r == Z_BUF_ERROR && zs.avail_in == 0) break;
    }
    inflateEnd(&zs);
    free(comp);
    if (!raw_len) { free(raw); return; }

    struct pval *root = parse_plain_pickle(raw, raw_len);
    free(raw);
    if (!root || root->kind != PK_DICT) {
        pv_free(root);
        return;
    }

    for (size_t i = 0; i < root->u.dict.count; i++) {
        const char *name = root->u.dict.keys[i];
        const struct pval *lst = root->u.dict.vals[i];
        if (!name || !is_image_name(name) || starts_ci(name, "tl/")) continue;
        if (!lst || (lst->kind != PK_LIST && lst->kind != PK_TUPLE) || lst->u.seq.count == 0) continue;
        const struct pval *tup = lst->u.seq.items[0];
        if (!tup || (tup->kind != PK_TUPLE && tup->kind != PK_LIST) || tup->u.seq.count < 2) continue;
        if (tup->u.seq.items[0]->kind != PK_INT || tup->u.seq.items[1]->kind != PK_INT) continue;

        uint64_t off = (uint64_t)tup->u.seq.items[0]->u.iv ^ key;
        uint64_t length = (uint64_t)tup->u.seq.items[1]->u.iv ^ key;
        if (off + length > arch_size || length > MAX_ART_BYTES) continue;

        char *rel = strdup(name);
        if (!rel) continue;
        for (char *p = rel; *p; p++) if (*p == '\\') *p = '/';

        struct art_cand c = {
            .rel = rel,
            .rpa_path = strdup(rpa_path),
            .offset = off,
            .length = length,
        };
        if (tup->u.seq.count >= 3 &&
            (tup->u.seq.items[2]->kind == PK_BYTES || tup->u.seq.items[2]->kind == PK_STR) &&
            tup->u.seq.items[2]->u.str.len > 0) {
            c.prefix_len = tup->u.seq.items[2]->u.str.len;
            c.prefix = malloc(c.prefix_len);
            if (c.prefix)
                memcpy(c.prefix, tup->u.seq.items[2]->u.str.s, c.prefix_len);
        }
        if (c.rpa_path) cand_push(cl, &c);
        else { free(c.rel); free(c.prefix); }
    }
    pv_free(root);
}

static bool cand_stem_matches(const char *rel, const char *target_stem)
{
    const char *exts[] = { ".png", ".jpg", ".jpeg", ".webp" };
    char cand[256];
    for (int i = 0; i < 4; i++) {
        snprintf(cand, sizeof cand, "%s%s", target_stem, exts[i]);
        if (eq_ci_s(rel, cand)) return true;
        snprintf(cand, sizeof cand, "images/%s%s", target_stem, exts[i]);
        if (eq_ci_s(rel, cand)) return true;
    }
    return false;
}

static bool has_logo_token(const char *rel)
{
    if (is_ui_chrome(rel)) return false;
    int slashes = 0;
    for (const char *p = rel; *p; p++)
        if (*p == '/' && ++slashes > 1)
            return false;
    const char *base = strrchr(rel, '/');
    base = base ? base + 1 : rel;
    if (contains_ci(base, "screen") || contains_ci(base, "bg") ||
        contains_ci(base, "line") || contains_ci(base, "bar") || contains_ci(base, "back"))
        return false;
    return contains_ci(base, "logo") || contains_ci(base, "title");
}

static void pick_best_art(const struct cand_list *cl, bool skip_main_menu,
                          const struct art_cand **bg_out, const struct art_cand **fg_out)
{
    *bg_out = NULL;
    *fg_out = NULL;

    /* 1. Direct full main menu art (matches ZATO's gui/main_menu.png & standard Ren'Py 8) */
    if (!skip_main_menu) {
        for (size_t i = 0; i < cl->count; i++) {
            if (cand_stem_matches(cl->items[i].rel, "gui/main_menu") ||
                cand_stem_matches(cl->items[i].rel, "main_menu")) {
                *bg_out = &cl->items[i];
                return;
            }
        }
    }

    /* 2. Layered title logo + menu background (exact logo/title first, then official presplash pair,
     *    then shallow logo or title fallback) */
    const struct art_cand *exact_logo = NULL, *shallow_logo = NULL, *menu_bg = NULL;
    for (size_t i = 0; i < cl->count; i++) {
        if (cand_stem_matches(cl->items[i].rel, "gui/logo")) {
            exact_logo = &cl->items[i];
            break;
        }
    }
    if (!exact_logo) {
        for (size_t i = 0; i < cl->count; i++) {
            if (cand_stem_matches(cl->items[i].rel, "gui/title")) {
                exact_logo = &cl->items[i];
                break;
            }
        }
    }
    for (size_t i = 0; i < cl->count; i++) {
        const struct art_cand *c = &cl->items[i];
        if (has_logo_token(c->rel) && (!shallow_logo || c->length > shallow_logo->length))
            shallow_logo = c;
    }

    const char *bg_stems[] = {
        "gui/titlescreen", "gui/title_screen", "gui/title_bg", "gui/titlebg",
        "titlescreen", "title_screen", "title_bg",
        "gui/menu_bg", "menu_bg", "main_bg", "bg/main", "gui/bg",
        "presplash_background", "gui/presplash_background", "presplash", "gui/presplash",
        "gui/game_menu"
    };
    for (size_t s = 0; s < sizeof bg_stems / sizeof bg_stems[0] && !menu_bg; s++) {
        for (size_t i = 0; i < cl->count; i++) {
            if (cand_stem_matches(cl->items[i].rel, bg_stems[s])) {
                menu_bg = &cl->items[i];
                break;
            }
        }
    }

    if (exact_logo && menu_bg && exact_logo != menu_bg) {
        *bg_out = menu_bg;
        *fg_out = exact_logo;
        return;
    }

    /* Official presplash_background + presplash_foreground pair (e.g. Our Life) takes precedence over broad matching */
    const struct art_cand *ps_bg = NULL, *ps_fg = NULL;
    for (size_t i = 0; i < cl->count; i++) {
        if (cand_stem_matches(cl->items[i].rel, "presplash_background") ||
            cand_stem_matches(cl->items[i].rel, "gui/presplash_background"))
            ps_bg = &cl->items[i];
        else if (cand_stem_matches(cl->items[i].rel, "presplash_foreground") ||
                 cand_stem_matches(cl->items[i].rel, "gui/presplash_foreground"))
            ps_fg = &cl->items[i];
    }
    if (ps_bg && ps_fg) {
        *bg_out = ps_bg;
        *fg_out = ps_fg;
        return;
    }

    if (shallow_logo && menu_bg && shallow_logo != menu_bg) {
        *bg_out = menu_bg;
        *fg_out = shallow_logo;
        return;
    }
    if (exact_logo) {
        *bg_out = exact_logo;
        return;
    }
    if (shallow_logo) {
        *bg_out = shallow_logo;
        return;
    }
    if (menu_bg) {
        *bg_out = menu_bg;
        return;
    }

    /* 3. Fallback: window icon or largest non-chrome image under gui/, bg/, or images/ */
    for (size_t i = 0; i < cl->count; i++) {
        if (cand_stem_matches(cl->items[i].rel, "gui/window_icon")) {
            *bg_out = &cl->items[i];
            return;
        }
    }
    const struct art_cand *largest = NULL;
    for (size_t i = 0; i < cl->count; i++) {
        const struct art_cand *c = &cl->items[i];
        if (is_ui_chrome(c->rel) || c->length < 4096) continue;
        if ((starts_ci(c->rel, "gui/") || starts_ci(c->rel, "bg/") || starts_ci(c->rel, "images/")) &&
            (!largest || c->length > largest->length))
            largest = c;
    }
    *bg_out = largest;
}

#ifndef RSL_COVER_NO_DECODE

static unsigned char *load_cand_bytes(const struct art_cand *c, size_t *len_out)
{
    size_t total = c->prefix_len + (size_t)c->length;
    if (total == 0 || total > MAX_ART_BYTES) return NULL;
    unsigned char *buf = malloc(total);
    if (!buf) return NULL;
    if (c->prefix_len)
        memcpy(buf, c->prefix, c->prefix_len);
    const char *path = c->rpa_path ? c->rpa_path : c->loose_path;
    FILE *f = fopen(path, "rb");
    if (!f) { free(buf); return NULL; }
    if (c->rpa_path && fseeko(f, (off_t)c->offset, SEEK_SET) != 0) {
        fclose(f); free(buf); return NULL;
    }
    size_t want = (size_t)c->length;
    if (fread(buf + c->prefix_len, 1, want, f) != want) {
        fclose(f); free(buf); return NULL;
    }
    fclose(f);
    *len_out = total;
    return buf;
}

/* ----------------------------------------------------- image decoding RGBA */

struct img_rgba {
    uint8_t *px;               /* w * h * 4 (R,G,B,A) */
    int w, h;
};

struct png_mem_reader {
    const unsigned char *data;
    size_t len;
    size_t pos;
};

static void png_read_mem_cb(png_structp png_ptr, png_bytep out, png_size_t count)
{
    struct png_mem_reader *r = png_get_io_ptr(png_ptr);
    if (r->pos + count > r->len)
        png_error(png_ptr, "unexpected EOF");
    memcpy(out, r->data + r->pos, count);
    r->pos += count;
}

static bool decode_png_mem(const unsigned char *data, size_t len, struct img_rgba *out)
{
    if (len < 8 || png_sig_cmp(data, 0, 8) != 0) return false;
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) return false;
    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_read_struct(&png, NULL, NULL); return false; }

    png_bytep *rows = NULL;
    uint8_t *px = NULL;
    if (setjmp(png_jmpbuf(png))) {
        free(rows);
        free(px);
        png_destroy_read_struct(&png, &info, NULL);
        return false;
    }
    struct png_mem_reader mr = { data, len, 0 };
    png_set_read_fn(png, &mr, png_read_mem_cb);
    png_read_info(png, info);

    png_uint_32 w = png_get_image_width(png, info);
    png_uint_32 h = png_get_image_height(png, info);
    png_byte ct = png_get_color_type(png, info);
    png_byte bd = png_get_bit_depth(png, info);
    if (w == 0 || h == 0 || w > 8192 || h > 8192 || (uint64_t)w * h > (16ULL << 20))
        png_error(png, "image too large");

    if (bd == 16) png_set_strip_16(png);
    if (ct == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (ct == PNG_COLOR_TYPE_GRAY && bd < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (ct == PNG_COLOR_TYPE_RGB || ct == PNG_COLOR_TYPE_GRAY || ct == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png, 0xff, PNG_FILLER_AFTER);
    if (ct == PNG_COLOR_TYPE_GRAY || ct == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);
    png_read_update_info(png, info);

    px = malloc((size_t)w * h * 4);
    rows = malloc((size_t)h * sizeof *rows);
    if (!px || !rows) png_error(png, "oom");
    for (png_uint_32 y = 0; y < h; y++)
        rows[y] = px + (size_t)y * w * 4;
    png_read_image(png, rows);
    png_read_end(png, NULL);

    free(rows);
    png_destroy_read_struct(&png, &info, NULL);
    out->px = px;
    out->w = (int)w;
    out->h = (int)h;
    return true;
}

struct jpg_err {
    struct jpeg_error_mgr pub;
    jmp_buf jmp;
};

static void jpg_err_exit(j_common_ptr cinfo)
{
    struct jpg_err *je = (struct jpg_err *)cinfo->err;
    longjmp(je->jmp, 1);
}

static bool decode_jpeg_mem(const unsigned char *data, size_t len, struct img_rgba *out)
{
    struct jpeg_decompress_struct cinfo;
    struct jpg_err jerr;
    uint8_t *px = NULL, *row = NULL;
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = jpg_err_exit;
    if (setjmp(jerr.jmp)) {
        free(row); free(px);
        jpeg_destroy_decompress(&cinfo);
        return false;
    }
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, data, (unsigned long)len);
    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        return false;
    }
    cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);
    int w = (int)cinfo.output_width, h = (int)cinfo.output_height;
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192 || (uint64_t)w * h > (16ULL << 20)) {
        jpeg_destroy_decompress(&cinfo);
        return false;
    }
    px = malloc((size_t)w * h * 4);
    row = malloc((size_t)w * 3);
    if (!px || !row) {
        free(row); free(px);
        jpeg_destroy_decompress(&cinfo);
        return false;
    }
    while ((int)cinfo.output_scanline < h) {
        int y = (int)cinfo.output_scanline;
        JSAMPROW rp = row;
        jpeg_read_scanlines(&cinfo, &rp, 1);
        uint8_t *dst = px + (size_t)y * w * 4;
        for (int x = 0; x < w; x++) {
            dst[x * 4 + 0] = row[x * 3 + 0];
            dst[x * 4 + 1] = row[x * 3 + 1];
            dst[x * 4 + 2] = row[x * 3 + 2];
            dst[x * 4 + 3] = 255;
        }
    }
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    free(row);
    out->px = px;
    out->w = w;
    out->h = h;
    return true;
}

static bool decode_webp_mem(const unsigned char *data, size_t len, struct img_rgba *out)
{
    int w = 0, h = 0;
    if (!WebPGetInfo(data, len, &w, &h)) return false;
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192 || (uint64_t)w * h > (16ULL << 20))
        return false;
    uint8_t *wp = WebPDecodeRGBA(data, len, &w, &h);
    if (!wp) return false;
    uint8_t *px = malloc((size_t)w * h * 4);
    if (px) memcpy(px, wp, (size_t)w * h * 4);
    WebPFree(wp);
    if (!px) return false;
    out->px = px;
    out->w = w;
    out->h = h;
    return true;
}

static bool decode_cand_image(const struct art_cand *c, struct img_rgba *out)
{
    size_t len = 0;
    unsigned char *data = load_cand_bytes(c, &len);
    if (!data) return false;
    bool ok = false;
    if (ends_ci(c->rel, ".png")) ok = decode_png_mem(data, len, out);
    else if (ends_ci(c->rel, ".jpg") || ends_ci(c->rel, ".jpeg")) ok = decode_jpeg_mem(data, len, out);
    else if (ends_ci(c->rel, ".webp")) ok = decode_webp_mem(data, len, out);
    free(data);
    return ok;
}

/* ----------------------------------------------------- compose 300x450 art */

static void sample_bilinear(const struct img_rgba *src, float fx, float fy, uint8_t rgba[4])
{
    if (fx < 0) fx = 0;
    if (fy < 0) fy = 0;
    if (fx > (float)(src->w - 1)) fx = (float)(src->w - 1);
    if (fy > (float)(src->h - 1)) fy = (float)(src->h - 1);
    int x0 = (int)fx, y0 = (int)fy;
    int x1 = x0 + 1 < src->w ? x0 + 1 : x0;
    int y1 = y0 + 1 < src->h ? y0 + 1 : y0;
    float tx = fx - (float)x0, ty = fy - (float)y0;
    const uint8_t *p00 = src->px + ((size_t)y0 * src->w + x0) * 4;
    const uint8_t *p10 = src->px + ((size_t)y0 * src->w + x1) * 4;
    const uint8_t *p01 = src->px + ((size_t)y1 * src->w + x0) * 4;
    const uint8_t *p11 = src->px + ((size_t)y1 * src->w + x1) * 4;
    for (int k = 0; k < 4; k++) {
        float top = (float)p00[k] * (1.0f - tx) + (float)p10[k] * tx;
        float bot = (float)p01[k] * (1.0f - tx) + (float)p11[k] * tx;
        float v = top * (1.0f - ty) + bot * ty;
        rgba[k] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v + 0.5f);
    }
}

static void draw_aspect_fill_smart(uint32_t *dst, int dw, int dh, const struct img_rgba *src, float darken)
{
    float sx = (float)src->w / (float)dw, sy = (float)src->h / (float)dh;
    float s = sx < sy ? sx : sy; /* increase -> cover whole dst */
    float cw = (float)dw * s, ch = (float)dh * s;
    float ox = ((float)src->w - cw) * 0.5f, oy = ((float)src->h - ch) * 0.5f;

    /* For wide images, downsample to a 64x36 coarse grid (ignoring 1px halftone dots and
     * thin menu text) and pick the window maximizing chroma + coarse visual contrast,
     * softly biased slightly right of centre to avoid Ren'Py's left-hand menu column. */
    int win = (int)(cw + 0.5f);
    if (win < 1) win = 1;
    if (win > src->w) win = src->w;
    if (src->w - win > 4) {
        const int gw = 64, gh = 36;
        double cr[64 * 36], cg[64 * 36], cb[64 * 36], luma[64 * 36];
        double col_score[64] = { 0 };
        for (int gy = 0; gy < gh; gy++) {
            int y0 = (int)(((int64_t)gy * src->h) / gh);
            int y1 = (int)(((int64_t)(gy + 1) * src->h) / gh);
            if (y1 <= y0) y1 = y0 + 1;
            for (int gx = 0; gx < gw; gx++) {
                int x0 = (int)(((int64_t)gx * src->w) / gw);
                int x1 = (int)(((int64_t)(gx + 1) * src->w) / gw);
                if (x1 <= x0) x1 = x0 + 1;
                double sr = 0, sg = 0, sb = 0, cnt = 0;
                int sy_step = (y1 - y0) > 6 ? (y1 - y0) / 6 : 1;
                int sx_step = (x1 - x0) > 6 ? (x1 - x0) / 6 : 1;
                for (int py = y0; py < y1; py += sy_step) {
                    const uint8_t *row = src->px + (size_t)py * src->w * 4;
                    for (int px = x0; px < x1; px += sx_step) {
                        const uint8_t *p = row + px * 4;
                        if (p[3] < 32) continue;
                        sr += p[0]; sg += p[1]; sb += p[2]; cnt++;
                    }
                }
                int idx = gy * gw + gx;
                if (cnt > 0) {
                    cr[idx] = sr / cnt; cg[idx] = sg / cnt; cb[idx] = sb / cnt;
                } else {
                    cr[idx] = cg[idx] = cb[idx] = 0;
                }
                luma[idx] = 0.299 * cr[idx] + 0.587 * cg[idx] + 0.114 * cb[idx];
            }
        }
        for (int gx = 0; gx < gw; gx++) {
            double e = 0;
            int gx1 = gx + 1 < gw ? gx + 1 : gx;
            for (int gy = 0; gy < gh; gy++) {
                int idx = gy * gw + gx;
                double r = cr[idx], g = cg[idx], b = cb[idx];
                double mx = fmax(r, fmax(g, b)), mn = fmin(r, fmin(g, b));
                double chroma = mx - mn;
                if (mx < 28.0 || (mn > 235.0 && chroma < 20.0))
                    continue;
                int gy1 = gy + 1 < gh ? gy + 1 : gy;
                double contrast = fabs(luma[idx] - luma[gy * gw + gx1]) + fabs(luma[idx] - luma[gy1 * gw + gx]);
                e += chroma * 1.4 + contrast * 0.6;
            }
            col_score[gx] = e;
        }
        int gwin = (int)(((double)win * gw) / (double)src->w + 0.5);
        if (gwin < 1) gwin = 1;
        if (gwin > gw) gwin = gw;
        int max_gx = gw - gwin;
        double best_score = -1.0;
        int best_gx = max_gx / 2;
        for (int cx = 0; cx <= max_gx; cx++) {
            double we = 0;
            for (int k = 0; k < gwin; k++)
                we += col_score[cx + k];
            double pos = max_gx > 0 ? (double)cx / (double)max_gx : 0.58;
            double weight = 0.75 + 0.25 * fmax(0.0, 1.0 - fabs(pos - 0.58) / 0.58);
            if (we * weight > best_score) {
                best_score = we * weight;
                best_gx = cx;
            }
        }
        if (best_score > 0.0 && max_gx > 0) {
            ox = (float)(((double)best_gx / (double)max_gx) * (double)(src->w - win));
        }
    }

    for (int y = 0; y < dh; y++) {
        for (int x = 0; x < dw; x++) {
            uint8_t c[4];
            sample_bilinear(src, ox + ((float)x + 0.5f) * s - 0.5f, oy + ((float)y + 0.5f) * s - 0.5f, c);
            uint32_t r = (uint32_t)((float)c[0] * darken);
            uint32_t g = (uint32_t)((float)c[1] * darken);
            uint32_t b = (uint32_t)((float)c[2] * darken);
            dst[(size_t)y * dw + x] = (r << 16) | (g << 8) | b;
        }
    }
}

static void box_blur_xrgb(uint32_t *buf, int w, int h, int rad)
{
    uint32_t *tmp = malloc((size_t)w * h * 4);
    if (!tmp) return;
    for (int pass = 0; pass < 2; pass++) {
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                uint32_t sr = 0, sg = 0, sb = 0, cnt = 0;
                int x0 = x - rad < 0 ? 0 : x - rad;
                int x1 = x + rad >= w ? w - 1 : x + rad;
                for (int k = x0; k <= x1; k++) {
                    uint32_t p = buf[(size_t)y * w + k];
                    sr += (p >> 16) & 0xff; sg += (p >> 8) & 0xff; sb += p & 0xff; cnt++;
                }
                tmp[(size_t)y * w + x] = ((sr / cnt) << 16) | ((sg / cnt) << 8) | (sb / cnt);
            }
        }
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                uint32_t sr = 0, sg = 0, sb = 0, cnt = 0;
                int y0 = y - rad < 0 ? 0 : y - rad;
                int y1 = y + rad >= h ? h - 1 : y + rad;
                for (int k = y0; k <= y1; k++) {
                    uint32_t p = tmp[(size_t)k * w + x];
                    sr += (p >> 16) & 0xff; sg += (p >> 8) & 0xff; sb += p & 0xff; cnt++;
                }
                buf[(size_t)y * w + x] = ((sr / cnt) << 16) | ((sg / cnt) << 8) | (sb / cnt);
            }
        }
    }
    free(tmp);
}

static void trim_alpha_bounds(const struct img_rgba *src, int *x0_out, int *y0_out, int *x1_out, int *y1_out)
{
    int min_x = src->w, min_y = src->h, max_x = -1, max_y = -1;
    for (int y = 0; y < src->h; y++) {
        const uint8_t *row = src->px + (size_t)y * src->w * 4;
        for (int x = 0; x < src->w; x++) {
            if (row[x * 4 + 3] >= 16) {
                if (x < min_x) min_x = x;
                if (x > max_x) max_x = x;
                if (y < min_y) min_y = y;
                if (y > max_y) max_y = y;
            }
        }
    }
    if (min_x <= max_x && min_y <= max_y && (max_x - min_x + 1) >= 16 && (max_y - min_y + 1) >= 16) {
        *x0_out = min_x; *y0_out = min_y; *x1_out = max_x; *y1_out = max_y;
    } else {
        *x0_out = 0; *y0_out = 0; *x1_out = src->w - 1; *y1_out = src->h - 1;
    }
}

static void blend_aspect_fit(uint32_t *dst, int dw, int dh, const struct img_rgba *src, float box_scale)
{
    int sx0, sy0, sx1, sy1;
    trim_alpha_bounds(src, &sx0, &sy0, &sx1, &sy1);
    float sw = (float)(sx1 - sx0 + 1), sh = (float)(sy1 - sy0 + 1);
    float max_w = (float)dw * box_scale, max_h = (float)dh * box_scale;
    float s = fminf(max_w / sw, max_h / sh);
    int fw = (int)(sw * s + 0.5f);
    int fh = (int)(sh * s + 0.5f);
    if (fw < 1) fw = 1;
    if (fh < 1) fh = 1;
    int ox = (dw - fw) / 2, oy = (dh - fh) / 2;
    for (int y = 0; y < fh; y++) {
        int dy = oy + y;
        if (dy < 0 || dy >= dh) continue;
        float sy = (float)sy0 + (((float)y + 0.5f) / (float)fh) * sh - 0.5f;
        for (int x = 0; x < fw; x++) {
            int dx = ox + x;
            if (dx < 0 || dx >= dw) continue;
            float sx = (float)sx0 + (((float)x + 0.5f) / (float)fw) * sw - 0.5f;
            uint8_t c[4];
            sample_bilinear(src, sx, sy, c);
            uint32_t a = c[3];
            if (a == 0) continue;
            uint32_t bg = dst[(size_t)dy * dw + dx];
            uint32_t br = (bg >> 16) & 0xff, bgr = (bg >> 8) & 0xff, bb = bg & 0xff;
            uint32_t rr = (c[0] * a + br * (255 - a)) / 255;
            uint32_t gg = (c[1] * a + bgr * (255 - a)) / 255;
            uint32_t bl = (c[2] * a + bb * (255 - a)) / 255;
            dst[(size_t)dy * dw + dx] = (rr << 16) | (gg << 8) | bl;
        }
    }
}

/* Reject solid-colour, mostly empty, or >88% monochrome template graphics (e.g. blank main_menu.png). */
static bool is_visually_flat(const struct img_rgba *img)
{
    if (!img || !img->px || img->w <= 0 || img->h <= 0)
        return true;
    const int gx = 24, gy = 24;
    uint64_t total = 0, opaque = 0;
    double sr = 0, sg = 0, sb = 0, sr2 = 0, sg2 = 0, sb2 = 0;
    uint16_t buckets[4096] = { 0 };
    for (int iy = 0; iy < gy; iy++) {
        int y = (int)(((int64_t)iy * img->h) / gy);
        for (int ix = 0; ix < gx; ix++) {
            int x = (int)(((int64_t)ix * img->w) / gx);
            const uint8_t *p = img->px + ((size_t)y * img->w + x) * 4;
            total++;
            if (p[3] < 32)
                continue;
            opaque++;
            int bidx = ((p[0] >> 4) << 8) | ((p[1] >> 4) << 4) | (p[2] >> 4);
            buckets[bidx]++;
            double r = p[0], g = p[1], b = p[2];
            sr += r; sg += g; sb += b;
            sr2 += r * r; sg2 += g * g; sb2 += b * b;
        }
    }
    if (opaque * 20 < total || opaque == 0)
        return true;

    int best_b = 0;
    uint16_t best_cnt = 0;
    for (int i = 0; i < 4096; i++) {
        if (buckets[i] > best_cnt) {
            best_cnt = buckets[i];
            best_b = i;
        }
    }
    int br0 = (best_b >> 8) & 15, bg0 = (best_b >> 4) & 15, bb0 = best_b & 15;
    uint64_t near_dom = 0;
    for (int dr = -1; dr <= 1; dr++) {
        int qr = br0 + dr;
        if (qr < 0 || qr > 15) continue;
        for (int dg = -1; dg <= 1; dg++) {
            int qg = bg0 + dg;
            if (qg < 0 || qg > 15) continue;
            for (int db = -1; db <= 1; db++) {
                int qb = bb0 + db;
                if (qb < 0 || qb > 15) continue;
                near_dom += buckets[(qr << 8) | (qg << 4) | qb];
            }
        }
    }
    if (near_dom * 100 >= opaque * 88)
        return true;

    double n = (double)opaque;
    double vr = sr2 / n - (sr / n) * (sr / n);
    double vg = sg2 / n - (sg / n) * (sg / n);
    double vb = sb2 / n - (sb / n) * (sb / n);
    double var = (vr + vg + vb) / 3.0;
    return var < 100.0; /* stddev < 10.0 */
}

static bool has_transparent_border(const struct img_rgba *img)
{
    if (!img || !img->px || img->w <= 0 || img->h <= 0)
        return false;
    const int gx = 24, gy = 24;
    uint64_t total = 0, transp = 0;
    for (int iy = 0; iy < gy; iy++) {
        int y = (int)(((int64_t)iy * img->h) / gy);
        for (int ix = 0; ix < gx; ix++) {
            int x = (int)(((int64_t)ix * img->w) / gx);
            const uint8_t *p = img->px + ((size_t)y * img->w + x) * 4;
            total++;
            if (p[3] < 32)
                transp++;
        }
    }
    return transp * 100 > total * 20;
}

#endif /* !RSL_COVER_NO_DECODE */

bool rsl_extract_game_cover(const char *game_dir, const char *out_bmp)
{
    char gdir[1024];
    snprintf(gdir, sizeof gdir, "%s/game", game_dir);

    struct cand_list cl = { 0 };
    scan_loose_dir(&cl, gdir, "", 0);

    DIR *d = opendir(gdir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (e->d_name[0] == '.' || !ends_ci(e->d_name, ".rpa"))
                continue;
            char full[1300];
            struct stat st;
            snprintf(full, sizeof full, "%s/%s", gdir, e->d_name);
            if (stat(full, &st) == 0 && S_ISREG(st.st_mode))
                scan_rpa_file(&cl, full);
        }
        closedir(d);
    }

    if (cl.count == 0) {
        cand_list_free(&cl);
        return false;
    }

    const struct art_cand *bg_c = NULL, *fg_c = NULL;
    pick_best_art(&cl, false, &bg_c, &fg_c);
    if (!bg_c) {
        cand_list_free(&cl);
        return false;
    }

#ifdef RSL_COVER_NO_DECODE
    (void)out_bmp;
    /* In host unit-test mode without libpng/libjpeg/libwebp, print selection */
    printf("COVER_SELECT bg=%s fg=%s\n", bg_c->rel, fg_c ? fg_c->rel : "(none)");
    cand_list_free(&cl);
    return true;
#else
    struct img_rgba bg = { 0 }, fg = { 0 };
    bool ok = decode_cand_image(bg_c, &bg);
    if (ok && !fg_c && is_visually_flat(&bg)) {
        const struct art_cand *fb_bg = NULL, *fb_fg = NULL;
        pick_best_art(&cl, true, &fb_bg, &fb_fg);
        if (fb_bg && (fb_bg != bg_c || fb_fg)) {
            free(bg.px);
            memset(&bg, 0, sizeof bg);
            bg_c = fb_bg;
            fg_c = fb_fg;
            ok = decode_cand_image(bg_c, &bg);
        }
    }
    if (ok && fg_c && !decode_cand_image(fg_c, &fg)) {
        free(fg.px);
        memset(&fg, 0, sizeof fg);
    }

    if (ok) {
        uint32_t *canvas = calloc(COVER_W * COVER_H, sizeof *canvas);
        if (canvas) {
            if (fg.px) {
                draw_aspect_fill_smart(canvas, COVER_W, COVER_H, &bg, 1.0f);
                blend_aspect_fit(canvas, COVER_W, COVER_H, &fg, 0.86f);
            } else if (!has_transparent_border(&bg)) {
                /* Full-bleed 2:3 poster crop without blurred top/bottom letterbox bars. */
                draw_aspect_fill_smart(canvas, COVER_W, COVER_H, &bg, 1.0f);
            } else {
                draw_aspect_fill_smart(canvas, COVER_W, COVER_H, &bg, 0.55f);
                box_blur_xrgb(canvas, COVER_W, COVER_H, 12);
                blend_aspect_fit(canvas, COVER_W, COVER_H, &bg, 0.86f);
            }
            char err[128];
            ok = rsl_write_bmp(out_bmp, canvas, COVER_W, COVER_H, err, sizeof err);
            free(canvas);
        } else {
            ok = false;
        }
    }

    free(bg.px);
    free(fg.px);
    cand_list_free(&cl);
    return ok;
#endif
}
