/* On-console Switch HOME Menu forwarder builder and installer.
 * Generates Program / Control / Meta NCAs in memory using the console's own
 * splCrypto header key (no prod.keys required), registers them in SdCard NCM,
 * and pushes an application record via NS so the shortcut appears on HOME. */
#include "forwarder.h"

#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jpeglib.h>
#include <png.h>
#include <webp/decode.h>
#include <switch.h>

#include "hbl_embed.h"

#define NCA3_MAGIC                    0x3341434Eu /* "NCA3" */
#define PFS0_MAGIC                    0x30534650u /* "PFS0" */
#define IVFC_MAGIC                    0x43465649u /* "IVFC" */

#define NCA_SECTOR_SIZE               0x200u
#define IVFC_MAX_LEVEL                6
#define IVFC_HASH_BLOCK_SIZE          0x4000u
#define PFS0_EXEFS_HASH_BLOCK_SIZE    0x10000u
#define PFS0_META_HASH_BLOCK_SIZE     0x1000u
#define PFS0_PADDING_SIZE             0x200u
#define ROMFS_ENTRY_EMPTY             0xFFFFFFFFu
#define ROMFS_FILEPARTITION_OFS       0x200u
#define ICON_SIZE                     256

#define NS_APP_RECORD_INSTALLED       0x3u

enum {
    NCA_CONTENT_PROGRAM = 0,
    NCA_CONTENT_META    = 1,
    NCA_CONTENT_CONTROL = 2,
};

enum {
    NCA_FS_ROMFS = 0,
    NCA_FS_PFS0  = 1,
};

enum {
    NCA_HASH_SHA256    = 2,
    NCA_HASH_INTEGRITY = 3,
};

enum {
    NCA_CRYPT_NONE = 1,
};

static const u8 HEADER_KEK_SRC[0x10] = {
    0x1F, 0x12, 0x91, 0x3A, 0x4A, 0xCB, 0xF0, 0x0D,
    0x4C, 0xDE, 0x3A, 0xF6, 0xD5, 0x23, 0x88, 0x2A,
};

static const u8 HEADER_KEY_SRC[0x20] = {
    0x5A, 0x3E, 0xD8, 0x4F, 0xDE, 0xC0, 0xD8, 0x26,
    0x31, 0xF7, 0xE2, 0x5D, 0x19, 0x7B, 0xF5, 0xD0,
    0x1C, 0x9B, 0x7B, 0xFA, 0xF6, 0x28, 0x18, 0x3D,
    0x71, 0xF6, 0x4D, 0x73, 0xF1, 0x50, 0xB9, 0xD2,
};

/* ------------------------------------------------------------------ structs */

typedef struct {
    u32 media_start_offset;
    u32 media_end_offset;
    u8 _0x8[0x4];
    u8 _0xC[0x4];
} NcaSectionTableEntry;

typedef struct {
    u64 offset;
    u64 size;
} NcaLayerRegion;

typedef struct {
    u8 master_hash[0x20];
    u32 block_size;
    u32 layer_count;
    NcaLayerRegion hash_layer;
    NcaLayerRegion pfs0_layer;
    NcaLayerRegion unused_layers[3];
    u8 _0x78[0x80];
} NcaHierarchicalSha256Data;
_Static_assert(sizeof(NcaHierarchicalSha256Data) == 0xF8, "NcaHierarchicalSha256Data size");

#pragma pack(push, 1)
typedef struct {
    u64 logical_offset;
    u64 hash_data_size;
    u32 block_size;
    u32 reserved;
} NcaIntegrityLevelInfo;
#pragma pack(pop)

typedef struct {
    u32 max_layers;
    NcaIntegrityLevelInfo levels[6];
    u8 signature_salt[0x20];
} NcaInfoLevelHash;

typedef struct {
    u32 magic;
    u32 version;
    u32 master_hash_size;
    NcaInfoLevelHash info_level_hash;
    u8 master_hash[0x20];
    u8 _0xE0[0x18];
} NcaIntegrityMetaInfo;
_Static_assert(sizeof(NcaIntegrityMetaInfo) == 0xF8, "NcaIntegrityMetaInfo size");

typedef struct {
    u16 version;
    u8 fs_type;
    u8 hash_type;
    u8 encryption_type;
    u8 metadata_hash_type;
    u8 _0x6[2];
    union {
        NcaHierarchicalSha256Data sha256;
        NcaIntegrityMetaInfo integrity;
    } hash_data;
    u8 _0x100[0x100];
} NcaFsHeader;
_Static_assert(sizeof(NcaFsHeader) == 0x200, "NcaFsHeader size");

typedef struct {
    u8 rsa_fixed_key[0x100];
    u8 rsa_npdm[0x100];
    u32 magic;
    u8 distribution_type;
    u8 content_type;
    u8 old_key_gen;
    u8 kaek_index;
    u64 size;
    u64 program_id;
    u32 context_id;
    u32 sdk_version;
    u8 key_gen;
    u8 sig_key_gen;
    u8 _0x222[0xE];
    FsRightsId rights_id;
    NcaSectionTableEntry fs_table[4];
    u8 fs_header_hash[4][0x20];
    u8 key_area[4][0x10];
    u8 _0x340[0xC0];
    NcaFsHeader fs_header[4];
} NcaHeader;
_Static_assert(sizeof(NcaHeader) == 0xC00, "NcaHeader size");

typedef struct {
    u32 magic;
    u32 total_files;
    u32 string_table_size;
    u32 padding;
} Pfs0Header;

typedef struct {
    u64 data_offset;
    u64 data_size;
    u32 name_offset;
    u32 padding;
} Pfs0FileEntry;

typedef struct {
    u32 magic;
    u32 signature_key_gen;
    u32 _0x8;
    u8 flags;
    u8 _0xD;
    u8 main_thread_priority;
    u8 main_thread_core_num;
    u32 _0x10;
    u32 sys_resource_size;
    u32 version;
    u32 main_thread_stack_size;
    char title_name[0x10];
    char product_code[0x10];
    u8 _0x40[0x30];
    u32 aci0_offset;
    u32 aci0_size;
    u32 acid_offset;
    u32 acid_size;
} NpdmMeta;

typedef struct {
    u8 rsa_sig[0x100];
    u8 rsa_pub[0x100];
    u32 magic;
    u32 size;
    u8 version;
    u8 _0x209;
    u8 _0x20A[2];
    u32 flags;
    u64 program_id_min;
    u64 program_id_max;
    u32 fac_offset;
    u32 fac_size;
    u32 sac_offset;
    u32 sac_size;
    u32 kac_offset;
    u32 kac_size;
    u8 _0x238[8];
} NpdmAcid;

