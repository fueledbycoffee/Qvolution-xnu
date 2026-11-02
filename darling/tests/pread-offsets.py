#!/usr/bin/env python3
"""Compile production pread and Linux syscall entry, then exercise sparse I/O.

Mach-O symbol spelling/directives are adapted for ELF. Cancellation and errno
mapping are fixture functions (EBADF has the same value); the offset packing
and register loading are the unmodified production implementation.
"""
import argparse
import json
from pathlib import Path
import resource
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--out', type=Path, required=True)
p.add_argument('--all', action='store_true', help='require ELF32 execution too')
a = p.parse_args()
a.out = a.out.resolve()
a.out.mkdir(parents=True, exist_ok=True)
x = Path(__file__).resolve().parents[1] / 'src/libsystem_kernel/emulation'
include = a.out / 'include/darling'
include.mkdir(parents=True, exist_ok=True)
if not (include / 'emulation').exists():
    (include / 'emulation').symlink_to(x / 'include')
entry = (x / 'src/linux_premigration/linux-syscall.S').read_text()
entry = '\n'.join(s for s in entry.splitlines() if not s.startswith('.private_extern'))
entry = entry.replace('_linux_syscall', 'linux_syscall')
asm = a.out / 'linux-syscall.S'
asm.write_text(entry + '\n.section .note.GNU-stack,"",@progbits\n')
fixture = a.out / 'pread-offsets.c'
fixture.write_text(r'''
#define _FILE_OFFSET_BITS 64
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
long sys_pread(int, void *, int, long long);
long sys_pread_nocancel(int, void *, int, long long);
long sys_pthread_canceled(int action) { (void)action; return 1; }
int errno_linux_to_bsd(int error) { return error; }
int main(int argc, char **argv) {
    _Static_assert(sizeof(off_t) == 8, "fixture requires 64-bit offsets");
    assert(argc == 2);
    int fd = open(argv[1], O_CREAT | O_EXCL | O_RDWR, 0600);
    assert(fd >= 0);
    long long high = 0x100000123LL;
    assert(pwrite(fd, "zero", 4, 0) == 4);
    assert(pwrite(fd, "high", 4, high) == 4);
    char b[5] = {0};
    assert(sys_pread_nocancel(fd, b, 4, 0) == 4 && !memcmp(b, "zero", 4));
    assert(sys_pread(fd, b, 4, high) == 4 && !memcmp(b, "high", 4));
    assert(sys_pread_nocancel(fd, b, 4, high + 4) == 0);
    assert(lseek(fd, 0, SEEK_CUR) == 0);
    assert(sys_pread_nocancel(-1, b, 4, 0) == -EBADF);
    assert(sys_pread_nocancel(fd, b, 4, -1) == -EINVAL);
    assert(close(fd) == 0);
    puts("production pread: zero, >4GiB, EOF, file position, EBADF and EINVAL pass");
}
''')
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
report = {}
for bits in (64, 32):
    binary = a.out / ('pread-' + str(bits))
    subprocess.run(['cc', '-m' + str(bits), '-Wall', '-Wextra', '-Werror', '-I' + str(include.parent), str(fixture), str(x / 'src/xnu_syscall/bsd/impl/unistd/pread.c'), str(asm), '-o', str(binary)], check=True)
    result = subprocess.run([str(binary), str(a.out / ('data-' + str(bits)))], capture_output=True, text=True)
    report[str(bits)] = dict(returncode=result.returncode, stdout=result.stdout, stderr=result.stderr)
print(json.dumps(report, indent=2))
raise SystemExit(bool(report['64']['returncode'] or (a.all and report['32']['returncode'])))
