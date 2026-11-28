/* The game library: scanning games/, reading each manifest.json, the
 * loader's own settings (config/loader.json) and launching a game. */
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include <switch.h>

#include "cJSON.h"
#include "cover_extract.h"
#include "forwarder.h"
#include "fs.h"
#include "launcher.h"
#include "log.h"
#include "ui.h"
#include "zip_unpack.h"

const char *title(int i)
{
    return g.info[i].title[0] ? g.info[i].title : g.games[i];
}

/* --------------------------------------------------------------- settings */

/* The loader's own settings; the controller files stay separate (their
 * parser rejects unknown keys). */
void load_settings(void)
{
    char err[128];
    char *text = rsl_read_file(LOADER_CFG, 4096, NULL, err, sizeof err);
    cJSON *j = text ? cJSON_Parse(text) : NULL;
    free(text);
    const char *layout = cJSON_GetStringValue(cJSON_GetObjectItem(j, "library_layout"));
    const char *accent = cJSON_GetStringValue(cJSON_GetObjectItem(j, "accent"));
    cJSON *hue = cJSON_GetObjectItem(j, "accent_hue");
    g.layout = layout && !strcmp(layout, "grid") ? LAYOUT_GRID : LAYOUT_ROW;
    g.accent = accent && accent[0] == '#' ? (uint32_t)strtoul(accent + 1, NULL, 16) & 0xffffff :
               accent && !strcmp(accent, "blue") ? 0x25d0f2 : DEFAULT_ACCENT;
    g.accent_hue = cJSON_IsNumber(hue) ? ((int)hue->valuedouble % 360 + 360) % 360 : DEFAULT_HUE;
    m.help_seen = cJSON_IsTrue(cJSON_GetObjectItem(j, "help_seen"));
    m.shortcut_prompted = cJSON_IsTrue(cJSON_GetObjectItem(j, "shortcut_prompted"));
    cJSON_Delete(j);
}

void save_settings(void)
{
    static const char *const layouts[] = { "row", "grid" };
    char text[320], err[128];
    int n = snprintf(text, sizeof text,
                     "{\n  \"library_layout\": \"%s\",\n"
                     "  \"accent\": \"#%06x\",\n  \"accent_hue\": %d,\n"
                     "  \"help_seen\": %s,\n  \"shortcut_prompted\": %s\n}\n",
                     layouts[g.layout], (unsigned)g.accent, g.accent_hue,
                     m.help_seen ? "true" : "false",
                     m.shortcut_prompted ? "true" : "false");
    if (!rsl_write_atomic(LOADER_CFG, text, (size_t)n, err, sizeof err))
        status(ST_BAD, rsl_ui_tr("设置未保存：%s"), err);
}

/* ------------------------------------------------------------------ library */

static bool has_zip_ext(const char *name)
{
    size_t n = strlen(name);
    if (n <= 4)
        return false;
    const char *p = name + n - 4;
    return (p[0] == '.') &&
           (p[1] == 'z' || p[1] == 'Z') &&
           (p[2] == 'i' || p[2] == 'I') &&
           (p[3] == 'p' || p[3] == 'P');
}

/* Recognise ZIP archives even when Windows Explorer / MTP stripped the .zip suffix. */
static bool has_zip_magic(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    unsigned char sig[4];
    bool ok = fread(sig, 1, 4, f) == 4 &&
              sig[0] == 0x50 && sig[1] == 0x4b && sig[2] == 0x03 && sig[3] == 0x04;
    fclose(f);
    return ok;
}

/* A file the manifest names, inside the game's folder; "" if it is not
 * there or the name tries to leave the folder. */
static void game_file(char *out, size_t size, const char *game, const char *name)
{
    struct stat st;
    out[0] = '\0';
    if (!name || !name[0] || name[0] == '/' || strstr(name, "..") || strchr(name, ':') || strlen(name) > 100)
        return;
    snprintf(out, size, GAMES_DIR "/%.127s/%.100s", game, name);
    if (stat(out, &st) != 0 || !S_ISREG(st.st_mode))
        out[0] = '\0';
}

/* Detect Ren'Py <= 7 / Python 2 games copied by hand without a manifest.json,
 * when runtimes/renpy-7.8.7.nro is installed. */
