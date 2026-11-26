// The nested compositor's fake keyboard does not exist in /dev/input. Keep the live user's held
// modifiers out of isolated tests; the separately guarded live harness covers real modifier state.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <linux/input.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

static void save_barrier(void) {
    // A deterministic disk barrier verifies that the wheel keeps animating during persistence.
    const char *gate = getenv("QRO_STACK_SAVE_GATE");
    const char *display = getenv("WAYLAND_DISPLAY");
    if (gate && display && strncmp(display, "qro-stack-test-", 15) == 0)
        for (int i = 0; i < 500 && access(gate, F_OK) == 0; ++i) usleep(10000);
}
int fsync(int fd) {
    save_barrier();
    int (*real_fsync)(int) = dlsym(RTLD_NEXT, "fsync");
    return real_fsync(fd);
}
int fdatasync(int fd) {
    save_barrier();
    int (*real_fdatasync)(int) = dlsym(RTLD_NEXT, "fdatasync");
    return real_fdatasync(fd);
}

int ioctl(int fd, unsigned long request, ...) {
    va_list arguments;
    va_start(arguments, request);
    void *argument = va_arg(arguments, void *);
    va_end(arguments);
    const char *display = getenv("WAYLAND_DISPLAY");
    if (_IOC_TYPE(request) == 'E' && _IOC_NR(request) == _IOC_NR(EVIOCGKEY(1)) &&
        display && strncmp(display, "qro-stack-test-", 15) == 0) {
        memset(argument, 0, _IOC_SIZE(request));
        return 0;
    }
    int (*real_ioctl)(int, unsigned long, ...) = dlsym(RTLD_NEXT, "ioctl");
    return real_ioctl(fd, request, argument);
}

ssize_t write(int fd, const void *buffer, size_t count) {
    // The isolated daemon must never write keys into the live desktop's /dev/input devices (char major 13).
    const char *display = getenv("WAYLAND_DISPLAY");
    struct stat info;
    if (display && strncmp(display, "qro-stack-test-", 15) == 0 && fstat(fd, &info) == 0 && S_ISCHR(info.st_mode)
        && major(info.st_rdev) == 13) {
        errno = EPERM;
        return -1;
    }
    ssize_t (*real_write)(int, const void *, size_t) = dlsym(RTLD_NEXT, "write");
    return real_write(fd, buffer, count);
}