typedef struct {
    u32 magic;
    u8 _0x4[0xC];
    u64 program_id;
    u8 _0x18[8];
    u32 fac_offset;
    u32 fac_size;
    u32 sac_offset;
    u32 sac_size;
    u32 kac_offset;
    u32 kac_size;
    u8 _0x38[8];
} NpdmAci0;

typedef struct {
    u64 title_id;
    u32 title_version;
    u8 meta_type;
    u8 _0xD;
    NcmContentMetaHeader meta_header;
    u8 install_type;
    u8 _0x17;
    u32 required_sys_version;
    u8 _0x1C[4];
} CnmtHeader;
_Static_assert(sizeof(CnmtHeader) == 0x20, "CnmtHeader size");

typedef struct {
    NcmContentMetaHeader header;
    NcmApplicationMetaExtendedHeader extended;
    NcmContentInfo infos[3];
} NcmAppMetaData;

typedef struct {
    NcmContentMetaKey key;
    u8 storage_id;
    u8 padding[7];
} NcmContentStorageRecord;

typedef struct {
    const char *name;
    const u8 *data;
    size_t size;
} FileItem;

typedef struct {
    u8 *data;
    size_t size;
    size_t cap;
} Buf;

typedef struct {
    Buf buf;
    u8 type;
    u8 hash[SHA256_HASH_SIZE];
} BuiltNca;

/* ----------------------------------------------------------- buffer helpers */

static bool buf_grow(Buf *b, size_t need)
{
    if (need <= b->cap)
        return true;
    size_t cap = b->cap ? b->cap : 4096u;
    while (cap < need) {
        if (cap > (size_t)-1 / 2)
            return false;
        cap *= 2u;
    }
    u8 *p = realloc(b->data, cap);
    if (!p)
        return false;
    b->data = p;
    b->cap = cap;
    return true;
}

static bool buf_write_at(Buf *b, size_t off, const void *src, size_t n)
{
    if (!buf_grow(b, off + n))
        return false;
    if (off > b->size)
        memset(b->data + b->size, 0, off - b->size);
    if (n)
        memcpy(b->data + off, src, n);
    if (off + n > b->size)
        b->size = off + n;
    return true;
}

static bool buf_write(Buf *b, const void *src, size_t n)
{
    return buf_write_at(b, b->size, src, n);
}

static bool buf_zero(Buf *b, size_t n)
{
    if (!n)
        return true;
    size_t off = b->size;
    if (!buf_grow(b, off + n))
        return false;
    memset(b->data + off, 0, n);
    b->size = off + n;
    return true;
}

static size_t buf_pad(Buf *b, size_t align, bool always_pad)
{
    size_t rem = b->size % align;
    size_t pad = rem ? (align - rem) : (always_pad ? align : 0);
    return buf_zero(b, pad) ? pad : (size_t)-1;
}

static void buf_free(Buf *b)
{
    free(b->data);
    b->data = NULL;
    b->size = b->cap = 0;
}

/* ----------------------------------------------------------- JPEG icon prep */

struct jpg_err {
    struct jpeg_error_mgr pub;
    jmp_buf env;
};

static void jpg_exit(j_common_ptr c)
{
    longjmp(((struct jpg_err *)c->err)->env, 1);
}

static u16 rd_le16(const u8 *p) { return (u16)(p[0] | (p[1] << 8)); }

static u32 rd_le32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static bool decode_bmp_rgb(const u8 *raw, size_t sz, u8 **out_rgb, int *out_w, int *out_h)
{
    if (sz < 54 || raw[0] != 'B' || raw[1] != 'M')
        return false;
    u32 data_off = rd_le32(raw + 10);
    u32 dib_size = rd_le32(raw + 14);
    int w = (int)(s32)rd_le32(raw + 18);
    int h = (int)(s32)rd_le32(raw + 22);
    u16 planes = rd_le16(raw + 26);
    u16 bpp = rd_le16(raw + 28);
    u32 comp = rd_le32(raw + 30);
    bool top_down = h < 0;
    int abs_h = top_down ? -h : h;
    if (dib_size < 40 || planes != 1 || comp != 0 || (bpp != 24 && bpp != 32))
        return false;
    if (w <= 0 || abs_h <= 0 || w > 8192 || abs_h > 8192)
        return false;
    size_t bytes_pp = (size_t)(bpp / 8);
    size_t stride = ((size_t)w * bytes_pp + 3u) & ~(size_t)3u;
    if (data_off > sz || stride > (sz - data_off) / (size_t)abs_h)
        return false;

    u8 *rgb = malloc((size_t)w * (size_t)abs_h * 3u);
    if (!rgb)
        return false;
    for (int y = 0; y < abs_h; y++) {
        int src_y = top_down ? y : (abs_h - 1 - y);
        const u8 *src_row = raw + data_off + (size_t)src_y * stride;
        u8 *dst_row = rgb + (size_t)y * (size_t)w * 3u;
        for (int x = 0; x < w; x++) {
            const u8 *sp = src_row + (size_t)x * bytes_pp;
            u8 *dp = dst_row + (size_t)x * 3u;
            dp[0] = sp[2];
            dp[1] = sp[1];
            dp[2] = sp[0];
        }
    }
    *out_rgb = rgb;
    *out_w = w;
    *out_h = abs_h;
    return true;
}

static bool decode_png_rgb(const u8 *raw, size_t sz, u8 **out_rgb, int *out_w, int *out_h)
{
    png_image img;
    memset(&img, 0, sizeof img);
    img.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&img, raw, sz))
        return false;
    img.format = PNG_FORMAT_RGB;
    size_t buf_sz = PNG_IMAGE_SIZE(img);
    if (img.width == 0 || img.height == 0 || img.width > 8192 || img.height > 8192 || !buf_sz) {
        png_image_free(&img);
        return false;
    }
    u8 *rgb = malloc(buf_sz);
    if (!rgb || !png_image_finish_read(&img, NULL, rgb, 0, NULL)) {
        free(rgb);
        png_image_free(&img);
        return false;
    }
    *out_rgb = rgb;
    *out_w = (int)img.width;
    *out_h = (int)img.height;
    return true;
}

static bool decode_webp_rgb(const u8 *raw, size_t sz, u8 **out_rgb, int *out_w, int *out_h)
{
    int w = 0, h = 0;
    uint8_t *dec = WebPDecodeRGB(raw, sz, &w, &h);
    if (!dec)
        return false;
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192) {
        WebPFree(dec);
        return false;
    }
    size_t n = (size_t)w * (size_t)h * 3u;
    u8 *rgb = malloc(n);
    if (rgb)
        memcpy(rgb, dec, n);
    WebPFree(dec);
    if (!rgb)
        return false;
    *out_rgb = rgb;
    *out_w = w;
    *out_h = h;
    return true;
}

