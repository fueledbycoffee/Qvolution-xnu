#!/usr/bin/env python3
"""Execute the production S2C receive loop with real native Unix FD transfers.

Only the surrounding Mach/Linux build interfaces are adapted to native Linux;
the receive function and public Darling S2C protocol types are used unchanged.
Pass the Darling superproject root, and optionally a directory for build output.
"""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('superproject', type=Path)
parser.add_argument('--out', type=Path)
args = parser.parse_args()
xnu = Path(__file__).resolve().parents[2]
header = xnu / 'darling/src/libsystem_kernel/emulation/include/linux_premigration/resources/dserver-rpc-defs.h'
source = header.read_text()
start = source.index('static long int dserver_rpc_hooks_receive_message(')
end = source.index('\n#define dserver_rpc_hooks_get_bad_message_status()', start)
function = source[start:end]
out = args.out or Path(tempfile.mkdtemp(prefix='s2c-receive-'))
out.mkdir(parents=True, exist_ok=True)
adapter = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <darlingserver/rpc-supplement.h>

#define dserver_rpc_hooks_msghdr_t struct msghdr
#define dserver_rpc_hooks_cmsghdr_t struct cmsghdr
#define DSERVER_RPC_HOOKS_CMSG_FIRSTHDR CMSG_FIRSTHDR
#define DSERVER_RPC_HOOKS_CMSG_LEN CMSG_LEN
#define DSERVER_RPC_HOOKS_CMSG_DATA CMSG_DATA
#define DSERVER_RPC_HOOKS_SOL_SOCKET SOL_SOCKET
#define DSERVER_RPC_HOOKS_SCM_RIGHTS SCM_RIGHTS
#define dserver_rpc_hooks_memcpy memcpy
#define dserver_rpc_hooks_get_pid getpid
#define dserver_rpc_hooks_get_tid() syscall(SYS_gettid)
#define dserver_rpc_hooks_get_architecture() 2
#define dserver_rpc_hooks_get_server_address() NULL
#define dserver_rpc_hooks_get_server_address_length() 0
#define __simple_printf(...) fprintf(stderr, __VA_ARGS__)
#define __simple_abort() abort()
#define close_internal close
static long linux_result(long result) {
    return result == -1 ? -errno : result;
}
/* The sandbox blocks sendto even on socketpairs. Adapt only that operation
 * to a real sendmsg on the connected socket; recvmsg/mmap/munmap stay real. */