static void detect_manual_runtime(const char *game, struct game_info *gi)
{
    char p[512], err[128];
    struct stat st;

    char *opt = NULL;
    snprintf(p, sizeof p, GAMES_DIR "/%s/game/options.rpy", game);
    opt = rsl_read_file(p, 65536, NULL, err, sizeof err);
    if (opt) {
        rsl_extract_options_title(opt, gi->title, sizeof gi->title);
        free(opt);
    }
    game_file(gi->cover, sizeof gi->cover, game, "cover.jpg");
    if (!gi->cover[0])
        game_file(gi->cover, sizeof gi->cover, game, "cover.bmp");
    if (!gi->cover[0])
        game_file(gi->cover, sizeof gi->cover, game, "game/gui/main_menu.jpg");

    snprintf(p, sizeof p, ROOT "/runtimes/renpy-7.8.7.nro");
    if (stat(p, &st) != 0)
        return;

    bool is_py2 = false;
    snprintf(p, sizeof p, GAMES_DIR "/%s/python27.dll", game);
    if (stat(p, &st) == 0)
        is_py2 = true;

    if (!is_py2) {
        snprintf(p, sizeof p, GAMES_DIR "/%s/lib", game);
        DIR *d = opendir(p);
        if (d) {
            struct dirent *e;
            while ((e = readdir(d))) {
                if (strncmp(e->d_name, "py2-", 4) == 0 ||
                    strcmp(e->d_name, "python2.7") == 0 ||
                    strcmp(e->d_name, "pythonlib2.7") == 0) {
                    is_py2 = true;
                    break;
                }
            }
            closedir(d);
        }
    }

    if (!is_py2) {
        snprintf(p, sizeof p, GAMES_DIR "/%s/game/script_version.txt", game);
        char *ver = rsl_read_file(p, 256, NULL, err, sizeof err);
        if (ver) {
            if (strncmp(ver, "(7,", 3) == 0 || strncmp(ver, "(6,", 3) == 0)
                is_py2 = true;
            free(ver);
        }
    }

    if (!is_py2) {
        snprintf(p, sizeof p, GAMES_DIR "/%s/renpy/__init__.py", game);
        char *init = rsl_read_file(p, 65536, NULL, err, sizeof err);
        if (init) {
            if ((strstr(init, "version_tuple = (7,") || strstr(init, "version_tuple = (6,")) &&
                !strstr(init, "version_tuple = (8,"))
                is_py2 = true;
            free(init);
        }
    }

    if (is_py2)
        snprintf(gi->runtime, sizeof gi->runtime, "renpy-7.8.7");
}

/* What a game needs, from the manifest.json the importer writes
 * (docs/IMPORTER.md). A game copied by hand has none and gets the default
 * runtime (auto-detecting Python 2 / Ren'Py 7 if renpy-7.8.7.nro is present).
 * Fills gi->problem when the game must not start. */
