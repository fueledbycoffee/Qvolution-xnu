#!/usr/bin/env python3
"""Execute the production i386 wrapper expansions in a native x86_64 adapter.

The selected standard wrapper instructions also execute in long mode.
INT and bespoke wrappers are tested in i386-wrapper-paths.py. Only ULOCK's
stack argument lookup here is mapped to SysV's EDI. Trap/handler and errno storage
are fixtures; no ia32 syscall or Darwin startup is claimed by this test.
"""
import argparse
from pathlib import Path
import resource
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--out', type=Path, required=True)
p.add_argument('--ref', help='test headers from this git revision instead')
a = p.parse_args()
x = Path(__file__).resolve().parents[2]
a.out.mkdir(parents=True, exist_ok=True)
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
shim = a.out / 'include'
for name, text in {
    'sys/syscall.h': '#define SYS_probe 153\n#define I386_SYSCALL_ARG_BYTES_SHIFT 16\n',
    'architecture/i386/asm_help.h': '#define LEAF(name, unused) .text; .globl name; name:\n#define BRANCH_EXTERN(name) jmp name\n',
    'mach/i386/syscall_sw.h': '#define UNIX_SYSCALL_TRAP call __darling_bsd_syscall\n',
}.items():
    f = shim / name
    f.parent.mkdir(parents=True, exist_ok=True)
    f.write_text(text)
fixture = r'''
#include "tested-SYS.h"
UNIX_SYSCALL(probe, 4)
ret
__SYSCALL2(noname, probe, 4, cerror)
#define __SYSCALL_32BIT_ARG_BYTES 20
__SYSCALL2(shortargs, probe, 4, cerror)
#undef __SYSCALL_32BIT_ARG_BYTES
#define __SYSCALL_32BIT_ARG_BYTES 24
__SYSCALL2(longargs, probe, 4, cerror)
ULOCK_SYSCALL(rawprobe, probe, 4, cerror)
'''
handlers = r'''
.text
__darling_bsd_syscall:
movl raw_result(%rip), %eax
cmpl $0, carry_flag(%rip)
je 1f
stc
ret
1: clc
ret
tramp_cerror:
tramp_cerror_nocancel:
movl %eax, observed_errno(%rip)
movl $-1, %eax
ret
.section .note.GNU-stack,"",@progbits
'''
c = a.out / 'check.c'
c.write_text(r'''
#include <stdio.h>
int raw_result, carry_flag, observed_errno;
extern int _probe(void), noname(void), shortargs(void), longargs(void);
extern int rawprobe(int);
int main(void) {
    int (*calls[])(void) = {_probe, noname, shortargs, longargs};
    int results[] = {-2, 0, 7, 4096, -1, -9, -4095, -4096, (int)0x80000000u};
    for (carry_flag = 0; carry_flag < 2; ++carry_flag)
        for (unsigned i = 0; i < sizeof(results)/sizeof(*results); ++i) {
            raw_result = results[i];
            for (unsigned j = 0; j < sizeof(calls)/sizeof(*calls); ++j) {
                observed_errno = 0;
                int actual = calls[j]();
                int error = (unsigned)raw_result >= (unsigned)-4095;
                if (actual != (error ? -1 : raw_result) || observed_errno != (error ? -raw_result : 0)) {
                    fprintf(stderr, "wrapper %u result %d carry %d: returned %d errno %d\n", j, raw_result, carry_flag, actual, observed_errno);
                    return 1;
                }
            }
            observed_errno = 0;
            if (rawprobe(0x01000000) != raw_result || observed_errno) return 2;
        }
    puts("four standard i386 wrapper forms: errno range and both carry states pass; raw ulock preserved");
    return 0;
}
''')
failed = False
for tag, name in (('custom', 'libsyscall/custom/SYS.h'), ('generated', 'gen/bsdsyscalls/SYS.h')):
    header = subprocess.check_output(['git', '-C', str(x), 'show', a.ref + ':' + name], text=True) if a.ref else (x / name).read_text()
    (a.out / 'tested-SYS.h').write_text(header)
    src = a.out / (tag + '.S')
    src.write_text(fixture)
    expanded = subprocess.check_output(['cc', '-E', '-P', '-nostdinc', '-I' + str(shim), '-D__i386__', '-U__x86_64__', '-DDARLING', str(src)], text=True)
    expanded = expanded.replace('4(%esp)', '%edi')
    src.write_text(expanded + handlers)
    binary = a.out / tag
    subprocess.run(['cc', '-no-pie', '-Wall', '-Wextra', '-Werror', str(c), str(src), '-o', str(binary)], check=True)
    result = subprocess.run([str(binary)])
    print(tag, 'exit', result.returncode, flush=True)
    failed |= bool(result.returncode)
raise SystemExit(failed)
