/* Change picture (the detail page): zoom and pan on the game's cover artwork
 * inside a fixed 2:3 frame, saving the framed region as a 24-bit cover.bmp. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "cJSON.h"
#include "cover_extract.h"
#include "forwarder.h"
#include "fs.h"
#include "launcher.h"
#include "log.h"
#include "ui.h"

#define VIEW_W       1280
#define VIEW_H       720
#define FRAME_W      360
#define FRAME_H      540
#define FRAME_CX     (VIEW_W / 2)
#define FRAME_CY     (96 + FRAME_H / 2)
#define COVER_OUT_W  300
#define COVER_OUT_H  450
#define ZOOM_MIN     0.4f
#define ZOOM_MAX     4.0f
#define PAN_STEP_PX  28.0f

static uint8_t *src_rgb;
static int src_w, src_h;
static float base_scale, zoom, pan_x, pan_y;
static bool pic_dirty;

static void close_picture(void)
{
    free(src_rgb);
    free(g.shot);   /* ui.c detaches u.backdrop before freeing */
    src_rgb = NULL;
    src_w = src_h = 0;
    g.shot = NULL;
    g.shot_w = g.shot_h = 0;
    pic_dirty = false;
}

static void back_to_details(void)
{
    go(SCR_DETAIL);   /* picture_tick() closes the buffers on leave */
    g.sel = DETAIL_PICTURE;
}

static void clamp_view(void)
{
    if (zoom < ZOOM_MIN)
        zoom = ZOOM_MIN;
    if (zoom > ZOOM_MAX)
        zoom = ZOOM_MAX;
    float hx = src_w * 0.5f;
    float hy = src_h * 0.5f;
    if (pan_x < -hx)
        pan_x = -hx;
    if (pan_x > hx)
        pan_x = hx;
    if (pan_y < -hy)
        pan_y = -hy;
    if (pan_y > hy)
        pan_y = hy;
}

static void render_preview(void)
{
    if (!g.shot || !src_rgb || src_w <= 0 || src_h <= 0)
        return;
    float scale = base_scale * zoom;
    int32_t inv_fp = (int32_t)llround(65536.0 / (double)scale);
    int32_t ux0_fp = (int32_t)llround(((double)src_w * 0.5 + (double)pan_x - (double)FRAME_CX / (double)scale) * 65536.0);
    int32_t uy0_fp = (int32_t)llround(((double)src_h * 0.5 + (double)pan_y - (double)FRAME_CY / (double)scale) * 65536.0);

    for (int y = 0; y < VIEW_H; y++) {
        uint32_t *dst = g.shot + (size_t)y * VIEW_W;
        int sy = (uy0_fp + y * inv_fp) >> 16;
        if (sy < 0 || sy >= src_h) {
            for (int x = 0; x < VIEW_W; x++)
                dst[x] = 0xff000000u;
            continue;
        }
        const uint8_t *row = src_rgb + (size_t)sy * (size_t)src_w * 3u;
        int32_t ux_fp = ux0_fp;
        for (int x = 0; x < VIEW_W; x++, ux_fp += inv_fp) {
            int sx = ux_fp >> 16;
            if (sx < 0 || sx >= src_w) {
                dst[x] = 0xff000000u;
            } else {
                const uint8_t *p = row + (size_t)sx * 3u;
                dst[x] = 0xff000000u | ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[2];
            }
        }
    }
    g.pic_zoom_pct = (int)(zoom * 100.0f + 0.5f);
    g.shot_gen++;
}

static bool ends_with_ci(const char *s, const char *suf)
{
    size_t ns = strlen(s), nf = strlen(suf);
    if (ns < nf)
        return false;
    s += ns - nf;
    for (size_t i = 0; i < nf; i++) {
        char a = s[i], b = suf[i];
        if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
        if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
        if (a != b)
            return false;
    }
    return true;
}

static bool load_cover_source(int i, char *label, size_t label_len)
{
    static const char *const exts[] = { "cover.jpg", "cover.jpeg", "cover.png", "cover.webp" };
    char gdir[512], cand[544];
    snprintf(gdir, sizeof gdir, GAMES_DIR "/%s", g.games[i]);

    for (size_t k = 0; k < sizeof exts / sizeof exts[0]; k++) {
        snprintf(cand, sizeof cand, "%s/%s", gdir, exts[k]);
        if (rsl_decode_image_rgb(cand, &src_rgb, &src_w, &src_h)) {
            snprintf(label, label_len, "%s", exts[k]);
            return true;
        }
    }

    const char *cur = g.info[i].cover;
    if (cur[0] && !ends_with_ci(cur, "/cover.bmp") &&
        rsl_decode_image_rgb(cur, &src_rgb, &src_w, &src_h)) {
        const char *slash = strrchr(cur, '/');
        snprintf(label, label_len, "%s", slash ? slash + 1 : cur);
        return true;
    }

    if (rsl_extract_game_cover_raw(gdir, &src_rgb, &src_w, &src_h)) {
        snprintf(label, label_len, "%s", rsl_ui_tr("游戏原图"));
        return true;
    }

    if (cur[0] && rsl_decode_image_rgb(cur, &src_rgb, &src_w, &src_h)) {
        snprintf(label, label_len, "%s", "cover.bmp");
        return true;
    }

    return false;
}

