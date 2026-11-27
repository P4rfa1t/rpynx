/* Background ZIP unpacker worker (+ clean cancel). */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <switch.h>

#include "launcher.h"
#include "log.h"
#include "ui.h"
#include "zip_unpack.h"

static struct {
    bool active;
    bool joined;
    pthread_t th;
    pthread_mutex_t lock;
    volatile bool cancel;
    char zip_path[512];
    rsl_unpack_status st;
} uwork = { .lock = PTHREAD_MUTEX_INITIALIZER, .joined = true };

static void on_progress(const rsl_zip_progress *p, void *ctx)
{
    (void)ctx;
    pthread_mutex_lock(&uwork.lock);
    uwork.st.done_bytes = p->done_bytes;
    uwork.st.total_bytes = p->total_bytes;
    uwork.st.done_files = p->done_files;
    uwork.st.total_files = p->total_files;
    snprintf(uwork.st.file, sizeof uwork.st.file, "%s", p->current_file);
    snprintf(uwork.st.stage, sizeof uwork.st.stage, "%s", p->stage);
    pthread_mutex_unlock(&uwork.lock);
}

static void *unpack_main(void *arg)
{
    (void)arg;
    rsl_zip_result res;
    bool ok = rsl_zip_unpack(uwork.zip_path, GAMES_DIR, true, &uwork.cancel,
                             on_progress, NULL, &res);
    pthread_mutex_lock(&uwork.lock);
    uwork.st.ok = ok;
    uwork.st.cancelled = uwork.cancel && !ok;
    snprintf(uwork.st.error, sizeof uwork.st.error, "%s", res.error);
    snprintf(uwork.st.out_id, sizeof uwork.st.out_id, "%s", res.game_id);
    snprintf(uwork.st.runtime, sizeof uwork.st.runtime, "%s", res.runtime);
    if (res.title[0])
        snprintf(uwork.st.title, sizeof uwork.st.title, "%s", res.title);
    uwork.st.running = false;
    uwork.st.done = true;
    pthread_mutex_unlock(&uwork.lock);
    return NULL;
}

void start_unpack(int i)
{
    if (uwork.active)
        return;

    go(SCR_UNPACK);
    memset(&uwork.st, 0, sizeof uwork.st);
    uwork.cancel = false;
    uwork.st.running = true;
    uwork.st.done = false;
    uwork.st.total_bytes = g.info[i].zip_size;
    snprintf(uwork.st.zip_name, sizeof uwork.st.zip_name, "%s", g.games[i]);
    snprintf(uwork.st.title, sizeof uwork.st.title, "%s", title(i));
    snprintf(uwork.st.stage, sizeof uwork.st.stage, "%s", rsl_ui_tr("正在准备解压"));
    snprintf(uwork.zip_path, sizeof uwork.zip_path, GAMES_DIR "/%s", g.games[i]);
    g.unpack = uwork.st;

    appletSetAutoSleepDisabled(true);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 1024 * 1024);
    int rc = pthread_create(&uwork.th, &attr, unpack_main, NULL);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        appletSetAutoSleepDisabled(false);
        go_library();
        status(ST_BAD, rsl_ui_tr("无法启动解压线程"));
        return;
    }
    uwork.active = true;
    uwork.joined = false;
    rsl_log("unpack: started %s", uwork.zip_path);
}

void cancel_unpack(void)
{
    if (!uwork.active)
        return;
    uwork.cancel = true;
    status(ST_INFO, rsl_ui_tr("正在取消解压并清理临时文件……"));
}

void unpack_tick(void)
{
    if (!uwork.active)
        return;
    pthread_mutex_lock(&uwork.lock);
    g.unpack = uwork.st;
    bool finished = uwork.st.done;
    pthread_mutex_unlock(&uwork.lock);

    if (!finished)
        return;

    if (!uwork.joined) {
        pthread_join(uwork.th, NULL);
        uwork.joined = true;
    }
    uwork.active = false;
    appletSetAutoSleepDisabled(false);

    scan_games();
    for (int k = 0; k < MAX_GAMES; k++)
        ui_art_changed(k);

    if (g.unpack.ok) {
        int found = -1;
        for (int k = 0; k < g.game_count; k++) {
            if (strcmp(g.games[k], g.unpack.out_id) == 0) {
                found = k;
                break;
            }
        }
        rsl_log("unpack: %s -> games/%s (runtime %s)", g.unpack.zip_name, g.unpack.out_id, g.unpack.runtime);
        if (found >= 0) {
            m.lib_sel = g.game = found;
            go(SCR_DETAIL);
            status(ST_OK, rsl_ui_tr("已完成解压：%s（运行时 %s）。可按 A 启动。"),
                   title(found), g.info[found].runtime);
        } else {
            go_library();
            status(ST_OK, rsl_ui_tr("已完成解压：%s"), g.unpack.title);
        }
    } else if (g.unpack.cancelled) {
        rsl_log("unpack: cancelled %s", g.unpack.zip_name);
        go_library();
        status(ST_INFO, rsl_ui_tr("已取消解压，原 .zip 文件已保留。"));
    } else {
        rsl_log("unpack: failed %s: %s", g.unpack.zip_name, g.unpack.error);
        go_library();
        status(ST_BAD, rsl_ui_tr("解压失败：%s"), g.unpack.error);
    }
}

void close_unpack(void)
{
    if (!uwork.active)
        return;
    uwork.cancel = true;
    if (!uwork.joined) {
        pthread_join(uwork.th, NULL);
        uwork.joined = true;
    }
    uwork.active = false;
    appletSetAutoSleepDisabled(false);
}