static bool decode_jpeg_mem_rgb(const u8 *raw, size_t sz, u8 **out_rgb, int *out_w, int *out_h)
{
    struct jpeg_decompress_struct cinfo;
    struct jpg_err jerr;
    u8 *rgb = NULL;
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = jpg_exit;
    if (setjmp(jerr.env)) {
        jpeg_destroy_decompress(&cinfo);
        free(rgb);
        return false;
    }
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, raw, (unsigned long)sz);
    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        return false;
    }
    cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);
    int w = (int)cinfo.output_width, h = (int)cinfo.output_height;
    if (w <= 0 || h <= 0 || cinfo.output_components != 3) {
        jpeg_destroy_decompress(&cinfo);
        return false;
    }
    rgb = malloc((size_t)w * (size_t)h * 3u);
    if (!rgb) {
        jpeg_destroy_decompress(&cinfo);
        return false;
    }
    while (cinfo.output_scanline < cinfo.output_height) {
        JSAMPROW row = rgb + (size_t)cinfo.output_scanline * (size_t)w * 3u;
        jpeg_read_scanlines(&cinfo, &row, 1);
    }
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    *out_rgb = rgb;
    *out_w = w;
    *out_h = h;
    return true;
}

static bool decode_cover_rgb(const char *path, u8 **out_rgb, int *out_w, int *out_h)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 16 * 1024 * 1024) {
        fclose(f);
        return false;
    }
    u8 *raw = malloc((size_t)sz);
    if (!raw || fread(raw, 1, (size_t)sz, f) != (size_t)sz) {
        free(raw);
        fclose(f);
        return false;
    }
    fclose(f);

    bool ok = false;
    if ((size_t)sz >= 2 && raw[0] == 'B' && raw[1] == 'M')
        ok = decode_bmp_rgb(raw, (size_t)sz, out_rgb, out_w, out_h);
    else if ((size_t)sz >= 8 && memcmp(raw, "\x89PNG\r\n\x1a\n", 8) == 0)
        ok = decode_png_rgb(raw, (size_t)sz, out_rgb, out_w, out_h);
    else if ((size_t)sz >= 12 && memcmp(raw, "RIFF", 4) == 0 && memcmp(raw + 8, "WEBP", 4) == 0)
        ok = decode_webp_rgb(raw, (size_t)sz, out_rgb, out_w, out_h);
    else if ((size_t)sz >= 2 && raw[0] == 0xFF && raw[1] == 0xD8)
        ok = decode_jpeg_mem_rgb(raw, (size_t)sz, out_rgb, out_w, out_h);

    free(raw);
    return ok;
}

static bool build_icon_jpeg(const char *cover_path, u8 **out_jpg, size_t *out_size)
{
    u8 *src = NULL;
    int sw = 0, sh = 0;
    u8 *dst = malloc((size_t)ICON_SIZE * ICON_SIZE * 3u);
    if (!dst)
        return false;

    if (cover_path && cover_path[0] && decode_cover_rgb(cover_path, &src, &sw, &sh)) {
        int side = sw < sh ? sw : sh;
        int x0 = (sw - side) / 2;
        int y0 = (sh > sw) ? (sh - side) / 3 : 0; /* upper-third framing on 2:3 portraits */
        for (int dy = 0; dy < ICON_SIZE; dy++) {
            float fy = (float)y0 + ((float)dy + 0.5f) * (float)side / (float)ICON_SIZE - 0.5f;
            int iy0 = (int)floorf(fy);
            int iy1 = iy0 + 1;
            float wy = fy - (float)iy0;
            if (iy0 < 0) iy0 = 0;
            if (iy1 >= sh) iy1 = sh - 1;
            for (int dx = 0; dx < ICON_SIZE; dx++) {
                float fx = (float)x0 + ((float)dx + 0.5f) * (float)side / (float)ICON_SIZE - 0.5f;
                int ix0 = (int)floorf(fx);
                int ix1 = ix0 + 1;
                float wx = fx - (float)ix0;
                if (ix0 < 0) ix0 = 0;
                if (ix1 >= sw) ix1 = sw - 1;
                const u8 *p00 = src + ((size_t)iy0 * (size_t)sw + (size_t)ix0) * 3u;
                const u8 *p10 = src + ((size_t)iy0 * (size_t)sw + (size_t)ix1) * 3u;
                const u8 *p01 = src + ((size_t)iy1 * (size_t)sw + (size_t)ix0) * 3u;
                const u8 *p11 = src + ((size_t)iy1 * (size_t)sw + (size_t)ix1) * 3u;
                u8 *dp = dst + ((size_t)dy * ICON_SIZE + (size_t)dx) * 3u;
                for (int c = 0; c < 3; c++) {
                    float top = (float)p00[c] * (1.0f - wx) + (float)p10[c] * wx;
                    float bot = (float)p01[c] * (1.0f - wx) + (float)p11[c] * wx;
                    float v = top * (1.0f - wy) + bot * wy;
                    dp[c] = (u8)(v < 0.0f ? 0 : (v > 255.0f ? 255 : (int)(v + 0.5f)));
                }
            }
        }
        free(src);
    } else {
        /* Fallback dark slate gradient tile if no cover image is available. */
        for (int dy = 0; dy < ICON_SIZE; dy++) {
            for (int dx = 0; dx < ICON_SIZE; dx++) {
                u8 *dp = dst + ((size_t)dy * ICON_SIZE + (size_t)dx) * 3u;
                dp[0] = (u8)(34 + (dx * 24) / ICON_SIZE);
                dp[1] = (u8)(36 + (dy * 20) / ICON_SIZE);
                dp[2] = (u8)(58 + (dy * 42) / ICON_SIZE);
            }
        }
    }

    struct jpeg_compress_struct cinfo;
    struct jpg_err jerr;
    unsigned char *mem = NULL;
    unsigned long mem_sz = 0;
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = jpg_exit;
    if (setjmp(jerr.env)) {
        jpeg_destroy_compress(&cinfo);
        free(mem);
        free(dst);
        return false;
    }
    jpeg_create_compress(&cinfo);
    jpeg_mem_dest(&cinfo, &mem, &mem_sz);
    cinfo.image_width = ICON_SIZE;
    cinfo.image_height = ICON_SIZE;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo); /* baseline non-progressive as HOS HOME Menu requires */
    jpeg_set_quality(&cinfo, 90, TRUE);
    jpeg_start_compress(&cinfo, TRUE);
    while (cinfo.next_scanline < cinfo.image_height) {
        JSAMPROW row = dst + (size_t)cinfo.next_scanline * ICON_SIZE * 3u;
        jpeg_write_scanlines(&cinfo, &row, 1);
    }
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    free(dst);

    *out_jpg = mem;
    *out_size = (size_t)mem_sz;
    return true;
}

