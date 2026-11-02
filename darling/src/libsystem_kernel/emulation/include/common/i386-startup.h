#ifndef DARLING_I386_STARTUP_H
#define DARLING_I386_STARTUP_H

/* Fixed records only: usable before dyld rebasing, libc and guest TLS. */
static inline int darling_i386_startup_enabled(const char* const entries[])
{
	static const char option[] = "DARLING_I386_STARTUP=1";
	if (entries) {
		for (; *entries; ++entries) {
			unsigned int i = 0;
			while (option[i] && option[i] == (*entries)[i])
				++i;
			if (!option[i] && !(*entries)[i])
				return 1;
		}
	}
	return 0;
}

#if defined(__i386__)
#include <darling/emulation/linux_premigration/linux-syscalls/linux.h>
extern int darling_i386_startup_records __attribute__((visibility("hidden")));
static inline void darling_i386_startup_write(const char* record, unsigned int length)
{
	long result;
	__asm__ volatile("int $0x80" : "=a" (result)
		: "0" (__NR_write), "b" (2), "c" (record), "d" (length)
		: "memory", "cc");
	(void)result;
}
#define DARLING_I386_STARTUP_STAGE(enabled, stage) do { \
	if (enabled) { \
		static const char record[] = "[darling-i386] " stage "\n"; \
		darling_i386_startup_write(record, sizeof(record) - 1); \
	} \
} while (0)
#define DARLING_I386_STARTUP_CURRENT(stage) \
	DARLING_I386_STARTUP_STAGE(darling_i386_startup_records, stage)
#else
#define DARLING_I386_STARTUP_CURRENT(stage) ((void)0)
#define DARLING_I386_STARTUP_STAGE(enabled, stage) ((void)0)
#endif

#endif
