#ifndef RSL_FORWARDER_H
#define RSL_FORWARDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Decode a BMP, PNG, WebP, or JPEG file at `path` into a newly allocated
 * RGB24 buffer (3 bytes per pixel). Caller must free `*out_rgb`. */
bool rsl_decode_image_rgb(const char *path, uint8_t **out_rgb, int *out_w, int *out_h);

/* Build and install a HOME Menu forwarder shortcut directly into NCM/NS
 * (SdCard storage) for `nro_path`. When `game_dir` is non-empty, passes
 * `--launch <game_dir>`; when NULL or empty, launches `nro_path` directly and
 * falls back to the icon embedded in `nro_path`'s NRO ASET section. Does not
 * require prod.keys on SD (derives NCA header key via splCrypto). Returns true
 * on success; on failure writes a concise message into `err`. */
bool rsl_forwarder_install(const char *nro_path, const char *game_dir, const char *game_title,
                           const char *cover_path, char *err, size_t errlen);

#endif
