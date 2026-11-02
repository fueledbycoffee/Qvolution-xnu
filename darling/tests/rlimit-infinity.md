# RLIM_INFINITY conversion

Compile `rlimit-infinity.c` as a Darwin Mach-O (x86_64 and i386) against the
configured SDK and libSystem, then run it with `darling shell` in a private
prefix with the rebuilt kernel library. It prints each limit, requires every
value to be at most Darwin's `RLIM_INFINITY` (2^63 - 1, `bsd/sys/resource.h`).
It then checks the setter in a child process against what Linux stored, read
from `/proc/self/limits` (Darling links `/proc` to the host's): a finite soft
limit under an unlimited hard limit, a return to unlimited, rejection with
EINVAL of a soft limit above the hard limit (including `RLIM_INFINITY + 1`
over `RLIM_INFINITY`, as `bsd/kern/kern_resource.c` rejects it), and an
unchanged finite pair. Exit 77 means the host's data-size hard limit is not
unlimited, so the setter was not tested.

Linux reports "no limit" as all ones. Passed through unchanged, that is larger
than any Darwin limit: Chromium-based apps (Electron) read `RLIMIT_DATA` as a
finite 2^64 - 1 byte address-space limit and stop with a trap while sizing the
V8 heap. Without the getter conversion the test fails at the first unlimited
resource; without the setter conversion Linux stores a finite 2^63 - 1 hard
limit; converting without checking the pair first accepts the inverted pair.