void open_picture(int i)
{
    if (g.info[i].is_zip) {
        status(ST_INFO, rsl_ui_tr("请先解压此游戏后再设置封面或手柄。"));
        return;
    }
    close_picture();
    if (!load_cover_source(i, g.pic_label, sizeof g.pic_label)) {
        status(ST_INFO, rsl_ui_tr("未找到此游戏的封面图片（请在游戏目录放置 cover.jpg/png/webp/bmp）。"));
        return;
    }
    g.shot = malloc((size_t)VIEW_W * VIEW_H * 4u);
    if (!g.shot) {
        close_picture();
        status(ST_BAD, rsl_ui_tr("内存不足，无法加载封面。"));
        return;
    }
    float sx = (float)FRAME_W / (float)src_w;
    float sy = (float)FRAME_H / (float)src_h;
    base_scale = sx > sy ? sx : sy;
    zoom = 1.0f;
    pan_x = 0.0f;
    pan_y = 0.0f;
    clamp_view();

    g.shot_w = VIEW_W;
    g.shot_h = VIEW_H;
    g.shot_gen = 0;
    render_preview();
    pic_dirty = false;
    rsl_log("change picture: %s (%s, %dx%d)", g.games[i], g.pic_label, src_w, src_h);
    go(SCR_PICTURE);
}

void picture_tick(void)
{
    if (g.screen != SCR_PICTURE && (g.shot || src_rgb)) {
        close_picture();
        return;
    }
    if (g.screen != SCR_PICTURE || !g.shot || !src_rgb)
        return;
    int dx = 0, dy = 0;
    if (ui_picture_drag(&dx, &dy) && !g.menu_open) {
        float scale = base_scale * zoom;
        pan_x -= (float)dx / scale;
        pan_y -= (float)dy / scale;
        clamp_view();
        pic_dirty = true;
    }
    if (pic_dirty) {
        render_preview();
        pic_dirty = false;
    }
}

static bool save_cropped_bmp(int i, char *err, size_t errlen)
{
    uint32_t *px = malloc((size_t)COVER_OUT_W * COVER_OUT_H * 4u);
    if (!px) {
        snprintf(err, errlen, rsl_ui_tr("内存不足"));
        return false;
    }

    float scale = base_scale * zoom;
    double step = ((double)FRAME_W / (double)COVER_OUT_W) / (double)scale;
    double ux0 = (double)src_w * 0.5 + (double)pan_x - ((double)FRAME_W * 0.5) / (double)scale;
    double uy0 = (double)src_h * 0.5 + (double)pan_y - ((double)FRAME_H * 0.5) / (double)scale;

    for (int y = 0; y < COVER_OUT_H; y++) {
        double uy = uy0 + ((double)y + 0.5) * step - 0.5;
        int iy0 = (int)floor(uy), iy1 = iy0 + 1;
        double wy = uy - iy0;
        for (int x = 0; x < COVER_OUT_W; x++) {
            double ux = ux0 + ((double)x + 0.5) * step - 0.5;
            int ix0 = (int)floor(ux), ix1 = ix0 + 1;
            double wx = ux - ix0;
            if (ix1 < 0 || ix0 >= src_w || iy1 < 0 || iy0 >= src_h) {
                px[(size_t)y * COVER_OUT_W + x] = 0u;
                continue;
            }
            int cx0 = ix0 < 0 ? 0 : (ix0 >= src_w ? src_w - 1 : ix0);
            int cx1 = ix1 < 0 ? 0 : (ix1 >= src_w ? src_w - 1 : ix1);
            int cy0 = iy0 < 0 ? 0 : (iy0 >= src_h ? src_h - 1 : iy0);
            int cy1 = iy1 < 0 ? 0 : (iy1 >= src_h ? src_h - 1 : iy1);
            const uint8_t *p00 = src_rgb + ((size_t)cy0 * src_w + cx0) * 3u;
            const uint8_t *p10 = src_rgb + ((size_t)cy0 * src_w + cx1) * 3u;
            const uint8_t *p01 = src_rgb + ((size_t)cy1 * src_w + cx0) * 3u;
            const uint8_t *p11 = src_rgb + ((size_t)cy1 * src_w + cx1) * 3u;
            uint8_t rgb[3];
            for (int c = 0; c < 3; c++) {
                double top = p00[c] * (1.0 - wx) + p10[c] * wx;
                double bot = p01[c] * (1.0 - wx) + p11[c] * wx;
                double v = top * (1.0 - wy) + bot * wy;
                rgb[c] = (uint8_t)(v < 0.0 ? 0 : (v > 255.0 ? 255 : (int)(v + 0.5)));
            }
            px[(size_t)y * COVER_OUT_W + x] = ((uint32_t)rgb[0] << 16) | ((uint32_t)rgb[1] << 8) | rgb[2];
        }
    }

    char path[512];
    snprintf(path, sizeof path, GAMES_DIR "/%s/cover.bmp", g.games[i]);
    bool ok = rsl_write_bmp(path, px, COVER_OUT_W, COVER_OUT_H, err, errlen);
    free(px);
    return ok;
}

