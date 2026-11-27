#ifndef RSL_ZIP_UNPACK_H
#define RSL_ZIP_UNPACK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t done_bytes;
    uint64_t total_bytes;
    unsigned done_files;
    unsigned total_files;
    char current_file[256];
    char stage[64];
} rsl_zip_progress;

typedef void (*rsl_zip_progress_cb)(const rsl_zip_progress *p, void *ctx);

typedef struct {
    char game_id[64];
    char title[96];
    char renpy_version[24];
    int python_gen;            /* 2 or 3 */
    char runtime[49];          /* "renpy-7.8.7" or "renpy-8.5.3" */
    unsigned extracted_files;
    unsigned packed_files;
    unsigned skipped_files;
    char error[256];
} rsl_zip_result;

/* Unpack zip_path into games_dir/<id>/ (via games_dir/.unpacking-<id>/).
 * If delete_zip is true, removes zip_path only after atomic rename succeeds.
 * cancel (optional) is polled during extraction; if set to true, aborts and
 * cleans up the staging directory while keeping zip_path intact. */
bool rsl_zip_unpack(const char *zip_path, const char *games_dir, bool delete_zip,
                    volatile bool *cancel, rsl_zip_progress_cb cb, void *ctx,
                    rsl_zip_result *out);

/* Parse config.name = ... from options.rpy text. Returns true if non-empty. */
bool rsl_extract_options_title(const char *text, char *out, size_t cap);

#endif