void read_manifest(const char *game, struct game_info *gi)
{
    char path[512], err[128];
    struct stat st;
    memset(gi, 0, sizeof *gi);
    strcpy(gi->runtime, "renpy-8.5.3");
    snprintf(path, sizeof path, GAMES_DIR "/%s/manifest.json", game);
    char *text = rsl_read_file(path, 1 << 20, NULL, err, sizeof err);
    if (!text)
        detect_manual_runtime(game, gi);
    if (text) {
        cJSON *j = cJSON_Parse(text);
        free(text);
        const char *format = cJSON_GetStringValue(cJSON_GetObjectItem(j, "format"));
        const char *state = cJSON_GetStringValue(cJSON_GetObjectItem(j, "status"));
        const char *runtime = cJSON_GetStringValue(cJSON_GetObjectItem(j, "runtime"));
        const char *version = cJSON_GetStringValue(cJSON_GetObjectItem(j, "renpy_version"));
        const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(j, "title"));
        const char *reason = cJSON_GetStringValue(cJSON_GetArrayItem(cJSON_GetObjectItem(j, "reasons"), 0));
        snprintf(gi->title, sizeof gi->title, "%s", name ? name : "");
        snprintf(gi->renpy, sizeof gi->renpy, "%s", version ? version : "?");
        snprintf(gi->state, sizeof gi->state, "%s", state ? state : "?");
        game_file(gi->cover, sizeof gi->cover, game, cJSON_GetStringValue(cJSON_GetObjectItem(j, "cover")));
        gi->recompile = cJSON_IsTrue(cJSON_GetObjectItem(j, "recompile"));
        cJSON *issues = cJSON_GetObjectItem(cJSON_GetObjectItem(j, "performance"), "issues");
        gi->issues = cJSON_GetArraySize(issues);
        for (int i = 0; i < 3 && i < gi->issues; i++) {
            const char *t = cJSON_GetStringValue(cJSON_GetArrayItem(issues, i));
            snprintf(gi->issue[i], sizeof gi->issue[i], "%s", t ? t : "?");
        }
        gi->runtime[0] = '\0';
        if (!format || strcmp(format, "renpy-switch-loader/manifest") != 0)
            snprintf(gi->problem, sizeof gi->problem, "manifest.json 无法读取；请重新导出此游戏。");
        else if (!state || strcmp(state, "unsupported") == 0 || !runtime)
            snprintf(gi->problem, sizeof gi->problem, rsl_ui_tr("此处无法运行：%.130s"), reason ? reason : "unsupported");
        else if (!runtime[0] || strlen(runtime) >= sizeof gi->runtime ||
                 strspn(runtime, "abcdefghijklmnopqrstuvwxyz0123456789.-") != strlen(runtime))
            snprintf(gi->problem, sizeof gi->problem, "manifest.json 中的运行时名称无效。");
        else
            strcpy(gi->runtime, runtime);
        cJSON_Delete(j);
    }
    if (!gi->runtime[0])
        return;
    char runtime[sizeof gi->runtime], nro[sizeof gi->nro];
    strcpy(runtime, gi->runtime);
    snprintf(nro, sizeof nro, ROOT "/runtimes/%s.nro", runtime);
    /* renpy-8.5.3 is the runtime installed as renpy-runtime.nro. */
    if (stat(nro, &st) != 0 && strcmp(runtime, "renpy-8.5.3") == 0)
        snprintf(nro, sizeof nro, "%s", RUNTIME_NRO);
    strcpy(gi->nro, nro);
    gi->runtime_ok = stat(nro, &st) == 0;
    if (!gi->runtime_ok)
        snprintf(gi->problem, sizeof gi->problem, rsl_ui_tr("需要运行时 %s；请将其复制到 %.90s"), runtime, nro);
}

#define COVER_AUTO_VER 4

static void try_auto_cover(const char *game, struct game_info *gi)
{
    if (gi->is_zip)
        return;
    char gdir[512], mpath[544], cover_bmp[544], err[128];
    struct stat st;
    snprintf(gdir, sizeof gdir, GAMES_DIR "/%s", game);
    snprintf(mpath, sizeof mpath, "%s/manifest.json", gdir);
    snprintf(cover_bmp, sizeof cover_bmp, "%s/cover.bmp", gdir);

    char *text = stat(mpath, &st) == 0 ? rsl_read_file(mpath, 1 << 20, NULL, err, sizeof err) : NULL;
    cJSON *j = text ? cJSON_Parse(text) : NULL;
    free(text);
    if (j) {
        if (cJSON_IsTrue(cJSON_GetObjectItem(j, "cover_custom"))) {
            cJSON_Delete(j);
            return;
        }
        cJSON *ver_item = cJSON_GetObjectItem(j, "cover_auto_ver");
        int auto_ver = cJSON_IsNumber(ver_item) ? ver_item->valueint : 0;
        const char *cur_cover = cJSON_GetStringValue(cJSON_GetObjectItem(j, "cover"));
        bool has_importer_cover = gi->cover[0] && cur_cover && strcmp(cur_cover, "cover.bmp") != 0;
        if (has_importer_cover || (auto_ver >= COVER_AUTO_VER &&
            (gi->cover[0] || cJSON_IsTrue(cJSON_GetObjectItem(j, "cover_auto_tried"))))) {
            cJSON_Delete(j);
            return;
        }
    } else if (gi->cover[0]) {
        return;
    }

    bool ok = rsl_extract_game_cover(gdir, cover_bmp);
    if (ok) {
        snprintf(gi->cover, sizeof gi->cover, "%.*s", (int)(sizeof gi->cover - 1), cover_bmp);
        if (!cJSON_IsObject(j)) {
            cJSON_Delete(j);
            j = cJSON_CreateObject();
            if (j) {
                cJSON_AddStringToObject(j, "format", "renpy-switch-loader/manifest");
                cJSON_AddNumberToObject(j, "version", 1);
                cJSON_AddStringToObject(j, "id", game);
                cJSON_AddStringToObject(j, "title", gi->title[0] ? gi->title : game);
                cJSON_AddStringToObject(j, "runtime", gi->runtime[0] ? gi->runtime : "renpy-8.5.3");
                cJSON_AddStringToObject(j, "status", "experimental");
                cJSON_AddItemToObject(j, "reasons", cJSON_CreateArray());
                cJSON_AddItemToObject(j, "warnings", cJSON_CreateArray());
                cJSON_AddFalseToObject(j, "recompile");
                cJSON *perf = cJSON_CreateObject();
                cJSON_AddItemToObject(perf, "issues", cJSON_CreateArray());
                cJSON_AddItemToObject(j, "performance", perf);
            }
        }
        if (cJSON_IsObject(j)) {
            cJSON_DeleteItemFromObject(j, "cover");
            cJSON_AddStringToObject(j, "cover", "cover.bmp");
            cJSON_DeleteItemFromObject(j, "cover_auto_tried");
            cJSON_AddTrueToObject(j, "cover_auto_tried");
            cJSON_DeleteItemFromObject(j, "cover_auto_ver");
            cJSON_AddNumberToObject(j, "cover_auto_ver", COVER_AUTO_VER);
            char *out = cJSON_Print(j);
            if (out) {
                rsl_write_atomic(mpath, out, strlen(out), err, sizeof err);
                free(out);
            }
        }
    } else if (cJSON_IsObject(j)) {
        cJSON_DeleteItemFromObject(j, "cover_auto_tried");
        cJSON_AddTrueToObject(j, "cover_auto_tried");
        cJSON_DeleteItemFromObject(j, "cover_auto_ver");
        cJSON_AddNumberToObject(j, "cover_auto_ver", COVER_AUTO_VER);
        char *out = cJSON_Print(j);
        if (out) {
            rsl_write_atomic(mpath, out, strlen(out), err, sizeof err);
            free(out);
        }
    }
    cJSON_Delete(j);
}

