
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <unistd.h>
#include <fcntl.h>
#include <glob.h>
#include <dlfcn.h>

static void info(const char *format, ...)
    __attribute__ ((format(printf, 1, 2)));
static void die(const char *format, ...)
    __attribute__ ((format(printf, 1, 2)));

/******************* REAL VERSIONS OF WRAPPED FUNCTIONS *******************/

static ssize_t real_readlink(const char *path, char *buf, size_t bufsize) {
    static ssize_t (*real_func)(const char *, char *, size_t);
    if (real_func == NULL) {
        real_func = dlsym(RTLD_NEXT, "readlink");
        if (real_func == NULL)
            die("failed to load real 'readlink': %s", dlerror());
    }
    return real_func(path, buf, bufsize);
}

static char *real_realpath(const char *path, char *resolved_path) {
    static char *(*real_func)(const char *, char *);
    if (real_func == NULL) {
        // glibc realpath is versioned, we want the latest version
        // (there is no way to request the latest version of the symbol except 
        // for hardcoding it, unfortunately)
        real_func = dlvsym(RTLD_NEXT, "realpath", "GLIBC_2.3");
        if (real_func == NULL)
            die("failed to load real 'realpath': %s", dlerror());
    }
    return real_func(path, resolved_path);
}

/*************************** UTILITY FUNCTIONS ****************************/

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
        if (d == NULL || d[0] == '\0')
            die("LSHW_TEST_DIR environment variable not set");
        // convert to absolute path, makes things easier below
        d = real_realpath(d, NULL);
        // strip trailing slash if any
        if (d[strlen(d) - 1] == '/')
            d[strlen(d) - 1] = '\0';
    }
    return d;
}

static void strip_test_dir_inplace(char *pathname) {
    const char *prefix = get_test_dir();
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
    size_t pathlen = strlen(test_dir) + 13;
    char *path = malloc(pathlen);
    if (snprintf(path, pathlen, "%s/sysconf/%d", test_dir, name) > pathlen)
        die("redirected_sysconf_path too long");
    return path;
}

static char *redirected_arch_path(void) {
    const char *test_dir = get_test_dir();
    size_t pathlen = strlen(test_dir) + 6;
    char *path = malloc(pathlen);
    if (snprintf(path, pathlen, "%s/arch", test_dir) > pathlen)
        die("redirected_arch_path too long");
    return path;
}

// For now, assumes block devices!
static char *redirected_sysfs_attr_path(const char *dev, const char *attr) {
    const char *test_dir = get_test_dir();
    size_t pathlen = strlen(test_dir) + strlen(dev) + strlen(attr) + 13;
    char *path = malloc(pathlen);
    if (snprintf(path, pathlen, "%s/sys/block/%s/%s", test_dir,
            basename(dev), attr) > pathlen)
        die("redirected_sysfs_attr_path too long");
    return path;
}

static int64_t load_int(const char *path) {
    FILE *f = fopen(path, "r");
    if (f == NULL)
        die("failed to open %s", path);
    int64_t result;
    if (fscanf(f, "%" PRIi64, &result) != 1)
        die("failed to read integer value from %s", path);
    fclose(f);
    return result;
}

static uint64_t load_uint(const char *path) {
    FILE *f = fopen(path, "r");
    if (f == NULL)
        die("failed to open %s", path);
    uint64_t result;
    if (fscanf(f, "%" PRIu64, &result) != 1)
        die("failed to read integer value from %s", path);
    fclose(f);
    return result;
}

static uint64_t load_sysfs_attr_uint(const char *dev, const char *attr) {
    char *p = redirected_sysfs_attr_path(dev, attr);
    uint64_t result = load_uint(p);
    free(p);
    return result;
}

static char *path_for_fd(int fd) {
    char proc_pathname[22];
    if (snprintf(proc_pathname, 22, "/proc/self/fd/%d", fd) > 22)
        die("path_for_fd(%d) too long", fd);
    char *fd_pathname = (char *)malloc(2048);
    ssize_t fd_pathname_len = real_readlink(proc_pathname, fd_pathname, 2048);
    if (fd_pathname_len < 0 || fd_pathname_len >= 2048)
        die("failed to read symlink %s", proc_pathname);
    fd_pathname[fd_pathname_len] = '\0';
    return fd_pathname;
}

/************************** WRAPPER FUNCTIONS ****************************/

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

