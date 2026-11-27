#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zip_unpack.h"

static void progress_cb(const rsl_zip_progress *p, void *ctx)
{
    (void)ctx;
    printf("[PROGRESS] %s | %u/%u | %llu/%llu | %s\n",
           p->stage, p->done_files, p->total_files,
           (unsigned long long)p->done_bytes, (unsigned long long)p->total_bytes,
           p->current_file);
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <zip_path> <games_dir> [delete_zip:0|1]\n", argv[0]);
        return 2;
    }
    bool del = argc > 3 && strcmp(argv[3], "1") == 0;
    rsl_zip_result res;
    bool ok = rsl_zip_unpack(argv[1], argv[2], del, NULL, progress_cb, NULL, &res);
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", res.error);
        return 1;
    }
    printf("OK: id=%s title=%s ver=%s py=%d runtime=%s extracted=%u packed=%u skipped=%u\n",
           res.game_id, res.title, res.renpy_version[0] ? res.renpy_version : "null",
           res.python_gen, res.runtime, res.extracted_files, res.packed_files, res.skipped_files);
    return 0;
}
