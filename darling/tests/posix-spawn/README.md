# Spawn regressions

`postfork-exec.c` uses public Darwin interfaces and an absolute path to a separately
built owned child that exits zero. Its ordinary fork/exec control must succeed,
then both ordinary and close-on-exec-default spawn modes must execute that same
child. It discards stdout and makes no permission calls or fallback. Compile/link
against the normally selected runtime and run only under the root-owned guest
orchestration. It does not fabricate or qualify a voice catalog.

`run-server-access-control.py --fork-source <fork.c> --helper-source <lkm.c>
--output <new-directory>` compiles the exact production raw-fork and factored
helper bodies against native host adapters. A separate authored sibling server
writes one known integer into an authored child buffer, testing the existing
process-local relationship with no foreign process or image involved. No address
or memory contents are printed. This requires unchanged Yama relational mode and
available process_vm_writev; a self-buffer precondition failure returns explicit
unqualified77 and is not a passing regression. The public guest control remains
required, including on hosts that prohibit this system call.

`run-source-control.py --source <posix_spawn.c> --output <new-directory>` compiles
the exact production `sys_posix_spawn` function body against small Linux host
adapters. It uses real fork/exec/pipe/wait operations, with NULL attributes/actions
only. The test adapter declarations are not Darwin ABI tests. They verify reported
exec failure, PID publication, child reaping, interrupted error-pipe read, failed
fork descriptor cleanup, and caller ownership of a successful child. Warnings for
the production body's pre-existing unused `err` and mixed signedness loops are
suppressed; all checks remain unconditional under NDEBUG. No production function
body is rewritten by the runner.

Source controls complement the real guest regression and do not establish full
posix_spawn/SETEXEC/arm64 behavior. Keep original source and before logs when
comparing a correction. Do not run full unsafe libSystem atfork callbacks inside
the raw spawn child as a shortcut for required owned runtime setup.
