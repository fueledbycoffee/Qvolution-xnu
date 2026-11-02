#include <darling/emulation/xnu_syscall/bsd/impl/misc/setrlimit.h>

#include <sys/errno.h>

#include <darling/emulation/common/base.h>
#include <darling/emulation/conversion/errno.h>
#include <darling/emulation/linux_premigration/linux-syscalls/linux.h>

long sys_setrlimit(unsigned int which, const struct xnu_rlimit* rlp)
{
	int ret;

	which = rlimit_bsd_to_linux(which);
	if (which == -1)
		return -EINVAL;

	// Check the caller's pair before conversion: every value from
	// RLIM_INFINITY up maps to Linux's single "unlimited", which would make
	// an inverted pair such as { RLIM_INFINITY + 1, RLIM_INFINITY } equal.
	if (rlp->rlim_cur > rlp->rlim_max)
		return -EINVAL;

	struct xnu_rlimit lim = { rlim_value_bsd_to_linux(rlp->rlim_cur), rlim_value_bsd_to_linux(rlp->rlim_max) };

	ret = LINUX_SYSCALL(__NR_prlimit64, 0, which, &lim, 0);
	if (ret < 0)
		ret = errno_linux_to_bsd(ret);

	return ret;
}
