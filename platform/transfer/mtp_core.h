#ifndef RSL_MTP_CORE_H
#define RSL_MTP_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct MtpCore MtpCore;
typedef bool (*MtpRead)(void *, void *, size_t);
typedef bool (*MtpWrite)(void *, const void *, size_t);
typedef void (*MtpProgress)(void *, const char *, uint64_t, uint64_t, bool);

MtpCore *mtp_core_new(const char *root, MtpRead read, MtpWrite write,
                      MtpProgress progress, void *context);
bool mtp_core_step(MtpCore *core);
void mtp_core_free(MtpCore *core);

#endif
