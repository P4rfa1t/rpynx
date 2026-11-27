/* Minimal POSIX stubs that newlib declares for CPython 2.7's posixmodule
 * and pwdmodule, where Horizon provides no multi-process or user database. */
#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

FILE *popen(const char *command, const char *type)
{
    (void)command;
    (void)type;
    errno = ENOSYS;
    return NULL;
}

int pclose(FILE *stream)
{
    (void)stream;
    errno = ENOSYS;
    return -1;
}

int execv(const char *path, char *const argv[])
{
    (void)path;
    (void)argv;
    errno = ENOSYS;
    return -1;
}

int pipe(int fildes[2])
{
    (void)fildes;
    errno = ENOSYS;
    return -1;
}

char *ttyname(int fildes)
{
    (void)fildes;
    errno = ENOTTY;
    return NULL;
}

mode_t umask(mode_t mask)
{
    (void)mask;
    return 022;
}

pid_t getppid(void) { return 1; }
uid_t getuid(void) { return 0; }
uid_t geteuid(void) { return 0; }
gid_t getgid(void) { return 0; }
gid_t getegid(void) { return 0; }

struct passwd *getpwuid(uid_t uid)
{
    (void)uid;
    errno = ENOENT;
    return NULL;
}

struct passwd *getpwnam(const char *name)
{
    (void)name;
    errno = ENOENT;
    return NULL;
}
