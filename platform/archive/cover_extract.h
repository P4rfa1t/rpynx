#ifndef RSL_COVER_EXTRACT_H
#define RSL_COVER_EXTRACT_H

#include <stdbool.h>
#include <stdint.h>

/* Extract and render a 300x450 cover BMP for a Ren'Py game folder (containing game/)
 * from loose images or standard RPA-2.0/3.0 archives inside game/.
 * Returns true if out_bmp was written successfully. */
bool rsl_extract_game_cover(const char *game_dir, const char *out_bmp);

/* Extract the best uncropped cover artwork from game_dir (compositing logo if
 * present) as a newly allocated RGB24 buffer (`*out_w x *out_h x 3`). */
bool rsl_extract_game_cover_raw(const char *game_dir, uint8_t **out_rgb, int *out_w, int *out_h);

#endif