/* -------------------------------------------------------- PFS0 / RomFS / NCA */

static bool build_pfs0(const FileItem *items, size_t count, Buf *out)
{
    Pfs0FileEntry *ft = calloc(count, sizeof *ft);
    Buf str = { 0 };
    if (!ft)
        return false;

    u64 data_off = 0;
    for (size_t i = 0; i < count; i++) {
        size_t nlen = strlen(items[i].name) + 1;
        ft[i].data_offset = data_off;
        ft[i].data_size = items[i].size;
        ft[i].name_offset = (u32)str.size;
        if (!buf_write(&str, items[i].name, nlen)) {
            free(ft);
            buf_free(&str);
            return false;
        }
        data_off += items[i].size;
    }
    if (buf_pad(&str, 0x20, false) == (size_t)-1) {
        free(ft);
        buf_free(&str);
        return false;
    }

    Pfs0Header hdr = {
        .magic = PFS0_MAGIC,
        .total_files = (u32)count,
        .string_table_size = (u32)str.size,
    };
    bool ok = buf_write(out, &hdr, sizeof hdr) &&
              buf_write(out, ft, sizeof *ft * count) &&
              buf_write(out, str.data, str.size);
    for (size_t i = 0; ok && i < count; i++)
        ok = buf_write(out, items[i].data, items[i].size);

    free(ft);
    buf_free(&str);
    return ok;
}

static size_t align_sz(size_t v, size_t a) { return (v + (a - 1u)) & ~(a - 1u); }

static u32 romfs_hash_count(u32 n)
{
    if (n < 3)
        return 3;
    if (n < 19)
        return n | 1u;
    while (n % 2 == 0 || n % 3 == 0 || n % 5 == 0 || n % 7 == 0 ||
           n % 11 == 0 || n % 13 == 0 || n % 17 == 0)
        n++;
    return n;
}

static u32 calc_romfs_hash(u32 parent, const u8 *name, size_t len)
{
    u32 h = parent ^ 123456789u;
    for (size_t i = 0; i < len; i++) {
        h = (h >> 5) | (h << 27);
        h ^= name[i];
    }
    return h;
}

/* Build flat root-level RomFS padded to IVFC_HASH_BLOCK_SIZE; writes logical size
 * (unpadded) to `*out_logical_size`. Item names start with '/' (skipped in table). */
static bool build_romfs(const FileItem *items, size_t count, Buf *out, u64 *out_logical_size)
{
    u32 dir_hash_n = romfs_hash_count(1);
    u32 file_hash_n = romfs_hash_count((u32)count);
    u32 *dir_hash = malloc(dir_hash_n * sizeof(u32));
    u32 *file_hash = malloc(file_hash_n * sizeof(u32));
    size_t file_table_size = 0;
    size_t *file_off = calloc(count, sizeof(size_t));
    size_t *entry_off = calloc(count, sizeof(size_t));
    if (!dir_hash || !file_hash || !file_off || !entry_off) {
        free(dir_hash);
        free(file_hash);
        free(file_off);
        free(entry_off);
        return false;
    }
    for (u32 i = 0; i < dir_hash_n; i++) dir_hash[i] = ROMFS_ENTRY_EMPTY;
    for (u32 i = 0; i < file_hash_n; i++) file_hash[i] = ROMFS_ENTRY_EMPTY;

    size_t file_part_size = 0;
    for (size_t i = 0; i < count; i++) {
        const char *fname = items[i].name[0] == '/' ? items[i].name + 1 : items[i].name;
        size_t nlen = strlen(fname);
        file_part_size = align_sz(file_part_size, 0x10);
        file_off[i] = file_part_size;
        file_part_size += items[i].size;
        entry_off[i] = file_table_size;
        file_table_size += sizeof(romfs_file) + align_sz(nlen, 4);
    }

    size_t dir_table_size = sizeof(romfs_dir);
    u8 *dir_table = calloc(1, dir_table_size);
    u8 *file_table = calloc(1, file_table_size ? file_table_size : 4);
    if (!dir_table || !file_table) {
        free(dir_hash);
        free(file_hash);
        free(file_off);
        free(entry_off);
        free(dir_table);
        free(file_table);
        return false;
    }

    /* Populate file table matching Sphaira's reverse-linked insertion order. */
    for (size_t i = 0; i < count; i++) {
        const char *fname = items[i].name[0] == '/' ? items[i].name + 1 : items[i].name;
        size_t nlen = strlen(fname);
        romfs_file *fe = (romfs_file *)(file_table + entry_off[i]);
        fe->parent = 0;
        fe->sibling = (i == 0) ? ROMFS_ENTRY_EMPTY : (u32)entry_off[i - 1];
        fe->dataOff = (u64)file_off[i];
        fe->dataSize = (u64)items[i].size;
        u32 h = calc_romfs_hash(0, (const u8 *)fname, nlen);
        fe->nextHash = file_hash[h % file_hash_n];
        file_hash[h % file_hash_n] = (u32)entry_off[i];
        fe->nameLen = (u32)nlen;
        memcpy(fe->name, fname, nlen);
    }

    romfs_dir *root = (romfs_dir *)dir_table;
    root->parent = 0;
    root->sibling = ROMFS_ENTRY_EMPTY;
    root->childDir = ROMFS_ENTRY_EMPTY;
    root->childFile = count ? (u32)entry_off[count - 1] : ROMFS_ENTRY_EMPTY;
    u32 dh = calc_romfs_hash(0, NULL, 0);
    root->nextHash = dir_hash[dh % dir_hash_n];
    dir_hash[dh % dir_hash_n] = 0;
    root->nameLen = 0;

    romfs_header rh = { 0 };
    rh.headerSize = sizeof rh;
    rh.dirHashTableSize = (u64)dir_hash_n * 4u;
    rh.dirTableSize = dir_table_size;
    rh.fileHashTableSize = (u64)file_hash_n * 4u;
    rh.fileTableSize = file_table_size;
    rh.fileDataOff = ROMFS_FILEPARTITION_OFS;
    rh.dirHashTableOff = align_sz(file_part_size + ROMFS_FILEPARTITION_OFS, 4);
    rh.dirTableOff = rh.dirHashTableOff + rh.dirHashTableSize;
    rh.fileHashTableOff = rh.dirTableOff + rh.dirTableSize;
    rh.fileTableOff = rh.fileHashTableOff + rh.fileHashTableSize;

    bool ok = buf_write(out, &rh, sizeof rh);
    for (size_t i = 0; ok && i < count; i++)
        ok = buf_write_at(out, file_off[i] + ROMFS_FILEPARTITION_OFS, items[i].data, items[i].size);
    if (ok && rh.dirHashTableOff > out->size)
        ok = buf_zero(out, (size_t)(rh.dirHashTableOff - out->size));
    if (ok) {
        ok = buf_write(out, dir_hash, (size_t)rh.dirHashTableSize) &&
             buf_write(out, dir_table, dir_table_size) &&
             buf_write(out, file_hash, (size_t)rh.fileHashTableSize) &&
             buf_write(out, file_table, file_table_size);
    }
    *out_logical_size = (u64)out->size;
    if (ok && buf_pad(out, IVFC_HASH_BLOCK_SIZE, true) == (size_t)-1)
        ok = false;

    free(dir_hash);
    free(file_hash);
    free(file_off);
    free(entry_off);
    free(dir_table);
    free(file_table);
    return ok;
}

