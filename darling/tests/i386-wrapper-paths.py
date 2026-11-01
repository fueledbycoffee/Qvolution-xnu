#!/usr/bin/env python3
"""Run production i386 wrapper instructions in a native x86_64 adapter.

Only stack/pointer widths and Mach-O assembler metadata are adapted. Dispatch
and cerror are fixtures (including caller-clobbered ECX/EDX). No IA32, Linux
syscall, Darwin loader or guest execution is claimed. Each wrapper runs in a
separate process so a broken vfork return cannot hide other failures.
"""
import argparse
import json
from pathlib import Path
import re
import resource
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--out', type=Path, required=True)
p.add_argument('--ref')
p.add_argument('--emit-guest', action='store_true', help='emit real i386 Mach-O fixture sources; do not run')
a = p.parse_args()
x = Path(__file__).resolve().parents[2]
out = a.out.resolve()
out.mkdir(parents=True, exist_ok=True)
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
names = ['lseek', 'pipe', 'gettimeofday', 'sigaltstack', 'sigreturn',
         'thread_selfid', 'thread_selfusage', 'kdebug_trace_string',
         'fork', 'vfork', 'syscall']
numbers = {name: i + 100 for i, name in enumerate(names)}
shim = out / 'include'
for name, contents in {
    'sys/syscall.h': ''.join(f'#define SYS_{n} {i}\n' for n, i in numbers.items()),
    'architecture/i386/asm_help.h': '''
#define LEAF(name, unused) .text; .globl name; name:
#define LABEL(name) .text; .globl name; name:
#define END(name) ret
#define BRANCH_EXTERN(name) jmp name
#define CALL_EXTERN(name) call name
#define REG_TO_EXTERN(reg, name) movl reg, name(%rip)
#define ALIGN .p2align 2
#define PICIFY(name) movq name@GOTPCREL(%rip), %r11
''',
    'mach/i386/syscall_sw.h': '''
#define UNIX_SYSCALL_TRAP call __darling_bsd_syscall
#define MACHDEP_SYSCALL_TRAP call __darling_bsd_syscall
''',
}.items():
    path = shim / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(contents)

def read(name):
    if a.ref:
        return subprocess.check_output(['git', '-C', str(x), 'show', a.ref + ':' + name], text=True)
    return (x / name).read_text()

