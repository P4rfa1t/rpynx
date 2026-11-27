#ifndef RSL_COVER_EXTRACT_H
#define RSL_COVER_EXTRACT_H

#include <stdbool.h>

/* Extract and render a 300x450 cover BMP for a Ren'Py game folder (containing game/)
 * from loose images or standard RPA-2.0/3.0 archives inside game/.
 * Returns true if out_bmp was written successfully. */
bool rsl_extract_game_cover(const char *game_dir, const char *out_bmp);

#endif