static bool build_ivfc_level(const Buf *src, Buf *dst)
{
    u8 hash[SHA256_HASH_SIZE];
    for (size_t i = 0; i < src->size; i += IVFC_HASH_BLOCK_SIZE) {
        size_t chunk = src->size - i;
        if (chunk > IVFC_HASH_BLOCK_SIZE)
            chunk = IVFC_HASH_BLOCK_SIZE;
        sha256CalculateHash(hash, src->data + i, chunk);
        if (!buf_write(dst, hash, sizeof hash))
            return false;
    }
    return buf_pad(dst, IVFC_HASH_BLOCK_SIZE, true) != (size_t)-1;
}

static bool write_nca_pfs0(NcaHeader *hdr, u8 idx, const FileItem *items, size_t count, u32 block_size, Buf *nca)
{
    Buf pfs0 = { 0 }, htab = { 0 };
    if (!build_pfs0(items, count, &pfs0)) {
        buf_free(&pfs0);
        return false;
    }
    u8 hash[SHA256_HASH_SIZE];
    for (size_t i = 0; i < pfs0.size; i += block_size) {
        size_t chunk = pfs0.size - i;
        if (chunk > block_size)
            chunk = block_size;
        sha256CalculateHash(hash, pfs0.data + i, chunk);
        if (!buf_write(&htab, hash, sizeof hash)) {
            buf_free(&pfs0);
            buf_free(&htab);
            return false;
        }
    }
    u8 master_hash[SHA256_HASH_SIZE];
    sha256CalculateHash(master_hash, htab.data, htab.size);

    size_t sec_start = (idx == 0) ? sizeof(NcaHeader) : (size_t)hdr->fs_table[idx - 1].media_end_offset * NCA_SECTOR_SIZE;
    bool ok = buf_write(nca, htab.data, htab.size);
    size_t pad = ok ? buf_pad(nca, PFS0_PADDING_SIZE, true) : (size_t)-1;
    if (!ok || pad == (size_t)-1 || !buf_write(nca, pfs0.data, pfs0.size) ||
        buf_pad(nca, NCA_SECTOR_SIZE, true) == (size_t)-1) {
        buf_free(&pfs0);
        buf_free(&htab);
        return false;
    }

    hdr->fs_table[idx].media_start_offset = (u32)(sec_start / NCA_SECTOR_SIZE);
    hdr->fs_table[idx].media_end_offset = (u32)(nca->size / NCA_SECTOR_SIZE);
    hdr->fs_table[idx]._0x8[0] = 1;

    NcaFsHeader *fs = &hdr->fs_header[idx];
    fs->version = 2;
    fs->fs_type = NCA_FS_PFS0;
    fs->hash_type = NCA_HASH_SHA256;
    fs->encryption_type = NCA_CRYPT_NONE;
    fs->hash_data.sha256.block_size = block_size;
    fs->hash_data.sha256.layer_count = 2;
    fs->hash_data.sha256.hash_layer.offset = 0;
    fs->hash_data.sha256.hash_layer.size = htab.size;
    fs->hash_data.sha256.pfs0_layer.offset = htab.size + pad;
    fs->hash_data.sha256.pfs0_layer.size = pfs0.size;
    memcpy(fs->hash_data.sha256.master_hash, master_hash, SHA256_HASH_SIZE);
    sha256CalculateHash(hdr->fs_header_hash[idx], fs, sizeof *fs);

    buf_free(&pfs0);
    buf_free(&htab);
    return true;
}

static bool write_nca_romfs(NcaHeader *hdr, u8 idx, const FileItem *items, size_t count, Buf *nca)
{
    Buf ivfc[IVFC_MAX_LEVEL] = { 0 };
    u64 logical_size = 0;
    bool ok = build_romfs(items, count, &ivfc[5], &logical_size);
    NcaFsHeader *fs = &hdr->fs_header[idx];
    NcaInfoLevelHash *ilh = &fs->hash_data.integrity.info_level_hash;
    ilh->levels[5].hash_data_size = logical_size;
    ilh->levels[5].block_size = 0x0E; /* log2(0x4000) */

    for (int b = 4; ok && b >= 0; b--) {
        ok = build_ivfc_level(&ivfc[b + 1], &ivfc[b]);
        ilh->levels[b].hash_data_size = ivfc[b].size;
        ilh->levels[b].block_size = 0x0E;
    }
    ilh->levels[0].logical_offset = 0;
    for (int l = 1; l <= 5; l++)
        ilh->levels[l].logical_offset = ilh->levels[l - 1].logical_offset + ilh->levels[l - 1].hash_data_size;

    size_t sec_start = (idx == 0) ? sizeof(NcaHeader) : (size_t)hdr->fs_table[idx - 1].media_end_offset * NCA_SECTOR_SIZE;
    for (int l = 0; ok && l < IVFC_MAX_LEVEL; l++)
        ok = buf_write(nca, ivfc[l].data, ivfc[l].size);
    if (ok && buf_pad(nca, NCA_SECTOR_SIZE, true) == (size_t)-1)
        ok = false;

    if (ok) {
        sha256CalculateHash(fs->hash_data.integrity.master_hash, ivfc[0].data, ivfc[0].size);
        hdr->fs_table[idx].media_start_offset = (u32)(sec_start / NCA_SECTOR_SIZE);
        hdr->fs_table[idx].media_end_offset = (u32)(nca->size / NCA_SECTOR_SIZE);
        hdr->fs_table[idx]._0x8[0] = 1;

        fs->version = 2;
        fs->fs_type = NCA_FS_ROMFS;
        fs->hash_type = NCA_HASH_INTEGRITY;
        fs->encryption_type = NCA_CRYPT_NONE;
        fs->hash_data.integrity.magic = IVFC_MAGIC;
        fs->hash_data.integrity.version = 0x20000;
        fs->hash_data.integrity.master_hash_size = SHA256_HASH_SIZE;
        ilh->max_layers = 7;
        sha256CalculateHash(hdr->fs_header_hash[idx], fs, sizeof *fs);
    }

    for (int l = 0; l < IVFC_MAX_LEVEL; l++)
        buf_free(&ivfc[l]);
    return ok;
}

