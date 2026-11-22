#ifndef RSL_MTP_USB_H
#define RSL_MTP_USB_H

#include "ftpd.h"

bool rsl_mtp_start(const char *root, char *error, size_t error_size);
void rsl_mtp_stop(void);
void rsl_mtp_get(rsl_ftpd_status *status);

#endif