/* Users sometimes drop a game .zip directly into the loader root over MTP
 * instead of games/. Move such top-level .zip archives into games/ before
 * scanning so the library sees them right away. */
static void rescue_root_zips(void)
{
    DIR *rd = opendir(ROOT);
    if (!rd)
        return;
    rsl_mkdirs(GAMES_DIR);
    struct dirent *e;
    while ((e = readdir(rd))) {
        if (e->d_name[0] == '.' || !has_zip_ext(e->d_name) || strlen(e->d_name) >= sizeof g.games[0])
            continue;
        char src[512], dst[512];
        struct stat st;
        snprintf(src, sizeof src, ROOT "/%s", e->d_name);
        if (stat(src, &st) != 0 || !S_ISREG(st.st_mode))
            continue;
        snprintf(dst, sizeof dst, GAMES_DIR "/%s", e->d_name);
        if (stat(dst, &st) == 0)
            continue;
        if (rename(src, dst) == 0)
            rsl_log("library scan: moved misplaced zip %s -> %s", src, dst);
        else
            rsl_log("library scan: failed to move misplaced zip %s -> %s (%s)", src, dst, strerror(errno));
    }
    closedir(rd);
}

void scan_games(void)
{
    g.game_count = 0;
    rescue_root_zips();
    DIR *d = opendir(GAMES_DIR);
    if (!d) {
        status(ST_BAD, rsl_ui_tr("无法打开 %s"), GAMES_DIR);
        return;
    }
    struct dirent *e;
    while ((e = readdir(d)) && g.game_count < MAX_GAMES) {
        if (e->d_name[0] == '.')
            continue;
        if (strlen(e->d_name) >= sizeof g.games[0]) {
            rsl_log("library scan: skipped %s (name too long: %zu)", e->d_name, strlen(e->d_name));
            continue;
        }
        char p[512];
        struct stat st;
        snprintf(p, sizeof p, "%s/%s", GAMES_DIR, e->d_name);
        if (stat(p, &st) != 0) {
            rsl_log("library scan: skipped %s (stat failed: %s)", e->d_name, strerror(errno));
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            strcpy(g.games[g.game_count], e->d_name);
            snprintf(p, sizeof p, "%s/%s/game", GAMES_DIR, e->d_name);
            g.game_ok[g.game_count] = stat(p, &st) == 0 && S_ISDIR(st.st_mode);
            read_manifest(e->d_name, &g.info[g.game_count]);
            if (g.game_ok[g.game_count])
                try_auto_cover(e->d_name, &g.info[g.game_count]);
            g.game_count++;
        } else if (S_ISREG(st.st_mode)) {
            bool ext_zip = has_zip_ext(e->d_name);
            if (!ext_zip && !(st.st_size >= 22 && has_zip_magic(p))) {
                rsl_log("library scan: skipped %s (mode 0%o)", e->d_name, (unsigned)st.st_mode);
                continue;
            }
            struct game_info *gi = &g.info[g.game_count];
            memset(gi, 0, sizeof *gi);
            strcpy(g.games[g.game_count], e->d_name);
            g.game_ok[g.game_count] = true;
            gi->is_zip = true;
            gi->zip_size = (uint64_t)st.st_size;
            snprintf(gi->state, sizeof gi->state, "zip");
            snprintf(gi->runtime, sizeof gi->runtime, "%s", rsl_ui_tr("自动检测"));
            gi->runtime_ok = g.runtime_found;
            int nlen = (int)strlen(e->d_name);
            snprintf(gi->title, sizeof gi->title, "%.*s", ext_zip ? nlen - 4 : nlen, e->d_name);
            size_t tlen = strlen(gi->title);
            if (tlen > 4 && strcmp(gi->title + tlen - 4, ".app") == 0)
                gi->title[tlen - 4] = '\0';
            g.game_count++;
        } else {
            rsl_log("library scan: skipped %s (mode 0%o)", e->d_name, (unsigned)st.st_mode);
        }
    }
    closedir(d);
    rsl_log("library scan: %d entry(s)", g.game_count);
}

