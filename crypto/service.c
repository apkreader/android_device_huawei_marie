// SPDX-License-Identifier: Apache-2.0
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <unistd.h>

static void fail(const char *operation) {
    fprintf(stderr, "marie crypto: %s: %s\n", operation, strerror(errno));
    exit(EXIT_FAILURE);
}

static void bind_stock(const char *source, const char *target) {
    if (mount(source, target, NULL, MS_BIND, NULL) < 0) fail(target);
    if (mount(NULL, target, NULL, MS_REMOUNT | MS_BIND | MS_RDONLY, NULL) < 0)
        fail("read-only stock mount");
}

int main(int argc, char **argv) {
    const char *binary;
    const char *context;
    gid_t groups[] = {1000, 1026};
    size_t group_count;
    if (argc != 2) return EXIT_FAILURE;
    if (strcmp(argv[1], "keymaster") == 0) {
        binary = "/vendor/bin/hw/android.hardware.keymaster@3.0-service";
        context = "u:r:hal_keymaster_default:s0";
        group_count = 2;
    } else if (strcmp(argv[1], "gatekeeper") == 0) {
        binary = "/vendor/bin/hw/android.hardware.gatekeeper@1.0-service";
        context = "u:r:hal_gatekeeper_default:s0";
        group_count = 1;
    } else {
        return EXIT_FAILURE;
    }

    // Keep Huawei's authenticated executables and runtime unchanged. Mounts
    // belong only to this service; recovery retains its platform libraries.
    if (unshare(CLONE_NEWNS) < 0) fail("mount namespace");
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) < 0)
        fail("private mount propagation");
    bind_stock("/crypto/system/system", "/system");
    bind_stock("/crypto/vendor", "/vendor");
    if (clearenv() < 0) fail("clear recovery environment");
    if (setenv("LD_LIBRARY_PATH", "/vendor/lib64:/system/lib64/vndk-28:"
               "/system/lib64/vndk-sp-28:/system/lib64", 1) < 0)
        fail("stock library path");

    int fd = open("/proc/self/attr/exec", O_WRONLY | O_CLOEXEC);
    if (fd < 0) fail("service security context");
    const size_t length = strlen(context);
    ssize_t written;
    do { written = write(fd, context, length); } while (written < 0 && errno == EINTR);
    if (written != (ssize_t)length) fail("set service security context");
    if (close(fd) < 0) fail("close security context");
    if (setgroups(group_count, groups) < 0 || setgid(1000) < 0 || setuid(1000) < 0)
        fail("service identity");
    execl(binary, binary, (char *)NULL);
    fail("stock service");
}
