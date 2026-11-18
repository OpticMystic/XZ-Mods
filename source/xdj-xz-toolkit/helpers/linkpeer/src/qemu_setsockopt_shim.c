/* Emulation-only shim for the qemu test variant: replaces musl's setsockopt at static link time.
 * qemu-user 8.2 does not translate IP_MULTICAST_IF / IPV6_MULTICAST_IF and returns ENOPROTOOPT,
 * which makes Link discard every interface. In the test namespaces each peer has one interface
 * and a 224.0.0.0/4 route on it, so the kernel's default multicast interface is already correct.
 * Never ship this variant. */
#define _GNU_SOURCE
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

int setsockopt(int fd, int level, int name, const void *value, socklen_t len)
{
    long r = syscall(SYS_setsockopt, fd, level, name, value, len);
    if (r != 0 && errno == ENOPROTOOPT &&
        ((level == IPPROTO_IP && name == IP_MULTICAST_IF) || (level == IPPROTO_IPV6 && name == IPV6_MULTICAST_IF)))
        return 0;
    return (int)r;
}