static bool finalize_nca(NcaHeader *hdr, u64 tid, u8 content_type, const u8 header_key[0x20], BuiltNca *out)
{
    hdr->magic = NCA3_MAGIC;
    hdr->distribution_type = 0;
    hdr->content_type = content_type;
    hdr->program_id = tid;
    hdr->sdk_version = 0x000C1100;
    hdr->size = out->buf.size;

    u8 encrypted[sizeof(NcaHeader)];
    Aes128XtsContext ctx;
    aes128XtsContextCreate(&ctx, header_key, header_key + 0x10, true);
    for (u64 sector = 0; sector < sizeof(NcaHeader) / NCA_SECTOR_SIZE; sector++) {
        size_t pos = (size_t)sector * NCA_SECTOR_SIZE;
        aes128XtsContextResetSector(&ctx, sector, true);
        aes128XtsEncrypt(&ctx, encrypted + pos, (const u8 *)hdr + pos, NCA_SECTOR_SIZE);
    }
    if (!buf_write_at(&out->buf, 0, encrypted, sizeof encrypted))
        return false;
    sha256CalculateHash(out->hash, out->buf.data, out->buf.size);
    return true;
}

/* ------------------------------------------------------------- NPDM & NACP */

static bool patch_kac(u8 *buf, u32 off, u32 size, u32 bitmask, u32 value)
{
    u32 pattern = BIT(bitmask) - 1u;
    u32 mask = BIT(bitmask) | pattern;
    for (u32 i = 0; i + 4 <= size; i += 4) {
        u32 cup;
        memcpy(&cup, buf + off + i, sizeof cup);
        if ((cup & mask) == pattern) {
            cup = value | pattern;
            memcpy(buf + off + i, &cup, sizeof cup);
            return true;
        }
    }
    return false;
}

static bool patch_npdm(u8 *npdm, size_t size, u64 tid)
{
    if (size < sizeof(NpdmMeta))
        return false;
    NpdmMeta meta;
    memcpy(&meta, npdm, sizeof meta);
    if (meta.aci0_offset + sizeof(NpdmAci0) > size || meta.acid_offset + sizeof(NpdmAcid) > size)
        return false;

    NpdmAci0 aci0;
    NpdmAcid acid;
    memcpy(&aci0, npdm + meta.aci0_offset, sizeof aci0);
    memcpy(&acid, npdm + meta.acid_offset, sizeof acid);

    memset(meta.title_name, 0, sizeof meta.title_name);
    memcpy(meta.title_name, "Application", 11);
    memset(meta.product_code, 0, sizeof meta.product_code);
    meta.flags = (u8)((meta.flags & ~(0x7u << 1)) | (1u << 1)); /* 36-bit address space */
    aci0.program_id = tid;
    acid.program_id_min = tid;
    acid.program_id_max = tid;

    const u32 flags_3cores = (((2u << 8) | 0u) << 6 | 28u) << 6 | 59u;
    const u32 kac_core3 = flags_3cores << 4;
    if (!patch_kac(npdm, meta.aci0_offset + aci0.kac_offset, aci0.kac_size, 3, kac_core3) ||
        !patch_kac(npdm, meta.acid_offset + acid.kac_offset, acid.kac_size, 3, kac_core3))
        return false;

    /* Atmosphere >= 1.8.0 (HOS 19.0.0+) requires ForceDebug in MiscFlags. */
    if (R_SUCCEEDED(splInitialize())) {
        u64 ver = 0;
        if (R_SUCCEEDED(splGetConfig((SplConfigItem)65000, &ver))) {
            ver >>= 40;
            if (ver >= MAKEHOSVERSION(1, 8, 0)) {
                patch_kac(npdm, meta.aci0_offset + aci0.kac_offset, aci0.kac_size, 16, BIT(19));
                patch_kac(npdm, meta.acid_offset + acid.kac_offset, acid.kac_size, 16, BIT(19));
            }
        }
        splExit();
    }

    memcpy(npdm, &meta, sizeof meta);
    memcpy(npdm + meta.aci0_offset, &aci0, sizeof aci0);
    memcpy(npdm + meta.acid_offset, &acid, sizeof acid);
    return true;
}

static void fill_nacp(NacpStruct *nacp, const char *title, u64 tid)
{
    memset(nacp, 0, sizeof *nacp);
    for (int i = 0; i < 16; i++) {
        snprintf(nacp->lang[i].name, sizeof nacp->lang[i].name, "%s", title && title[0] ? title : "Ren'Py Game");
        snprintf(nacp->lang[i].author, sizeof nacp->lang[i].author, "rpynx");
    }
    nacp->add_on_content_registration_type = 1;
    nacp->supported_language_flag = BIT(0);
    nacp->video_capture = 2;
    memset(nacp->rating_age, 0xFF, sizeof nacp->rating_age);
    snprintf(nacp->display_version, sizeof nacp->display_version, "1.0.0");
    nacp->logo_type = 2;
    nacp->presence_group_id = tid;
    nacp->save_data_owner_id = tid;
    nacp->pseudo_device_id_seed = tid;
    nacp->add_on_content_base_id = tid ^ 0x1000u;
    for (size_t i = 0; i < sizeof(nacp->local_communication_id) / sizeof(nacp->local_communication_id[0]); i++)
        nacp->local_communication_id[i] = tid;
    memcpy(&nacp->application_error_code_category, "rpynx\0\0\0", 8);
}

/* ----------------------------------------------------- NCM / NS integration */