/* "cover": "cover.bmp", "cover_custom": true in the game's
 * manifest.json; creates a minimal manifest.json if the game was copied manually. */
static bool name_in_manifest(int i, char *err, size_t errlen)
{
    char path[512];
    struct stat st;
    snprintf(path, sizeof path, GAMES_DIR "/%s/manifest.json", g.games[i]);
    cJSON *j = NULL;
    if (stat(path, &st) == 0) {
        char *text = rsl_read_file(path, 1 << 20, NULL, err, errlen);
        j = text ? cJSON_Parse(text) : NULL;
        free(text);
        if (!cJSON_IsObject(j)) {
            cJSON_Delete(j);
            snprintf(err, errlen, "manifest.json 无法读取");
            return false;
        }
    } else {
        j = cJSON_CreateObject();
        if (!j) {
            snprintf(err, errlen, rsl_ui_tr("内存不足"));
            return false;
        }
        cJSON_AddStringToObject(j, "format", "renpy-switch-loader/manifest");
        cJSON_AddNumberToObject(j, "version", 1);
        cJSON_AddStringToObject(j, "id", g.games[i]);
        cJSON_AddStringToObject(j, "title", title(i));
        cJSON_AddStringToObject(j, "runtime", g.info[i].runtime[0] ? g.info[i].runtime : "renpy-8.5.3");
        cJSON_AddStringToObject(j, "status", "experimental");
        cJSON_AddItemToObject(j, "reasons", cJSON_CreateArray());
        cJSON_AddItemToObject(j, "warnings", cJSON_CreateArray());
        cJSON_AddFalseToObject(j, "recompile");
        cJSON *perf = cJSON_CreateObject();
        cJSON_AddItemToObject(perf, "issues", cJSON_CreateArray());
        cJSON_AddItemToObject(j, "performance", perf);
    }

    cJSON_DeleteItemFromObject(j, "cover");
    cJSON_AddStringToObject(j, "cover", "cover.bmp");
    cJSON_DeleteItemFromObject(j, "cover_custom");
    cJSON_AddTrueToObject(j, "cover_custom");
    char *out = cJSON_Print(j);
    bool ok = out && rsl_write_atomic(path, out, strlen(out), err, errlen);
    if (!out)
        snprintf(err, errlen, rsl_ui_tr("内存不足"));
    free(out);
    cJSON_Delete(j);
    return ok;
}

static void use_picture(void)
{
    int i = g.game;
    char err[128] = "";
    if (!save_cropped_bmp(i, err, sizeof err) || !name_in_manifest(i, err, sizeof err)) {
        status(ST_BAD, rsl_ui_tr("图片未保存：%s"), err);
        return;
    }
    read_manifest(g.games[i], &g.info[i]);
    char path[sizeof g.info[i].cover];
    snprintf(path, sizeof path, GAMES_DIR "/%s/cover.bmp", g.games[i]);
    strcpy(g.info[i].cover, path);
    ui_art_changed(i);
    back_to_details();
    status(ST_OK, rsl_ui_tr("已为 %s 保存封面。"), title(i));
}

void picture_action(rsl_action a)
{
    if (!src_rgb)
        return;
    float scale = base_scale * zoom;
    switch (a) {
    case RSL_ACT_LEFT:
        pan_x -= PAN_STEP_PX / scale;
        clamp_view();
        pic_dirty = true;
        break;
    case RSL_ACT_RIGHT:
        pan_x += PAN_STEP_PX / scale;
        clamp_view();
        pic_dirty = true;
        break;
    case RSL_ACT_UP:
        pan_y -= PAN_STEP_PX / scale;
        clamp_view();
        pic_dirty = true;
        break;
    case RSL_ACT_DOWN:
        pan_y += PAN_STEP_PX / scale;
        clamp_view();
        pic_dirty = true;
        break;
    case RSL_ACT_ROLLBACK:
        zoom -= 0.1f;
        clamp_view();
        pic_dirty = true;
        break;
    case RSL_ACT_ROLLFORWARD:
        zoom += 0.1f;
        clamp_view();
        pic_dirty = true;
        break;
    case RSL_ACT_HIDE_UI:
        zoom = 1.0f;
        pan_x = 0.0f;
        pan_y = 0.0f;
        clamp_view();
        pic_dirty = true;
        break;
    case RSL_ACT_CONFIRM:
        use_picture();
        break;
    case RSL_ACT_BACK:
        back_to_details();
        break;
    default:
        break;
    }
}
