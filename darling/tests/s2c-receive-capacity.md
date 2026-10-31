# S2C ancillary receive capacity

Run from the xnu repository:

```
python3 darling/tests/s2c-receive-capacity.py /path/to/darling --out /path/to/results
```

Or use the standalone native CTest runner (no Darling container required):

```
cmake -S darling/tests -B native-tests -DDARLING_SUPERPROJECT=/path/to/darling
ctest --test-dir native-tests --output-on-failure
```

Set `-DDARLINGSERVER_SOURCE=/path/to/darlingserver` to test a separate component
checkout; the Python entry point accepts the same override as `--darlingserver`.

The test extracts the actual `dserver_rpc_hooks_receive_message` function and
uses Darling's `rpc-supplement.h` protocol declarations. Native Linux adapters
provide its surrounding build interfaces. Real Unix datagram socketpairs,
`recvmsg`, `SCM_RIGHTS`, `mmap`, and `munmap` exercise the receive loop. The
reply-only `sendto` operation uses `sendmsg` on the connected socketpair because
the worker sandbox blocks `sendto`, including on local sockets.

Both cases first deliver a no-FD S2C `munmap`. One then delivers two FD-carrying
S2C mmap calls and a final ordinary reply; the other delivers an ordinary reply
carrying an FD. The original loop aborts with `Bad S2C call: no FD, but expected
one` in the first case and truncates ancillary data in the second. Restoring
`msg_controllen` before each internal receive fixes both. The final reply must
still expose its actual received control length, rather than buffer capacity.

Two further cases generate and execute the actual `interrupt_enter` wrapper.
After receiving an unrelated no-FD reply, it pushes that reply and jumps to
`retry_receive`. The next packet carries either an S2C mmap FD or another
ordinary reply FD. Both fail with the original generator even with the internal
receive-loop fix. Resetting capacity at the generated retry label fixes them.
The push-reply adapter checks the actual control length, flags and FD contents
at the deferral interface; it does not implement server-side deferred delivery.
The expected final reply carries no FD and must retain a zero control length.

Specification: the Linux `recvmsg(2)` input/output contract for
`msg_controllen` and the actual native socket observations above (rung 4).
The test does not boot Darling or establish Mach/IOSurface acceptance; run the
unchanged canonical guest fixture separately.