static Result write_and_register_nca(NcmContentStorage *cs, const BuiltNca *nca)
{
    NcmContentId cid;
    NcmPlaceHolderId pid;
    memcpy(&cid, nca->hash, sizeof cid);
    Result rc = ncmContentStorageGeneratePlaceHolderId(cs, &pid);
    if (R_FAILED(rc)) return rc;
    ncmContentStorageDeletePlaceHolder(cs, &pid);
    rc = ncmContentStorageCreatePlaceHolder(cs, &cid, &pid, (s64)nca->buf.size);
    if (R_FAILED(rc)) return rc;
    const size_t chunk_max = 1024u * 1024u;
    for (size_t off = 0; off < nca->buf.size; off += chunk_max) {
        size_t chunk = nca->buf.size - off;
        if (chunk > chunk_max) chunk = chunk_max;
        rc = ncmContentStorageWritePlaceHolder(cs, &pid, (u64)off, nca->buf.data + off, chunk);
        if (R_FAILED(rc)) {
            ncmContentStorageDeletePlaceHolder(cs, &pid);
            return rc;
        }
    }
    ncmContentStorageDelete(cs, &cid);
    rc = ncmContentStorageRegister(cs, &cid, &pid);
    if (R_FAILED(rc))
        ncmContentStorageDeletePlaceHolder(cs, &pid);
    return rc;
}

static Result push_ns_app_record(Service *ns_app, u64 tid, const NcmContentStorageRecord *rec)
{
    const struct {
        u8 last_modified_event;
        u8 padding[7];
        u64 tid;
    } in = { NS_APP_RECORD_INSTALLED, { 0 }, tid };
    return serviceDispatchIn(ns_app, 16, in,
        .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_In },
        .buffers = { { rec, sizeof *rec } });
}