/* ------------------------------------------------------------------ actions */

/* Exit to `nro` (via the Homebrew loader). When `return_to_self` is true, ask
 * the runtime to chain-load back into this loader on exit; direct HOME-menu
 * shortcuts omit `--return` so quitting a game returns straight to HOME. */
static bool chain_load_ex(const char *nro, const char *args, bool return_to_self)
{
    struct stat st;
    char argv[1024];
    if (!envHasNextLoad()) {
        status(ST_BAD, "无法启动其他程序：请从 Homebrew Menu 启动加载器。");
        return false;
    }
    if (stat(nro, &st) != 0) {
        status(ST_BAD, rsl_ui_tr("未安装运行时：%s"), nro);
        return false;
    }
    /* libnx splits on spaces and strips "..." around an argument. */
    if (return_to_self)
        snprintf(argv, sizeof argv, "\"%s\" --return \"%s\" %s", nro, m.self_path, args);
    else
        snprintf(argv, sizeof argv, "\"%s\" %s", nro, args);
    Result rc = envSetNextLoad(nro, argv);
    if (R_FAILED(rc)) {
        status(ST_BAD, "envSetNextLoad 失败：0x%x", (unsigned)rc);
        return false;
    }
    rsl_log("chain-loading: %s", argv);
    m.quit = true;
    return true;
}

void chain_load(const char *nro, const char *args)
{
    chain_load_ex(nro, args, true);
}

int find_game_by_arg(const char *arg)
{
    if (!arg || !arg[0])
        return -1;
    char name[256];
    const char *p = arg;
    size_t prefix_len = strlen(GAMES_DIR "/");
    if (strncmp(p, GAMES_DIR "/", prefix_len) == 0)
        p += prefix_len;
    snprintf(name, sizeof name, "%s", p);
    size_t n = strlen(name);
    while (n > 0 && name[n - 1] == '/')
        name[--n] = '\0';
    if (!name[0])
        return -1;
    for (int i = 0; i < g.game_count; i++)
        if (strcmp(g.games[i], name) == 0)
            return i;
    for (int i = 0; i < g.game_count; i++)
        if (strcasecmp(g.games[i], name) == 0)
            return i;
    return -1;
}

bool launch_direct(const char *arg, bool return_to_loader)
{
    int i = find_game_by_arg(arg);
    if (i < 0) {
        rsl_log("direct launch: game not found: %s", arg ? arg : "");
        return false;
    }
    if (g.info[i].is_zip || !g.game_ok[i] || strchr(g.games[i], '"') || !g.full_memory || g.info[i].problem[0]) {
        rsl_log("direct launch: %s cannot start directly (zip=%d ok=%d full_mem=%d problem=%s)",
                g.games[i], g.info[i].is_zip, g.game_ok[i], g.full_memory, g.info[i].problem);
        return false;
    }
    char args[512];
    rsl_log("direct launch: %s -> runtime %s (%s)", g.games[i], g.info[i].runtime, g.info[i].nro);
    snprintf(args, sizeof args, "--game \"" GAMES_DIR "/%s\"", g.games[i]);
    return chain_load_ex(g.info[i].nro, args, return_to_loader);
}

