#ifndef RSL_FORWARDER_H
#define RSL_FORWARDER_H

#include <stdbool.h>
#include <stddef.h>

/* Build and install a HOME Menu forwarder shortcut directly into NCM/NS
 * (SdCard storage) for `game_dir`, passing `--launch <game_dir>` to the
 * loader. Does not require prod.keys on SD (derives NCA header key via
 * splCrypto). Returns true on success; on failure writes a concise message
 * into `err`. */
bool rsl_forwarder_install(const char *nro_path, const char *game_dir, const char *game_title,
                           const char *cover_path, char *err, size_t errlen);

#endif
