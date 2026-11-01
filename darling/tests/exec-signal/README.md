# SIGWINCH during exec

`sigexc_handler` must not call libc `getenv`: it locks, and dyld's static libc
has not had `_program_vars_init` initialize `_NSGetEnviron` yet. An unset
`DARLING_LOG_SIGNALS` variable still invokes `getenv`. The handler reaches that
diagnostic before testing the SIGWINCH default-ignore disposition. Remove that
optional diagnostic; the existing kernel logger remains.

Specification: rung 3, `bsd/man/man2/execve.2` (caught signals reset, ignored
signals and masks survive exec) and `bsd/man/man2/sigaction.2` (SIGWINCH defaults
to discard). Source evidence: our dyld calls `sigexc_setup` before application
initializers; libc `stdlib/FreeBSD/getenv.c` dereferences `_NSGetEnviron`, whose
dynamic pointer in `sys/crt_externs.c` starts NULL. Both are compiled into dyld
with `__DYNAMIC__` in the Stage41d build. Rung 4: the Stage41d exec-stage records
and darlingserver log identify SIGWINCH before checkout/exec and again in dyld,
where the final handler stops between its entry and context forwarding.

Native control (through the build slot in the fleet):

```
python3 darling/tests/exec-signal-native.py --out /tmp/exec-signal
```

It compiles the unchanged production handler body with host RPC/context
adapters, for ordinary and VARIANT_DYLD builds. `getenv` deliberately exits 91:
no signal may enter that machinery even when the environment is unavailable.
The unpatched handler fails at this check, and the patch passes default-ignore,
explicit-ignore, caught-handler and fatal-default controls, preserving balanced
interrupt RPCs and re-raises. This is also CTest `exec-signal-libc-independence`.
It does not test real Mach contexts, debugger behavior or Darling startup.

Compile `parent.c` and `tiny.c` as separate guest executables with the normal
Darling SDK/compiler, then run `parent /absolute/guest/path/tiny 200`. The parent
repeatedly sends SIGWINCH across fork/exec, cycling default, caught, ignored and
blocked dispositions. The tiny executable checks inherited signal masks and
raises another SIGWINCH. A deadline kills/reaps only the child it created.
The pair is portable to a native POSIX host as a harness control; a host pass
does not qualify the Darling change. Run both unpatched and patched Darling,
including `DARLING_QV_PTY_DELAY=execve.rpc.checkout.entry:500` and
`DARLING_QV_PTY_DELAY=execve.linux.entry:500`, and with that variable unset.

Then rerun Terminal in a fresh runner-owned prefix for the same delay settings.
Judge its shell from our opt-in stages (`program=bash` reaching
`libsystem_initialized`) and an authored shell marker. Never read Terminal's
stdout/stderr. The Terminal and guest before/after gates require a container
runner and remain unverified in the worker sandbox.
