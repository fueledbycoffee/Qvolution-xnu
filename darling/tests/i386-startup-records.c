#include <assert.h>
#include <darling/emulation/common/i386-startup.h>

int main(void)
{
	const char* empty[] = {0};
	const char* off[] = {"DARLING_I386_STARTUP=0", 0};
	const char* prefix[] = {"DARLING_I386_STARTUP=1-extra", 0};
	const char* short_name[] = {"DARLING_I386_STARTUP", 0};
	const char* on[] = {"OTHER=1", "DARLING_I386_STARTUP=1", 0};
	assert(!darling_i386_startup_enabled(0));
	assert(!darling_i386_startup_enabled(empty));
	assert(!darling_i386_startup_enabled(off));
	assert(!darling_i386_startup_enabled(prefix));
	assert(!darling_i386_startup_enabled(short_name));
	assert(darling_i386_startup_enabled(on));
	DARLING_I386_STARTUP_STAGE(darling_i386_startup_enabled(off), "test-disabled");
	return 0;
}
