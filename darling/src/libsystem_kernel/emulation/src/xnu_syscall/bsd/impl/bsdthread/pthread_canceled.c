#include <darling/emulation/xnu_syscall/bsd/impl/bsdthread/pthread_canceled.h>

#include <stddef.h>
#include <sys/errno.h>

#include <darling/emulation/common/base.h>
#include <darling/emulation/conversion/errno.h>
#include <darling/emulation/conversion/duct_errno.h>
#include <darling/emulation/linux_premigration/linux-syscalls/linux.h>
#include <darling/emulation/xnu_syscall/bsd/impl/bsdthread/bsdthread_create.h>

#include <darlingserver/rpc.h>

long sys_pthread_canceled(int action)
{
	if (action == 0 && !uses_threads())
		return -EINVAL;

	// darlingserver does not implement cancellation yet: it answers ENOSYS,
	// at the cost of a round trip in every cancellation point (read, recv,
	// send, fcntl...). Give its answer without asking.
	return errno_linux_to_bsd(-LINUX_ENOSYS);
}
