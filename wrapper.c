
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <fcntl.h>
#include <glob.h>
#include <dlfcn.h>

static void info(const char *format, ...)
    __attribute__ ((format(printf, 1, 2)));
static void die(const char *format, ...)
    __attribute__ ((format(printf, 1, 2)));

static void info(const char *format, ...) {
    static bool verbose = false;
    static bool verbose_checked = false;
    if (!verbose_checked) {
        const char *env_value = getenv("LSHW_TEST_WRAPPER_VERBOSE");
        verbose = (env_value != NULL && strlen(env_value) > 0);
        verbose_checked = true;
    }
    if (!verbose)
        return;
    fprintf(stderr, __FILE__ ": ");
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, "\n");
}

static void die(const char *format, ...) {
    fprintf(stderr, __FILE__ ": ");
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, "\n");
    abort();
}

static const char *get_test_dir(void) {
    static char *d = NULL;
    if (d == NULL) {
        d = getenv("LSHW_TEST_DIR");
        if (d == NULL) return NULL;
        // convert to absolute path, makes things easier below
        d = realpath(d, NULL);
        // strip trailing slash if any
        if (d[strlen(d) - 1] == '/')
            d[strlen(d) - 1] = '\0';
    }
    return d;
}

static void strip_test_dir_inplace(char *pathname) {
    const char *prefix = get_test_dir();
    if (prefix == NULL)
        return;
    size_t prefix_len = strlen(prefix);
    if (strncmp(pathname, prefix, prefix_len) == 0) {
        size_t i;
        for (i = prefix_len; pathname[i] != '\0'; i ++) {
            pathname[i - prefix_len] = pathname[i];
        }
        pathname[i - prefix_len] = '\0';
    }
}

static char *redirected_path(const char *pathname) {
    const char *test_dir = get_test_dir();
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
    if (strncmp(abs, "/proc/", 6) == 0 ||
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
    const char *test_dir = get_test_dir();
    if (test_dir == NULL || test_dir[0] == '\0')
        return NULL;
    char *path = malloc(strlen(test_dir) + 12);
    sprintf(path, "%s/sysconf/%d", test_dir, name);
    return path;
}

static char *redirected_arch_path(void) {
    const char *test_dir = get_test_dir();
    if (test_dir == NULL || test_dir[0] == '\0')
        return NULL;
    char *path = malloc(strlen(test_dir) + 6);
    sprintf(path, "%s/arch", test_dir);
    return path;
}

int chdir(const char *path) {
    static int (*real_func)(const char *);
    if (real_func == NULL) {
        real_func = dlsym(RTLD_NEXT, "chdir");
        if (real_func == NULL)
            die("failed to load real 'chdir': %s", dlerror());
    }
    char *p = redirected_path(path);
    if (p != NULL) {
        info("chdir() redirected to %s", p);
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
        if (real_func == NULL)
            die("failed to load real 'access': %s", dlerror());
    }
    char *p = redirected_path(pathname);
    if (p != NULL) {
        info("access() redirected to %s", p);
        int result = real_func(p, mode);
        free(p);
        return result;
    } else {
        return real_func(pathname, mode);
    }
}

int glob(const char *pattern, int flags,
        int (*errfunc)(const char *epath, int eerrno),
        glob_t *pglob) {
    static int (*real_func)(const char *, int,
            int (*)(const char *, int), glob_t *);
    if (real_func == NULL) {
        real_func = dlsym(RTLD_NEXT, "glob");
        if (real_func == NULL)
            die("failed to load real 'glob': %s", dlerror());
    }
    char *p = redirected_path(pattern);
    if (p != NULL) {
        info("glob() redirected to %s", p);
        int result = real_func(p, flags, errfunc, pglob);
        free(p);
        if (result == 0) {
            // strip off test dir prefix from the returned pathnames
            for (size_t i = 0; i < pglob->gl_pathc; i ++)
                strip_test_dir_inplace(pglob->gl_pathv[i]);
        }
        return result;
    } else {
        return real_func(pattern, flags, errfunc, pglob);
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
        if (real_func == NULL)
            die("failed to load real 'open': %s", dlerror());
    }
    char *p = redirected_path(pathname);
    if (p != NULL) {
        // Reject any write attempts
        if ((flags & O_WRONLY) != 0 || (flags & O_RDWR) != 0 ||
                (flags & O_APPEND) != 0 || (flags & O_CREAT) != 0) {
            info("open() denied write access to redirected path %s", p);
            errno = EROFS;
            return -1;
        }
        info("open() redirected to %s", p);
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
        if (real_func == NULL)
            die("failed to load real 'fopen': %s", dlerror());
    }
    char *p = redirected_path(path);
    if (p != NULL) {
        // Reject any write attempts
        if (strcmp(mode, "r") != 0) {
            info("fopen() denied write access to redirected path %s", p);
            errno = EROFS;
            return NULL;
        }
        info("fopen() redirected to %s", p);
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
        if (real_func == NULL)
            die("failed to load real 'sysconf': %s", dlerror());
    }
    char *p = redirected_sysconf_path(name);
    if (p != NULL) {
        FILE *f = fopen(p, "r");
        if (f == NULL)
            die("failed to fopen %s", p);
        long value;
        if (fscanf(f, "%ld", &value) != 1)
            die("failed to read value from %s", p);
        fclose(f);
        info("sysconf() returning value %ld from %s", value, p);
        free(p);
        return value;
    } else {
        return real_func(name);
    }
}

int uname(struct utsname *name) {
    static int (*real_func)(struct utsname *);
    if (real_func == NULL) {
        real_func = dlsym(RTLD_NEXT, "uname");
        if (real_func == NULL)
            die("failed to load real 'uname': %s", dlerror());
    }
    char *p = redirected_arch_path();
    if (p != NULL) {
        FILE *f = fopen(p, "r");
        if (f == NULL)
            die("failed to fopen %s", p);
        char *arch;
        if (fscanf(f, "%ms", &arch) != 1)
            die("failed to read arch from %s", p);
        fclose(f);
        int result = real_func(name);
        if (result == 0) {
            info("uname() overriding arch to %s from %s", arch, p);
            strncpy(name->machine, arch, sizeof(name->machine));
        }
        free(p);
        free(arch);
        return result;
    } else {
        return real_func(name);
    }
}
