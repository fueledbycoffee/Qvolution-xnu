# S2C ancillary receive capacity

Run from the xnu repository:

```
python3 darling/tests/s2c-receive-capacity.py /path/to/darling --out /path/to/results
```

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

Specification: the Linux `recvmsg(2)` input/output contract for
`msg_controllen` and the actual native socket observations above (rung 4).
The test does not boot Darling or establish Mach/IOSurface acceptance; run the
unchanged canonical guest fixture separately.