static long native_syscall(long number, ...) {
    va_list args;
    va_start(args, number);
    long result;
    if (number == __NR_sendto) {
        int socket = va_arg(args, int);
        const void *data = va_arg(args, const void *);
        size_t size = va_arg(args, size_t);
        int flags = va_arg(args, int);
        struct iovec iov = { .iov_base = (void *)data, .iov_len = size };
        struct msghdr message = { .msg_iov = &iov, .msg_iovlen = 1 };
        result = sendmsg(socket, &message, flags);
    } else if (number == __NR_recvmsg) {
        int socket = va_arg(args, int);
        struct msghdr *message = va_arg(args, struct msghdr *);
        int flags = va_arg(args, int);
        result = recvmsg(socket, message, flags);
    } else if (number == __NR_mmap) {
        uint64_t address = va_arg(args, uint64_t), length = va_arg(args, uint64_t);
        int protection = va_arg(args, int), flags = va_arg(args, int), fd = va_arg(args, int);
        int64_t offset = va_arg(args, int64_t);
        result = syscall(number, address, length, protection, flags, fd, offset);
    } else {
        uint64_t address = va_arg(args, uint64_t), length = va_arg(args, uint64_t);
        if (number == __NR_munmap) result = syscall(number, address, length);
        else {
            assert(number == __NR_mprotect || number == __NR_msync);
            int flags = va_arg(args, int);
            result = syscall(number, address, length, flags);
        }
    }
    va_end(args);
    return linux_result(result);
}
#define LINUX_SYSCALL native_syscall
#define LINUX_SYSCALL2 LINUX_SYSCALL
#define LINUX_SYSCALL3 LINUX_SYSCALL
'''
harness = r'''
static void send_packet(int socket, const void *data, size_t size, int fd) {
    char control[CMSG_SPACE(sizeof(int))];
    struct iovec iov = { .iov_base = (void *)data, .iov_len = size };
    struct msghdr message = { .msg_iov = &iov, .msg_iovlen = 1 };
    if (fd >= 0) {
        message.msg_control = control;
        message.msg_controllen = sizeof(control);
        struct cmsghdr *cmsg = CMSG_FIRSTHDR(&message);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(sizeof(fd));
        memcpy(CMSG_DATA(cmsg), &fd, sizeof(fd));
    }
    assert(sendmsg(socket, &message, 0) == (ssize_t)size);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    int sockets[2];
    assert(socketpair(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0, sockets) == 0);
    int fd = memfd_create("s2c-capacity", MFD_CLOEXEC);
    assert(fd >= 0 && ftruncate(fd, 4096) == 0);
    assert(pwrite(fd, "shared", 7, 0) == 7);
    void *old = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(old != MAP_FAILED);
    dserver_s2c_call_munmap_t unmap = {
        .header = { 0x52cca11, dserver_s2c_msgnum_munmap },
        .address = (uintptr_t)old, .length = 4096,
    };
    dserver_s2c_call_mmap_t map = {
        .header = { 0x52cca11, dserver_s2c_msgnum_mmap },
        .length = 4096, .protection = PROT_READ | PROT_WRITE,
        .flags = MAP_SHARED, .fd = 0,
    };
    send_packet(sockets[0], &unmap, sizeof(unmap), -1);
    int maps = strcmp(argv[1], "ordinary-fd") ? 2 : 0;
    for (int i = 0; i < maps; ++i)
        send_packet(sockets[0], &map, sizeof(map), fd);
    const uint64_t marker = 0x12345678;
    send_packet(sockets[0], &marker, sizeof(marker), maps ? -1 : fd);
    union { dserver_s2c_call_t s2c; uint64_t marker; } buffer;
    char control[CMSG_SPACE(sizeof(int))];
    struct iovec iov = { .iov_base = &buffer, .iov_len = sizeof(buffer) };
    struct msghdr message = { .msg_iov = &iov, .msg_iovlen = 1,
        .msg_control = control, .msg_controllen = sizeof(control) };
    long received_size = dserver_rpc_hooks_receive_message(sockets[1], &message);
    if (received_size != sizeof(marker))
        fprintf(stderr, "receive hook returned %ld, errno=%d\n", received_size, errno);
    assert(received_size == sizeof(marker));
    assert(buffer.marker == marker && !(message.msg_flags & MSG_CTRUNC));
    if (maps) {
        assert(message.msg_controllen == 0);
    } else {
        struct cmsghdr *cmsg = CMSG_FIRSTHDR(&message);
        assert(cmsg && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS);
        int received;
        memcpy(&received, CMSG_DATA(cmsg), sizeof(received));
        char bytes[7];
        assert(pread(received, bytes, sizeof(bytes), 0) == sizeof(bytes));
        assert(!memcmp(bytes, "shared", sizeof(bytes)));
        assert(close(received) == 0);
    }
    dserver_s2c_reply_munmap_t unmap_reply;
    assert(recv(sockets[0], &unmap_reply, sizeof(unmap_reply), 0) == sizeof(unmap_reply));
    assert(unmap_reply.return_value == 0 && unmap_reply.errno_result == 0);
    for (int i = 0; i < maps; ++i) {
        dserver_s2c_reply_mmap_t reply;
        assert(recv(sockets[0], &reply, sizeof(reply), 0) == sizeof(reply));
        assert(reply.errno_result == 0);
        assert(!memcmp((void *)(uintptr_t)reply.address, "shared", 7));
        assert(munmap((void *)(uintptr_t)reply.address, 4096) == 0);
    }
    close(fd);
    close(sockets[0]);
    close(sockets[1]);
    printf("%s: real recvmsg/SCM_RIGHTS, mmap, munmap and final reply passed\n", argv[1]);
}
'''
generated = out / 's2c-receive-capacity.c'
generated.write_text(adapter + '\n' + function + '\n' + harness)
binary = out / 's2c-receive-capacity'
command = ['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-int-conversion',
           '-Wno-sign-compare', '-I' + str(args.superproject.resolve() / 'src/external/darlingserver/include'),
           str(generated), '-o', str(binary)]
subprocess.run(command, check=True)
rows = []
for case in ('s2c-fd', 'ordinary-fd'):
    result = subprocess.run([str(binary), case], capture_output=True, text=True)
    rows.append(dict(case=case, exit=result.returncode, stdout=result.stdout, stderr=result.stderr))
report = dict(header=str(header), header_sha256=hashlib.sha256(header.read_bytes()).hexdigest(),
              compile=command, cases=rows)
(out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
raise SystemExit(0 if all(row['exit'] == 0 for row in rows) else 1)
