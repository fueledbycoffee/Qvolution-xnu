#include <darling/emulation/xnu_syscall/bsd/impl/network/recvfrom.h>

#include <stddef.h>

#include <darling/emulation/common/base.h>
#include <darling/emulation/conversion/errno.h>
#include <darling/emulation/conversion/network/duct.h>
#include <darling/emulation/linux_premigration/linux-syscalls/linux.h>
#include <darling/emulation/xnu_syscall/bsd/impl/network/socket.h>
#include <darling/emulation/xnu_syscall/bsd/helper/network/duct.h>
#include <darling/emulation/xnu_syscall/bsd/helper/bsdthread/cancelable.h>

extern void *memcpy(void *dest, const void *src, __SIZE_TYPE__ n);

long sys_recvfrom(int fd, void* buf, unsigned long len,
		int flags, void* from, int* socklen)
{
	CANCELATION_POINT();
	return sys_recvfrom_nocancel(fd, buf, len, flags, from, socklen);
}

long sys_recvfrom_nocancel(int fd, void* buf, unsigned long len,
		int flags, void* from, int* socklen)
{
	int capacity = 0;
	if (from != NULL) {
		capacity = sockaddr_fixup_output_capacity(from, socklen);
		if (capacity < 0)
			return capacity;
	}
	union sockaddr_fixup_storage native;
	int native_length = sizeof(native);
	void* native_address = from != NULL ? native.bytes : NULL;
	int* native_size = from != NULL ? &native_length : NULL;
	int linux_flags = msgflags_bsd_to_linux(flags);
	int ret;
#ifdef __NR_socketcall
	ret = LINUX_SYSCALL(__NR_socketcall, LINUX_SYS_RECVFROM, ((long[6]) { fd, buf, len,
		linux_flags, native_address, native_size }));
#else
	ret = LINUX_SYSCALL(__NR_recvfrom, fd, buf, len, linux_flags, native_address, native_size);
#endif
	if (ret < 0)
		return errno_linux_to_bsd(ret);
	if (from != NULL) {
		int converted = sockaddr_fixup_copyout(from, capacity, socklen, native.bytes, native_length);
		if (converted < 0)
			return converted;
	}
	return ret;
}