void launch(int i)
{
    char args[512];
    if (g.info[i].is_zip) {
        start_unpack(i);
        return;
    }
    if (!g.game_ok[i]) {
        status(ST_BAD, rsl_ui_tr("%s 没有 game/ 文件夹；请复制整个游戏文件夹。"), g.games[i]);
    } else if (strchr(g.games[i], '"')) {
        status(ST_BAD, rsl_ui_tr("请重命名 %s：文件夹名不能包含引号。"), g.games[i]);
    } else if (!g.full_memory) {
        /* Applet mode (the loader opened from the Album): a Ren'Py game took the
         * whole console down with a fatal error, twice (hardware runs 8 and 26),
         * also with a 64 MB image cache. Games do not start here, no override;
         * the notice says how to get full memory. */
        rsl_log("%s: not started, applet mode (%llu MiB)", g.games[i], (unsigned long long)(g.mem_total >> 20));
        g.dialog_kind = DLG_APPLET;
        g.dialog_sel = 0;
        g.dialog_open = true;
    } else if (g.info[i].problem[0]) {
        status(ST_BAD, "%s: %s", title(i), g.info[i].problem);
    } else {
        rsl_log("%s: runtime %s (%s)", g.games[i], g.info[i].runtime, g.info[i].nro);
        snprintf(args, sizeof args, "--game \"" GAMES_DIR "/%s\"", g.games[i]);
        chain_load(g.info[i].nro, args);
    }
}

void delete_game(int i)
{
    if (i < 0 || i >= g.game_count)
        return;
    const char *entry = g.games[i];
    if (!entry[0] || !strcmp(entry, ".") || !strcmp(entry, "..") || strchr(entry, '/')) {
        status(ST_BAD, rsl_ui_tr("删除失败：%s"), "invalid game path");
        return;
    }
    char name[96], path[512], err[256] = "";
    bool is_zip = g.info[i].is_zip;
    snprintf(name, sizeof name, "%s", title(i));
    snprintf(path, sizeof path, GAMES_DIR "/%s", entry);
    if (!rsl_rmtree(path, err, sizeof err)) {
        status(ST_BAD, rsl_ui_tr("删除失败：%s"), err);
        return;
    }
    rsl_log("deleted game: %s", path);
    scan_games();
    for (int k = 0; k < MAX_GAMES; k++)
        ui_art_changed(k);
    if (m.lib_sel >= g.game_count)
        m.lib_sel = g.game_count > 0 ? g.game_count - 1 : 0;
    go_library();
    if (is_zip)
        status(ST_OK, rsl_ui_tr("已删除压缩包：%s。"), name);
    else
        status(ST_OK, rsl_ui_tr("已删除：%s（存档已保留）。"), name);
}

void create_shortcut(int i)
{
    if (i < 0 || i >= g.game_count)
        return;
    if (g.info[i].is_zip) {
        status(ST_INFO, rsl_ui_tr("请先解压此游戏后再设置封面或手柄。"));
        return;
    }
    if (!g.game_ok[i]) {
        status(ST_BAD, rsl_ui_tr("%s 没有 game/ 文件夹；请复制整个游戏文件夹。"), g.games[i]);
        return;
    }
    char err[256] = "";
    const char *cover_path = g.info[i].cover[0] ? g.info[i].cover : NULL;
    if (rsl_forwarder_install(m.self_path, g.games[i], title(i), cover_path, err, sizeof err)) {
        rsl_log("shortcut created: %s (%s)", g.games[i], title(i));
        status(ST_OK, rsl_ui_tr("已创建桌面快捷方式：%s（请按 HOME 查看）"), title(i));
    } else {
        rsl_log("shortcut failed: %s: %s", g.games[i], err);
        status(ST_BAD, rsl_ui_tr("创建快捷方式失败：%s"), err);
    }
}

void create_self_shortcut(void)
{
    char err[256] = "";
    if (rsl_forwarder_install(m.self_path, NULL, "rpynx", NULL, err, sizeof err)) {
        rsl_log("self shortcut created: %s", m.self_path);
        status(ST_OK, rsl_ui_tr("已创建加载器桌面快捷方式（请按 HOME 查看，从桌面启动可获得完整内存）"));
    } else {
        rsl_log("self shortcut failed: %s", err);
        status(ST_BAD, rsl_ui_tr("创建加载器快捷方式失败：%s"), err);
    }
}