fixture = out / 'fixture.S'
fixture.write_text(r'''
.text
.globl __darling_bsd_syscall
__darling_bsd_syscall:
movl raw_low(%rip), %eax
movl raw_high(%rip), %edx
xorl %ecx, %ecx
cmpl $0, carry_flag(%rip)
je 1f
stc
ret
1: clc
ret
.globl tramp_cerror, tramp_cerror_nocancel
tramp_cerror:
tramp_cerror_nocancel:
movl %eax, observed_errno(%rip)
movl $-1, %eax
movl $-1, %edx
ret
.globl invoke, invoke_syscall
invoke_syscall:
pushq %rbx
movq %rsi, %rbx
pushq $99
call *%rdi
addq $8, %rsp
jmp 1f
invoke:
pushq %rbx
movq %rsi, %rbx
call *%rdi
1:
shlq $32, %rdx
movl %eax, %eax
orq %rdx, %rax
popq %rbx
ret
.section .note.GNU-stack,"",@progbits
''')
c = out / 'check.c'
c.write_text(r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int raw_low, raw_high, carry_flag, observed_errno, __current_pid;
extern void ___lseek(void), ___pipe(void), ___gettimeofday_with_mach(void);
extern void ___sigaltstack(void), ___sigreturn(void), ___thread_selfid(void);
extern void ___thread_selfusage(void), ___kdebug_trace_string(void);
extern void ___fork(void), ___vfork(void), ___syscall(void);
extern void _i386_get_ldt(void), _i386_set_ldt(void), _intprobe(void);
extern uint64_t invoke(void (*)(void), void *), invoke_syscall(void (*)(void), void *);
struct wrapper {const char *name; void (*fn)(void);} wrappers[] = {
 {"lseek", ___lseek}, {"pipe", ___pipe}, {"gettimeofday", ___gettimeofday_with_mach},
 {"sigaltstack", ___sigaltstack}, {"sigreturn", ___sigreturn},
 {"thread_selfid", ___thread_selfid}, {"thread_selfusage", ___thread_selfusage},
 {"kdebug_trace_string", ___kdebug_trace_string}, {"fork", ___fork},
 {"vfork", ___vfork}, {"syscall", ___syscall},
 {"get_ldt", _i386_get_ldt}, {"set_ldt", _i386_set_ldt}, {"int", _intprobe}
};
int main(int argc, char **argv) {
 if (argc != 2) return 2;
 unsigned w = (unsigned)atoi(argv[1]);
 if (w >= sizeof(wrappers)/sizeof(*wrappers)) return 2;
 const char *name = wrappers[w].name;
 int wide = !strcmp(name, "lseek"), pipe = !strcmp(name, "pipe");
 int time = !strcmp(name, "gettimeofday");
 int fork = !strcmp(name, "fork"), vfork = !strcmp(name, "vfork");
 int values[] = {-9, -1, -4095, -4096, 0, 7, 0x7fffffff, (int)0x80000000u};
 int highs[] = {-1, 0, 1, 0x1234};
 int failed = 0;
 for (carry_flag = 0; carry_flag < 2; ++carry_flag)
 for (unsigned i = 0; i < sizeof(values)/sizeof(*values); ++i)
 for (unsigned h = 0; h < sizeof(highs)/sizeof(*highs); ++h)
 for (unsigned cache = 0; cache < (vfork ? 2u : 1u); ++cache) {
   raw_low = values[i]; raw_high = highs[h]; observed_errno = 0;
   int initial_pid = cache ? -3 : 123;
   __current_pid = initial_pid;
   int output[] = {0x3344, 0x5566};
   int error = (unsigned)raw_low >= (unsigned)-4095 && (!wide || raw_high == -1);
   uint64_t result = (!strcmp(name, "syscall") ? invoke_syscall : invoke)(wrappers[w].fn, output);
   uint32_t low = (uint32_t)result, high = result >> 32;
   uint32_t expected = error ? -1u : (pipe || time ? 0u : (unsigned)raw_low);
   int expected_pid = initial_pid;
   if (fork && !error && !raw_low) expected_pid = 0;
   if (vfork) {
     expected_pid = initial_pid >= 0 ? -1 : initial_pid - 1;
     if (error || raw_low) ++expected_pid;
   }
   int memory_bad = (error || (!pipe && !time) || (time && !raw_low))
       ? output[0] != 0x3344 || output[1] != 0x5566
       : output[0] != raw_low || output[1] != raw_high;
   if (low != expected || observed_errno != (error ? -raw_low : 0) ||
       (wide && high != (error ? -1u : (unsigned)raw_high)) || memory_bad ||
       __current_pid != expected_pid) {
     if (!failed) fprintf(stderr, "%s low=%d high=%d carry=%d: result=%x:%x errno=%d pid=%d expected-low=%x errno=%d pid=%d\n",
       name, raw_low, raw_high, carry_flag, high, low, observed_errno, __current_pid,
       expected, error ? -raw_low : 0, expected_pid);
     failed = 1;
   }
 }
 if (!failed) printf("%s: errno, boundaries, carry, output and PID paths PASS\n", name);
 return failed;
}
''')
if a.emit_guest:
    # Keep fixtures private: executable symbols must never interpose on libSystem.
    aliases = {'___' + n: 'probe_' + n for n in names}
    aliases.update({'___gettimeofday_with_mach': 'probe_gettimeofday_with_mach',
      '_i386_get_ldt': 'probe_get_ldt', '_i386_set_ldt': 'probe_set_ldt',
      '_intprobe': 'probe_int', '__current_pid': 'probe_pid',
      'tramp_cerror': 'probe_tramp_cerror', 'tramp_cerror_nocancel': 'probe_tramp_cerror_nocancel',
      '__sysenter_trap': 'probe_sysenter', '__thread_set_tsd_base': 'probe_tsd',
      '__darling_bsd_syscall': 'probe_dispatch', '__darling_machdep_syscall': 'probe_dispatch'})
    for n in names:
        aliases['_' + n] = 'probe_public_' + n
    (out / 'rename.json').write_text(json.dumps(aliases, indent=2) + '\n')
    pragmas = ''.join('#pragma redefine_extname ' + n + ' ' + aliases.get(n, n) + '\n'
        for n in ['raw_low', 'raw_high', 'carry_flag', '__current_pid', 'invoke', 'invoke_syscall',
                  *['___' + n for n in names], '___gettimeofday_with_mach',
                  '_i386_get_ldt', '_i386_set_ldt', '_intprobe'])
    source = c.read_text().replace(', observed_errno', '', 1).replace('observed_errno', 'errno')
    c.write_text('#include <errno.h>\n' + pragmas + source)
    fixture.write_text('''
.text
.globl probe_dispatch
probe_dispatch:
movl raw_low, %eax
movl raw_high, %edx
xorl %ecx, %ecx
cmpl $0, carry_flag
je 1f
stc
ret
1: clc
ret
.globl invoke, invoke_syscall
invoke:
pushl %ebx
movl 8(%esp), %eax
subl $4, %esp
pushl 16(%esp)
call *%eax
addl $8, %esp
popl %ebx
ret
invoke_syscall:
pushl %ebx
movl 8(%esp), %eax
pushl 12(%esp)
pushl $99
call *%eax
addl $8, %esp
popl %ebx
ret
''')
    for tag, directory in [('custom', 'libsyscall/custom'), ('generated', 'gen/bsdsyscalls')]:
        work = out / tag
        work.mkdir(exist_ok=True)
        (work / 'SYS.h').write_text(read(directory + '/SYS.h'))
        for n in names:
            source = read(directory + '/' + ('__' if tag == 'custom' else '___') + n + '.S')
            # PICIFY/NON_LAZY_STUB paste this token, preventing -D renaming.
            source = re.sub(r'\b__current_pid\b', 'probe_pid', source)
            (work / (n + '.S')).write_text(source)
        (work / 'tramp.S').write_text(read(directory + '/custom.S'))
        (work / 'int.S').write_text('#include "SYS.h"\nUNIX_SYSCALL_INT(intprobe, 0)\nret\n')
    print('Emitted real i386 sources, dispatcher fixture and production errno trampolines:', out)
    raise SystemExit(0)

failed = False
for tag, directory in [('custom', 'libsyscall/custom'), ('generated', 'gen/bsdsyscalls')]:
    work = out / tag
    work.mkdir(exist_ok=True)
    (work / 'SYS.h').write_text(read(directory + '/SYS.h'))
    objects = []
    sources = {n: read(directory + '/' + ('__' if tag == 'custom' else '___') + n + '.S') for n in names}
    custom = read(directory + '/custom.S')
    sources['ldt'] = '#include "SYS.h"\n' + custom[custom.index('\t.globl _i386_get_ldt'):custom.index('\t.globl __thread_set_tsd_base')]
    sources['int'] = '#include "SYS.h"\nUNIX_SYSCALL_INT(intprobe, 0)\nret\n'
    with (shim / 'sys/syscall.h').open('a') as stream:
        stream.write('#define SYS_intprobe 200\n')
    for name, source in sources.items():
        asm = work / (name + '.S')
        asm.write_text(source)
        expanded = subprocess.check_output(['cc', '-E', '-P', '-nostdinc', '-I' + str(shim),
          '-D__i386__', '-U__x86_64__', '-DDARLING', str(asm)], text=True)
        if name == 'gettimeofday':
            expanded = expanded[expanded.index('___gettimeofday_with_mach:'):]
            expanded = '.text\n.globl ___gettimeofday_with_mach\n' + expanded
        expanded = re.sub(r'^\s*\.private_extern.*$', '', expanded, flags=re.M)
        expanded = expanded.replace('4(%esp),%ecx', '%rbx,%rcx')
        expanded = expanded.replace('4(%esp), %ecx', '%rbx, %rcx')
        expanded = re.sub(r'movl?(\s+)%rbx', r'movq\1%rbx', expanded)
        expanded = expanded.replace('(%ecx)', '(%rcx)')
        expanded = expanded.replace('popl', 'popq').replace('pushl', 'pushq')
        expanded = expanded.replace('%esp', '%rsp')
        expanded = re.sub(r'(add|sub)l(\s+\$\d+,\s*%rsp)', r'\1q\2', expanded)
        expanded = expanded.replace('(%rsp),%edx', '(%rsp),%rdx')
        expanded = re.sub(r'movl(\s+\(%rsp\),%rdx)', r'movq\1', expanded)
        expanded = re.sub(r'(popq|pushq)(\s+)%e([acdx]+)', r'\1\2%r\3', expanded)
        expanded = expanded.replace('*%ecx', '*%rcx')
        expanded += '\n.section .note.GNU-stack,"",@progbits\n'
        asm.write_text(expanded)
        obj = asm.with_suffix('.o')
        subprocess.run(['cc', '-c', str(asm), '-o', str(obj)], check=True)
        objects.append(str(obj))
    binary = work / 'check'
    subprocess.run(['cc', '-no-pie', '-Wall', '-Wextra', '-Werror', str(c), str(fixture), *objects, '-o', str(binary)], check=True)
    for i, name in enumerate([*names, 'get_ldt', 'set_ldt', 'int']):
        result = subprocess.run([str(binary), str(i)])
        print(tag, name, 'exit', result.returncode, flush=True)
        failed |= bool(result.returncode)
# The review also identifies the same always-taken comparison in the adjacent
# Darling x86_64 fork/vfork branches. These run natively without a width adapter.
c64 = out / 'fork64.c'
c64.write_text(r'''
#include <stdio.h>
int raw_low, raw_high, carry_flag, observed_errno, __current_pid;
extern long ___fork(void), ___vfork(void);
int main(void) {
 long (*calls[])(void) = {___fork, ___vfork};
 int values[] = {-9, -1, -4095, -4096, 0, 7};
 int failed = 0;
 for (unsigned w = 0; w < 2; ++w)
 for (carry_flag = 0; carry_flag < 2; ++carry_flag)
 for (unsigned i = 0; i < sizeof(values)/sizeof(*values); ++i)
 for (int cache = 0; cache < 2; ++cache) {
   raw_low = values[i]; observed_errno = 0;
   int initial = cache ? -3 : 123;
   __current_pid = initial;
   int error = (unsigned)raw_low >= (unsigned)-4095;
   long result = calls[w]();
   int pid = w ? ((initial >= 0 ? -1 : initial - 1) + (error || raw_low != 0))
       : (!error && !raw_low ? 0 : initial);
   if (result != (error ? -1 : raw_low) || observed_errno != (error ? -raw_low : 0) || __current_pid != pid) {
     if (!failed) fprintf(stderr, "x86_64 %s raw=%d returned=%ld errno=%d pid=%d\n",
       w ? "vfork" : "fork", raw_low, result, observed_errno, __current_pid);
     failed = 1;
   }
 }
 if (!failed) puts("x86_64 fork/vfork: native errno and PID paths PASS");
 return failed;
}
''')
fixture64 = out / 'fixture64.S'
fixture64.write_text(fixture.read_text().replace('movl raw_low(%rip), %eax', 'movslq raw_low(%rip), %rax') + '''
.text
.globl _cerror
_cerror:
movl %edi, observed_errno(%rip)
movq $-1, %rax
movq $-1, %rdx
ret
''')
for tag, directory in [('custom', 'libsyscall/custom'), ('generated', 'gen/bsdsyscalls')]:
    work = out / tag
    objects = []
    for name in ['fork', 'vfork']:
        asm = work / (name + '64.S')
        asm.write_text(read(directory + '/' + ('__' if tag == 'custom' else '___') + name + '.S'))
        expanded = subprocess.check_output(['cc', '-E', '-P', '-nostdinc', '-I' + str(shim), '-DDARLING', str(asm)], text=True)
        asm.write_text(expanded + '\n.section .note.GNU-stack,"",@progbits\n')
        obj = asm.with_suffix('.o')
        subprocess.run(['cc', '-c', str(asm), '-o', str(obj)], check=True)
        objects.append(str(obj))
    binary = work / 'fork64'
    subprocess.run(['cc', '-no-pie', '-Wall', '-Wextra', '-Werror', str(c64), str(fixture64), *objects, '-o', str(binary)], check=True)
    result = subprocess.run([str(binary)])
    print(tag, 'fork64 exit', result.returncode, flush=True)
    failed |= bool(result.returncode)
raise SystemExit(failed)
