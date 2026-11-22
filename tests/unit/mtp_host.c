#include <stdio.h>

#include "mtp_core.h"

static bool input(void *user, void *buffer, size_t size)
{
    (void)user;
    return fread(buffer, 1, size, stdin) == size;
}

static bool output(void *user, const void *buffer, size_t size)
{
    (void)user;
    return fwrite(buffer, 1, size, stdout) == size && fflush(stdout) == 0;
}

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    MtpCore *core = mtp_core_new(argv[1], input, output, NULL, NULL);
    if (!core) return 2;
    while (mtp_core_step(core)) {}
    mtp_core_free(core);
    return 0;
}