// glibc doesn't export "stat", it's an inline which indirects to __xstat with 
// a version number argument, sigh...
int __xstat(int ver, const char *pathname, struct stat *buf) {
    static int (*real_func)(int, const char *, struct stat *);
    if (real_func == NULL) {
        real_func = dlsym(RTLD_NEXT, "__xstat");
        if (real_func == NULL)
            die("failed to load real '__xstat': %s", dlerror());
    }
    char *p = redirected_path(pathname);
    if (p != NULL) {
        info("__xstat() redirected to %s", p);
        int result = real_func(ver, p, buf);
        free(p);
        return result;
    } else {
        return real_func(ver, pathname, buf);
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

ssize_t readlink(const char *path, char *buf, size_t bufsize) {
    char *p = redirected_path(path);
    if (p != NULL) {
        info("readlink() redirected to %s", p);
        ssize_t result = real_readlink(p, buf, bufsize);
        free(p);
        return result;
    } else {
        return real_readlink(path, buf, bufsize);
    }
}

char *realpath(const char *path, char *resolved_path) {
    char *p = redirected_path(path);
    if (p != NULL) {
        info("realpath() redirected to %s", p);
        char *result = real_realpath(p, NULL);
        free(p);
        if (result != NULL) {
            // strip off test dir prefix from the resolved path
            strip_test_dir_inplace(result);
            if (resolved_path != NULL) {
                memcpy(resolved_path, result, PATH_MAX);
                free(result);
                result = resolved_path;
            }
        }
        return result;
    } else {
        return real_realpath(path, resolved_path);
    }
}

// glibc has an inline which redirects to this implementation of realpath with 
// buffer size checking
char *__realpath_chk(const char *path, char *resolved_path, size_t resolved_len) {
    return realpath(path, resolved_path);
}

static int _open(const char *pathname, int flags, mode_t mode) {
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

int open(const char *pathname, int flags, ... /* mode_t mode */) {
    va_list args;
    va_start(args, flags);
    mode_t mode = va_arg(args, mode_t);
    va_end(args);
    return _open(pathname, flags, mode);
}

// When a program is compiled with "large file support" it might reference open64 instead.
// XXX I think this is wrong on 32-bit platforms?
int open64(const char *pathname, int flags, ... /* mode_t mode */) {
    va_list args;
    va_start(args, flags);
    mode_t mode = va_arg(args, mode_t);
    va_end(args);
    return _open(pathname, flags, mode);
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
        if (strncmp(mode, "r", 1) != 0) {
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

FILE *fopen64(const char *path, const char *mode) {
    return fopen(path, mode);
}

int ioctl(int d, unsigned long request, ...) {
    va_list args;
    va_start(args, request);
    void *argp = va_arg(args, void *);
    va_end(args);
    static long (*real_func)(int, int, void *);
    if (real_func == NULL) {
        real_func = dlsym(RTLD_NEXT, "ioctl");
        if (real_func == NULL)
            die("failed to load real 'ioctl': %s", dlerror());
    }
    if (d <= 0)
        goto passthrough;
    char *fd_pathname = path_for_fd(d);
    // is the fd for a redirected path?
    const char *test_dir = get_test_dir();
    if (strncmp(fd_pathname, test_dir, strlen(test_dir)) != 0) {
        free(fd_pathname);
        goto passthrough;
    }
    switch (request) {
        case BLKGETSIZE64: {
            uint64_t sectors = load_sysfs_attr_uint(fd_pathname, "size");
            // sysfs size is always in 512-byte "UNIX sectors" (I think...)
            uint64_t size = sectors * 512;
            info("ioctl() returning BLKGETSIZE64 value %" PRIu64 " for %s",
                    size, fd_pathname);
            *((uint64_t *)argp) = size;
        } break;
        case BLKPBSZGET: {
            unsigned int phys_block_size = (unsigned int) load_sysfs_attr_uint(
                    fd_pathname, "queue/physical_block_size");
            info("ioctl() returning BLKPBSZGET value %u for %s",
                    phys_block_size, fd_pathname);
            *((unsigned int *)argp) = phys_block_size;
        } break;
        case BLKSSZGET: {
            int logical_block_size = (int) load_sysfs_attr_uint(fd_pathname,
                    "queue/logical_block_size");
            info("ioctl() returning BLKSSZGET value %d for %s",
                    logical_block_size, fd_pathname);
            *((int *)argp) = logical_block_size;
        } break;
        default:
            die("unimplemented ioctl() %lu", request);
    }
    free(fd_pathname);
    return 0;
passthrough:
    return real_func(d, request, argp);
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
        int64_t value = load_int(p);
        info("sysconf() returning value %ld from %s", value, p);
        free(p);
        return (long) value;
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
