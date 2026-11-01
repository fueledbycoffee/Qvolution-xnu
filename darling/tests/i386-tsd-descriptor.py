#!/usr/bin/env python3
"""Run the production i386 descriptor setter with modeled Linux GDT syscalls.

Only segment-register reads/writes are adapted so the i386 control flow can
run in a native x86_64 process. This does not execute ia32 kernel syscalls.
"""
import argparse
from pathlib import Path
import resource
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--source', type=Path)
p.add_argument('--out', type=Path, required=True)
a = p.parse_args()
source = a.source or Path(__file__).resolve().parents[1] / 'src/libsystem_kernel/emulation/src/xnu_syscall/machdep/impl/tls.c'
s = source.read_text()
s = s[s.index('void sys_thread_set_tsd_base('):]
s = s[s.index('#elif defined(__i386__)') + len('#elif defined(__i386__)'):s.index('#elif defined(__aarch64__)')]
s = s.replace('__asm__ volatile("movw %%fs, %0" : "=r"(selector));', 'selector = current_fs;')
s = s.replace('__asm__ ("movl %0, %%fs" :: "r" (desc.entry_number*8 + 3));', 'current_fs = desc.entry_number*8 + 3;')
assert '__asm__' not in s
adapter = r'''
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <asm/unistd.h>
static unsigned short current_fs;
/* Three slots; indices are arbitrary in this syscall model. */
static unsigned long slots[4];
static int calls;
struct model_desc {
    unsigned int entry_number;
    unsigned long base_addr;
    unsigned int limit;
    unsigned int seg_32bit:1, contents:2, read_exec_only:1, limit_in_pages:1,
                 seg_not_present:1, useable:1;
};
static long model_syscall(long nr, void *ptr) {
    struct model_desc *d = ptr;
    ++calls;
    if (nr == __NR_get_thread_area) {
        if (d->entry_number < 1 || d->entry_number > 3) return -22;
        d->base_addr = slots[d->entry_number];
        return 0;
    }
    assert(nr == __NR_set_thread_area);
    if (d->entry_number == (unsigned)-1) {
        for (unsigned i = 1; i <= 3; ++i) {
            if (!slots[i]) { d->entry_number = i; break; }
        }
    }
    if (d->entry_number < 1 || d->entry_number > 3) return -3;
    slots[d->entry_number] = d->base_addr;
    return 0;
}
#define LINUX_SYSCALL(nr, p) model_syscall(nr, p)
#define __simple_abort abort
'''
functions = ''.join('static void ' + name + '(void *ptr) {\n' + s + '\n}\n' for name in ('dyld_set_tsd', 'dynamic_set_tsd'))
harness = r'''
int main(void) {
    for (int initial = 0; initial < 2; ++initial) {
        memset(slots, 0, sizeof(slots));
        slots[1] = 0x111; /* Native Linux GS descriptor. */
        current_fs = 0;
        if (initial) {
            slots[2] = 0x222; /* Loader's temporary FS descriptor. */
            current_fs = 2 * 8 + 3;
        }
        for (int i = 0; i < 128; ++i) {
            dyld_set_tsd((void*)0x333);
            dynamic_set_tsd((void*)0x444);
            assert(current_fs == 2 * 8 + 3);
            assert(slots[1] == 0x111 && slots[2] == 0x444 && slots[3] == 0);
        }
    }
    assert(calls > 0);
    puts("i386 descriptor reuse: loader/dyld/dynamic handoffs preserve native slot");
}
'''
a.out.mkdir(parents=True, exist_ok=True)
c = a.out / 'i386-tsd-descriptor.c'
c.write_text(adapter + functions + harness)
binary = a.out / 'i386-tsd-descriptor'
subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', str(c), '-o', str(binary)], check=True)
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
raise SystemExit(subprocess.run([str(binary)]).returncode)
