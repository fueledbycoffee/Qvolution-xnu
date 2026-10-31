#include <darling/emulation/xnu_syscall/bsd/helper/network/duct.h>

#include <sys/socket.h>
#include <sys/errno.h>

#include <darling/emulation/conversion/network/duct.h>
#include <darling/emulation/conversion/errno.h>
#include <darling/emulation/linux_premigration/vchroot_expand.h>
#include <darling/emulation/common/bsdthread/per_thread_wd.h>

#include <stddef.h>

unsigned long sockaddr_fixup_size_from_bsd(const void* bsd_sockaddr, int bsd_sockaddr_len) {
	/* Callers allocate before conversion validates the supplied length. */
	if (bsd_sockaddr_len < 2 || bsd_sockaddr_len > 512)
		return sizeof(struct sockaddr_fixup);
	unsigned long size = bsd_sockaddr_len;

	/* A PF_LOCAL sockaddr is rewritten in place, so the buffer has to hold the
	 * host's struct sockaddr_un on top of whatever the guest passed, and
	 * sockaddr_fixup_from_bsd() copies all bsd_sockaddr_len bytes into it. The
	 * callers accept a socklen of up to 512, so without this floor a guest
	 * asking for a long AF_LOCAL address would write past the allocation. */
	if (((const struct sockaddr_fixup*) bsd_sockaddr)->bsd_family == PF_LOCAL && size < sizeof(struct sockaddr_fixup))
		size = sizeof(struct sockaddr_fixup);

	return size;
}

/* Copies a path out of a fixed-up sockaddr into a NUL-terminated buffer, reading
 * no further than the address actually covers. The guest need not NUL-terminate
 * sun_path within socklen (Darwin's SUN_LEN is offsetof(sun_path) + strlen,
 * with no terminator) and the tail of the buffer past that is uninitialised
 * stack, so a plain strcpy() both overruns the source and turns a short address
 * into a garbage path. */
static void copy_path(char* dest, __SIZE_TYPE__ dest_size, const struct sockaddr_fixup* in, int in_len) {
	const int path_off = (int) offsetof(struct sockaddr_fixup, sun_path);
	__SIZE_TYPE__ max;

	if (in_len <= path_off) {
		/* An address that does not even reach the path carries no path. */
		dest[0] = '\0';
		return;
	}

	/* Never read past sun_path itself, whatever the destination allows. */
	max = sizeof(in->sun_path);
	if (max > dest_size - 1)
		max = dest_size - 1;
	if (in_len < (int) sizeof(*in) && (in_len - path_off) < (int) max)
		max = in_len - path_off;

	strncpy(dest, in->sun_path, max);
	dest[max] = '\0';
}

int sockaddr_fixup_from_bsd(struct sockaddr_fixup* out, const void* bsd_sockaddr, int bsd_sockaddr_len) {
	int ret = bsd_sockaddr_len;
	if (bsd_sockaddr_len < 2 || bsd_sockaddr_len > 512)
		return -EINVAL;
	const struct sockaddr_fixup* input = bsd_sockaddr;
	if (input->bsd_family == PF_LOCAL) {
		const int offset = (int) offsetof(struct sockaddr_fixup, bsd_sun_path);
		if (bsd_sockaddr_len > offset + SOCKADDR_FIXUP_BSD_PATH_MAX)
			return -EINVAL;

	}

	memcpy(out, bsd_sockaddr, bsd_sockaddr_len);

	out->linux_family = sfamily_bsd_to_linux(out->bsd_family);

	if (out->linux_family == LINUX_PF_LOCAL) {
		struct vchroot_expand_args vc;
		vc.flags = VCHROOT_FOLLOW;
		vc.dfd = get_perthread_wd();

		copy_path(vc.path, sizeof(vc.path), out, bsd_sockaddr_len);

		ret = vchroot_expand(&vc);
		if (ret < 0)
			return errno_linux_to_bsd(ret);

		/* A shortened name can denote a different socket or even its parent
		 * directory. Reject before copying; a later root-alias mapping may
		 * supply a representable spelling of the same endpoint. */
		if (strlen(vc.path) >= sizeof(out->sun_path))
			return -ENAMETOOLONG;
		strncpy(out->sun_path, vc.path, sizeof(out->sun_path) - 1);
		out->sun_path[sizeof(out->sun_path) - 1] = '\0';
		/* + 1 for the NUL terminator: a sockaddr length is the offset of the
		 * path plus the path plus its terminator, and Linux's connect() reads
		 * exactly that many bytes, so omitting it truncates the path by one and
		 * the call fails on a socket that exists. The reverse direction,
		 * sockaddr_fixup_from_linux, has always had the + 1; this one did not. */
		ret = (int) (offsetof(struct sockaddr_fixup, sun_path) + strlen(out->sun_path) + 1);
	}

	return ret;
}

int sockaddr_fixup_from_linux(struct sockaddr_fixup* out, const void* linux_sockaddr, int linux_sockaddr_len) {
	int ret = linux_sockaddr_len;
	if (linux_sockaddr_len == 0)
		return 0; // recvfrom may report no source address.
	if (linux_sockaddr_len < 2 || linux_sockaddr_len > SOCKADDR_FIXUP_STORAGE_MAX)
		return -EINVAL;

	memcpy(out, linux_sockaddr, linux_sockaddr_len);

	out->bsd_family = sfamily_linux_to_bsd(out->linux_family);

	if (out->bsd_family == PF_LOCAL) {
		if (linux_sockaddr_len == (int) offsetof(struct sockaddr_fixup, sun_path)) {
			out->bsd_length = linux_sockaddr_len;
			return linux_sockaddr_len; // An unnamed local socket has no pathname.
		}
		struct vchroot_unexpand_args vc;

		copy_path(vc.path, sizeof(vc.path), out, linux_sockaddr_len);

		ret = vchroot_unexpand(&vc);
		if (ret < 0)
			return errno_linux_to_bsd(ret);

		/* Darwin's pathname field is narrower than Linux's. Length-bounded
		 * names may fill all 104 bytes without a terminating NUL. */
		unsigned long path_length = strlen(vc.path);
		if (path_length > sizeof(out->bsd_sun_path))
			return -ENAMETOOLONG;
		memcpy(out->bsd_sun_path, vc.path, path_length);
		ret = (int) (offsetof(struct sockaddr_fixup, bsd_sun_path) + path_length);
		if (path_length < sizeof(out->bsd_sun_path)) {
			out->bsd_sun_path[path_length] = '\0';
			ret++;
		}
	}

	if (ret >= 0) {
		out->bsd_length = ret;
	}

	return ret;
}


int sockaddr_fixup_output_capacity(const void* address, const int* length) {
	if (length == NULL)
		return -EFAULT;
	int capacity = *length;
	if (capacity < 0)
		return -EINVAL;
	if (capacity > 0 && address == NULL)
		return -EFAULT;
	return capacity;
}

int sockaddr_fixup_copyout(void* address, int capacity, int* length,
	const void* native_address, int native_length) {
	if (length == NULL || (capacity > 0 && address == NULL) ||
		(native_length > 0 && native_address == NULL))
		return -EFAULT;
	if (capacity < 0 || native_length < 0 || native_length > SOCKADDR_FIXUP_STORAGE_MAX)
		return -EINVAL;
	union sockaddr_fixup_storage translated;
	int converted = sockaddr_fixup_from_linux(&translated.address, native_address, native_length);
	if (converted < 0)
		return converted;
	int copied = capacity < converted ? capacity : converted;
	if (copied > 0)
		memcpy(address, translated.bytes, copied);
	*length = converted;
	return 0;
}
