
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>

static char *get_test_dir(void) {
    static char *d = NULL;
    if (d == NULL) {
        d = getenv("LSHW_TEST_DIR");
        if (d == NULL) return NULL;
        // convert to absolute path, makes things easier below
        d = realpath(d, NULL);
    }
    return d;
}

static char *redirected_path(const char *pathname) {
    char *test_dir = get_test_dir();
    if (test_dir == NULL || test_dir[0] == '\0')
        return NULL;
    char *abs;
    if (pathname[0] != '/') {
        // relative path, make it absolute
        char *cwd = get_current_dir_name();
        abs = malloc(strlen(cwd) + strlen(pathname) + 2);
        sprintf(abs, "%s/%s", cwd, pathname);
        free(cwd);
    } else {
        abs = strdup(pathname);
    }
    if (strcmp(abs, "/proc/cpuinfo") == 0 ||
            strcmp(abs, "/proc/sys/abi") == 0 ||
            strncmp(abs, "/proc/sys/kernel/", 17) == 0 ||
            strncmp(abs, "/dev/", 5) == 0 ||
            strncmp(abs, "/sys/", 5) == 0) {
        char *redirected = malloc(strlen(test_dir) + strlen(abs) + 2);
        sprintf(redirected, "%s%s", test_dir, abs);
        free(abs);
        return redirected;
    } else {
        free(abs);
        return NULL;
    }
}

static char *redirected_sysconf_path(int name) {
    char *test_dir = get_test_dir();
    if (test_dir == NULL || test_dir[0] == '\0')
        return NULL;
    char *path = malloc(strlen(test_dir) + 12);
    sprintf(path, "%s/sysconf/%d", test_dir, name);
    return path;
}

int chdir(const char *path) {
    static int (*real_func)(const char *);
    if (real_func == NULL) {
        real_func = dlsym(RTLD_NEXT, "chdir");
        if (real_func == NULL) {
            fprintf(stderr, "wrapper.so: failed to load real 'chdir': %s\n", dlerror());
            abort();
        }
    }
    char *p = redirected_path(path);
    if (p != NULL) {
        fprintf(stderr, "wrapper.so: chdir() redirected to %s\n", p);
        int result = real_func(p);
        free(p);
        return result;
    } else {
        return real_func(path);
    }
}

int access(const char *pathname, int mode) {
    static int (*real_func)(const char *, int);
    if (real_func == NULL) {
        real_func = dlsym(RTLD_NEXT, "access");
        if (real_func == NULL) {
            fprintf(stderr, "wrapper.so: failed to load real 'access': %s\n", dlerror());
            abort();
        }
    }
    char *p = redirected_path(pathname);
    if (p != NULL) {
        fprintf(stderr, "wrapper.so: access() redirected to %s\n", p);
        int result = real_func(p, mode);
        free(p);
        return result;
    } else {
        return real_func(pathname, mode);
    }
}

int open(const char *pathname, int flags, ... /* mode_t mode */) {
    va_list args;
    va_start(args, flags);
    mode_t mode = va_arg(args, mode_t);
    va_end(args);
    static int (*real_func)(const char *, int, mode_t);
    if (real_func == NULL) {
        real_func = dlsym(RTLD_NEXT, "open");
        if (real_func == NULL) {
            fprintf(stderr, "wrapper.so: failed to load real 'open': %s\n", dlerror());
            abort();
        }
    }
    char *p = redirected_path(pathname);
    if (p != NULL) {
        // Reject any write attempts
        if ((flags & O_WRONLY) != 0 || (flags & O_RDWR) != 0 ||
                (flags & O_APPEND) != 0 || (flags & O_CREAT) != 0) {
            fprintf(stderr, "wrapper.so: open() denied write access to "
                    "redirected path %s\n", p);
            errno = EROFS;
            return -1;
        }
        fprintf(stderr, "wrapper.so: open() redirected to %s\n", p);
        int result = real_func(p, flags, 0);
        free(p);
        return result;
    } else {
        return real_func(pathname, flags, mode);
    }
}

FILE *fopen(const char *path, const char *mode) {
    static FILE *(*real_func)(const char *, const char *);
    if (real_func == NULL) {
        real_func = dlsym(RTLD_NEXT, "fopen");
        if (real_func == NULL) {
            fprintf(stderr, "wrapper.so: failed to load real 'fopen': %s\n", dlerror());
            abort();
        }
    }
    char *p = redirected_path(path);
    if (p != NULL) {
        // Reject any write attempts
        if (strcmp(mode, "r") != 0) {
            fprintf(stderr, "wrapper.so: fopen() denied write access to "
                    "redirected path %s\n", p);
            errno = EROFS;
            return NULL;
        }
        fprintf(stderr, "wrapper.so: fopen() redirected to %s\n", p);
        FILE *result = real_func(p, mode);
        free(p);
        return result;
    } else {
        return real_func(path, mode);
    }
}

long sysconf(int name) {
    static long (*real_func)(int);
    if (real_func == NULL) {
        real_func = dlsym(RTLD_NEXT, "sysconf");
        if (real_func == NULL) {
            fprintf(stderr, "wrapper.so: failed to load real 'sysconf': %s\n", dlerror());
            abort();
        }
    }
    char *p = redirected_sysconf_path(name);
    if (p != NULL) {
        FILE *f = fopen(p, "r");
        if (f == NULL) {
            fprintf(stderr, "wrapper.so: failed to fopen %s\n", p);
            abort();
        }
        long value;
        if (fscanf(f, "%ld", &value) != 1) {
            fprintf(stderr, "wrapper.so: failed to read value from %s\n", p);
            abort();
        }
        fclose(f);
        fprintf(stderr, "wrapper.so: sysconf() returning value %ld from %s\n",
                value, p);
        free(p);
        return value;
    } else {
        return real_func(name);
    }
}
