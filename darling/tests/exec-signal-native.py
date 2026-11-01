#!/usr/bin/env python3
"""Run sigexc_handler's production body without initialized guest libc."""
import argparse
from pathlib import Path
import resource
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--out', type=Path, required=True)
p.add_argument('--source', type=Path)
a = p.parse_args()
xnu = Path(__file__).resolve().parents[2]
em = xnu / 'darling/src/libsystem_kernel/emulation'
source = a.source or em / 'src/linux_premigration/signal/sigexc.c'
body = source.read_text().split('void sigexc_handler(int linux_signum,', 1)[1]
body = 'void sigexc_handler(int linux_signum,' + body.split('#define DUMPREG', 1)[0]
a.out.mkdir(parents=True, exist_ok=True)
(a.out / 'handler.inc').write_text(body)
constants = (em / 'include/conversion/signal/duct_signals.h').read_text()
(a.out / 'linux-signals.inc').write_text('\n'.join(
    s for s in constants.splitlines() if s.startswith('#define LINUX_')) + '\n')
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
failed = False
for variant in ('normal', 'dyld'):
    exe = a.out / variant
    subprocess.run(['clang', '-std=gnu99', '-Wall', '-Wextra', '-Werror',
        '-Wno-unused-variable', '-Wno-unused-parameter', '-Wno-unused-function',
        '-fsanitize=undefined', '-fno-sanitize-recover=all',
        *(['-DVARIANT_DYLD'] if variant == 'dyld' else []),
        '-I' + str(a.out.resolve()), str(xnu / 'darling/tests/exec-signal/native.c'),
        '-o', str(exe)], check=True)
    result = subprocess.run([str(exe.resolve())], capture_output=True, text=True)
    (a.out / (variant + '.txt')).write_text(
        f'exit={result.returncode}\n' + result.stdout + result.stderr)
    print(f'{variant}: exit={result.returncode}', result.stdout.strip(), flush=True)
    if result.returncode:
        failed = True
raise SystemExit(1 if failed else 0)
