# RLIM_INFINITY conversion

Compile `rlimit-infinity.c` as a Darwin Mach-O (x86_64 and i386) against the
configured SDK and libSystem, then run it with `darling shell` in a private
prefix with the rebuilt kernel library. It prints each limit, requires every
value to be at most Darwin's `RLIM_INFINITY` (2^63 - 1, `bsd/sys/resource.h`)
and then round-trips a soft limit of an unlimited resource through a finite
value and back to `RLIM_INFINITY`. Exit 77 means the host left no resource
unlimited, so nothing was tested.

Linux reports "no limit" as all ones. Passed through unchanged, that is larger
than any Darwin limit: Chromium-based apps (Electron) read `RLIMIT_DATA` as a
finite 2^64 - 1 byte address-space limit and stop with a trap while sizing the
V8 heap. Before the fix this test fails at the first unlimited resource.