bool rsl_forwarder_install(const char *nro_path, const char *game_dir, const char *game_title,
                           const char *cover_path, char *err, size_t errlen)
{
    if (!nro_path || !nro_path[0] || !game_dir || !game_dir[0]) {
        snprintf(err, errlen, "invalid game or nro path");
        return false;
    }

    char next_argv[1024];
    snprintf(next_argv, sizeof next_argv, "\"%s\" --launch \"%s\"", nro_path, game_dir);

    /* Derive deterministic TitleID from target NRO + argv (Sphaira 0x05.. range). */
    u8 tid_hash[SHA256_HASH_SIZE];
    Sha256Context sc;
    sha256ContextCreate(&sc);
    sha256ContextUpdate(&sc, nro_path, strlen(nro_path));
    sha256ContextUpdate(&sc, next_argv, strlen(next_argv));
    sha256ContextGetHash(&sc, tid_hash);
    u64 hash64 = 0;
    memcpy(&hash64, tid_hash, sizeof hash64);
    u64 tid = 0x0500000000000000ULL | (hash64 & 0x00FFFFFFFFFFF000ULL);
    u64 old_tid = 0x0100000000000000ULL | (hash64 & 0x00FFFFFFFFFFF000ULL);

    u8 header_key[0x20] = { 0 };
    u8 header_kek[0x20] = { 0 };
    Result rc = splCryptoInitialize();
    if (R_FAILED(rc)) {
        snprintf(err, errlen, "splCryptoInitialize: 0x%x", (unsigned)rc);
        return false;
    }
    rc = splCryptoGenerateAesKek(HEADER_KEK_SRC, 0, 0, header_kek);
    if (R_SUCCEEDED(rc)) rc = splCryptoGenerateAesKey(header_kek, HEADER_KEY_SRC, header_key);
    if (R_SUCCEEDED(rc)) rc = splCryptoGenerateAesKey(header_kek, HEADER_KEY_SRC + 0x10, header_key + 0x10);
    splCryptoExit();
    if (R_FAILED(rc)) {
        snprintf(err, errlen, "header_key derive: 0x%x", (unsigned)rc);
        return false;
    }

    BuiltNca ncas[3] = { 0 };
    u8 *npdm_copy = malloc(hbl_npdm_size);
    u8 *icon_jpg = NULL;
    size_t icon_size = 0;
    bool ok = false;

    if (!npdm_copy) {
        snprintf(err, errlen, "out of memory");
        goto cleanup;
    }
    memcpy(npdm_copy, hbl_npdm_data, hbl_npdm_size);
    if (!patch_npdm(npdm_copy, hbl_npdm_size, tid)) {
        snprintf(err, errlen, "patch_npdm failed");
        goto cleanup;
    }
    if (!build_icon_jpeg(cover_path, &icon_jpg, &icon_size)) {
        snprintf(err, errlen, "build icon failed");
        goto cleanup;
    }

    /* 1. Program NCA */
    {
        NcaHeader hdr = { 0 };
        ncas[0].type = NcmContentType_Program;
        if (!buf_zero(&ncas[0].buf, sizeof hdr)) {
            snprintf(err, errlen, "oom program nca");
            goto cleanup;
        }
        FileItem exefs[2] = {
            { "main", hbl_main_data, hbl_main_size },
            { "main.npdm", npdm_copy, hbl_npdm_size },
        };
        FileItem prog_romfs[2] = {
            { "/nextArgv", (const u8 *)next_argv, strlen(next_argv) },
            { "/nextNroPath", (const u8 *)nro_path, strlen(nro_path) },
        };
        if (!write_nca_pfs0(&hdr, 0, exefs, 2, PFS0_EXEFS_HASH_BLOCK_SIZE, &ncas[0].buf) ||
            !write_nca_romfs(&hdr, 1, prog_romfs, 2, &ncas[0].buf) ||
            !finalize_nca(&hdr, tid, NCA_CONTENT_PROGRAM, header_key, &ncas[0])) {
            snprintf(err, errlen, "build program nca failed");
            goto cleanup;
        }
    }

    /* 2. Control NCA */
    {
        NcaHeader hdr = { 0 };
        NacpStruct nacp;
        fill_nacp(&nacp, game_title, tid);
        ncas[1].type = NcmContentType_Control;
        if (!buf_zero(&ncas[1].buf, sizeof hdr)) {
            snprintf(err, errlen, "oom control nca");
            goto cleanup;
        }
        FileItem ctrl_romfs[2] = {
            { "/control.nacp", (const u8 *)&nacp, sizeof nacp },
            { "/icon_AmericanEnglish.dat", icon_jpg, icon_size },
        };
        if (!write_nca_romfs(&hdr, 0, ctrl_romfs, 2, &ncas[1].buf) ||
            !finalize_nca(&hdr, tid, NCA_CONTENT_CONTROL, header_key, &ncas[1])) {
            snprintf(err, errlen, "build control nca failed");
            goto cleanup;
        }
    }

    /* 3. Meta NCA */
    NcmAppMetaData meta_data = { 0 };
    NcmContentMetaKey meta_key = { 0 };
    NcmContentStorageRecord storage_rec = { 0 };
    {
        NcmPackagedContentInfo pinfo[2] = { 0 };
        for (int k = 0; k < 2; k++) {
            memcpy(pinfo[k].hash, ncas[k].hash, sizeof pinfo[k].hash);
            memcpy(&pinfo[k].info.content_id, ncas[k].hash, sizeof pinfo[k].info.content_id);
            pinfo[k].info.content_type = ncas[k].type;
            ncmU64ToContentInfoSize(ncas[k].buf.size, &pinfo[k].info);
        }
        CnmtHeader ch = { 0 };
        ch.title_id = tid;
        ch.meta_type = NcmContentMetaType_Application;
        ch.meta_header.extended_header_size = sizeof(NcmApplicationMetaExtendedHeader);
        ch.meta_header.content_count = 2;
        ch.meta_header.content_meta_count = 1;
        ch.meta_header.storage_id = NcmStorageId_SdCard;
        ch.install_type = NcmContentInstallType_Full;

        NcmApplicationMetaExtendedHeader ext = { 0 };
        ext.patch_id = tid | 0x800u;
        u8 digest[0x20] = { 0 };

        Buf cnmt = { 0 };
        if (!buf_write(&cnmt, &ch, sizeof ch) ||
            !buf_write(&cnmt, &ext, sizeof ext) ||
            !buf_write(&cnmt, pinfo, sizeof pinfo) ||
            !buf_write(&cnmt, digest, sizeof digest)) {
            buf_free(&cnmt);
            snprintf(err, errlen, "oom cnmt");
            goto cleanup;
        }
        char cnmt_name[40];
        snprintf(cnmt_name, sizeof cnmt_name, "Application_%016llX.cnmt", (unsigned long long)tid);
        FileItem meta_items[1] = { { cnmt_name, cnmt.data, cnmt.size } };

        NcaHeader hdr = { 0 };
        ncas[2].type = NcmContentType_Meta;
        if (!buf_zero(&ncas[2].buf, sizeof hdr) ||
            !write_nca_pfs0(&hdr, 0, meta_items, 1, PFS0_META_HASH_BLOCK_SIZE, &ncas[2].buf) ||
            !finalize_nca(&hdr, tid, NCA_CONTENT_META, header_key, &ncas[2])) {
            buf_free(&cnmt);
            snprintf(err, errlen, "build meta nca failed");
            goto cleanup;
        }

        meta_key.id = tid;
        meta_key.type = NcmContentMetaType_Application;
        meta_key.install_type = NcmContentInstallType_Full;

        storage_rec.key = meta_key;
        storage_rec.storage_id = NcmStorageId_SdCard;

        meta_data.header = ch.meta_header;
        meta_data.header.content_count = 3;
        meta_data.header.storage_id = 0;
        meta_data.extended = ext;
        memcpy(&meta_data.infos[0].content_id, ncas[2].hash, sizeof meta_data.infos[0].content_id);
        meta_data.infos[0].content_type = NcmContentType_Meta;
        ncmU64ToContentInfoSize(cnmt.size, &meta_data.infos[0]);
        meta_data.infos[1] = pinfo[0].info;
        meta_data.infos[2] = pinfo[1].info;
        buf_free(&cnmt);
    }

    /* 4. Register NCAs & Meta in NCM (SdCard). */
    rc = ncmInitialize();
    if (R_FAILED(rc)) {
        snprintf(err, errlen, "ncmInitialize: 0x%x", (unsigned)rc);
        goto cleanup;
    }
    NcmContentStorage cs;
    rc = ncmOpenContentStorage(&cs, NcmStorageId_SdCard);
    if (R_FAILED(rc)) {
        ncmExit();
        snprintf(err, errlen, "ncmOpenContentStorage: 0x%x", (unsigned)rc);
        goto cleanup;
    }
    for (int k = 0; k < 3; k++) {
        rc = write_and_register_nca(&cs, &ncas[k]);
        if (R_FAILED(rc)) {
            ncmContentStorageClose(&cs);
            ncmExit();
            snprintf(err, errlen, "ncm write nca[%d]: 0x%x", k, (unsigned)rc);
            goto cleanup;
        }
    }
    ncmContentStorageClose(&cs);

    NcmContentMetaDatabase db;
    rc = ncmOpenContentMetaDatabase(&db, NcmStorageId_SdCard);
    if (R_FAILED(rc)) {
        ncmExit();
        snprintf(err, errlen, "ncmOpenContentMetaDatabase: 0x%x", (unsigned)rc);
        goto cleanup;
    }
    rc = ncmContentMetaDatabaseSet(&db, &meta_key, &meta_data, sizeof meta_data);
    if (R_SUCCEEDED(rc)) rc = ncmContentMetaDatabaseCommit(&db);
    ncmContentMetaDatabaseClose(&db);
    ncmExit();
    if (R_FAILED(rc)) {
        snprintf(err, errlen, "ncm meta commit: 0x%x", (unsigned)rc);
        goto cleanup;
    }

    /* 5. Push ApplicationRecord into NS so it appears on HOME Menu. */
    rc = nsInitialize();
    if (R_FAILED(rc)) {
        snprintf(err, errlen, "nsInitialize: 0x%x", (unsigned)rc);
        goto cleanup;
    }
    Service ns_app = { 0 };
    bool close_app_srv = false;
    if (hosversionAtLeast(3, 0, 0)) {
        rc = nsGetApplicationManagerInterface(&ns_app);
        close_app_srv = R_SUCCEEDED(rc);
    } else {
        ns_app = *nsGetServiceSession_ApplicationManagerInterface();
        rc = 0;
    }
    if (R_FAILED(rc)) {
        nsExit();
        snprintf(err, errlen, "nsGetAppManager: 0x%x", (unsigned)rc);
        goto cleanup;
    }

    nsDeleteApplicationCompletely(old_tid);
    nsDeleteApplicationEntity(tid);
    rc = push_ns_app_record(&ns_app, tid, &storage_rec);
    if (R_SUCCEEDED(rc))
        serviceDispatchIn(&ns_app, 404, tid); /* InvalidateApplicationControlCache */

    if (close_app_srv)
        serviceClose(&ns_app);
    nsExit();
    if (R_FAILED(rc)) {
        snprintf(err, errlen, "nsPushApplicationRecord: 0x%x", (unsigned)rc);
        goto cleanup;
    }

    ok = true;

cleanup:
    for (int k = 0; k < 3; k++)
        buf_free(&ncas[k].buf);
    free(npdm_copy);
    free(icon_jpg);
    return ok;
}
