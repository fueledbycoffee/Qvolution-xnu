#include <darling/emulation/xnu_syscall/bsd/helper/misc/sysctl_hw.h>

#include <mach/host_info.h>
#include <mach/machine.h>
#include <mach/mach_init.h>
#include <mach/vm_page_size.h>
#include <sys/errno.h>
#include <stdint.h>

#include <darling/emulation/xnu_syscall/bsd/helper/misc/sysctl_kern.h>
#include <darling/emulation/xnu_syscall/bsd/helper/misc/sysctl_proc.h>
#include <darling/emulation/linux_premigration/ext/sys/utsname.h>
#include <darling/emulation/common/simple.h>
#include <darling/emulation/common/string.h>
#include <darling/emulation/xnu_syscall/bsd/helper/misc/readline.h>
#include <darling/emulation/xnu_syscall/bsd/impl/fcntl/open.h>
#include <darling/emulation/xnu_syscall/bsd/impl/unistd/close.h>
#include <darling/emulation/conversion/fcntl/open.h>
#include <darling/emulation/xnu_syscall/bsd/impl/unistd/read.h>

extern kern_return_t mach_port_deallocate(ipc_space_t task, mach_port_name_t name);
extern kern_return_t host_info(mach_port_name_t host, int itype, void* hinfo, mach_msg_type_number_t* count);

/* Darling specific */
enum {
	_HW_PHYSICAL_CPU = 1000,
	_HW_PHYSICAL_CPU_MAX,
	_HW_LOGICAL_CPU,
	_HW_LOGICAL_CPU_MAX,
	_HW_CPUTYPE,
	_HW_CPUSUBTYPE,
	_HW_CPUTHREADTYPE,
	_HW_64BITCAPABLE,
	_HW_PACKAGES,
	_HW_CPUFREQUENCY,
	_HW_OPTIONAL = 1100,
	_HW_OPTIONAL_ARM = 1101,
	/* hw.optional.* leaves: _HW_FEATURE + index into hw_features[] */
	_HW_FEATURE = 2000,
};

static long feature_value(int index, void* old, unsigned long* oldlen, void* _new);

/* sysctlbyname() calls handlers without the MIB, so each feature has its own. */
#define FEATURE_HANDLER(i) static sysctl_handler(handle_feature_##i) { return feature_value(i, old, oldlen, _new); }

/*
 * CPU features as macOS reports them under hw.optional, answered from the
 * Linux host's AT_HWCAP/AT_HWCAP2 (arm64 bits; see the kernel's
 * arch/arm64/include/uapi/asm/hwcap.h). compiler-rt's outline atomics read
 * hw.optional.armv8_1_atomics to decide whether to use LSE instructions.
 * Absent features read as 0, as on macOS; on other architectures all do.
 */
#define HWCAP1(bit) (bit)
#define HWCAP2(bit) (64 + (bit))
#define HWCAP_ALWAYS (-1)
static const struct { const char* name; int bit; } hw_features[] = {
	/* hw.optional.<name> */
	{ "floatingpoint", HWCAP1(0) },
	{ "neon", HWCAP1(1) },
	{ "neon_hpfp", HWCAP1(10) },
	{ "neon_fp16", HWCAP1(10) },
	{ "armv8_1_atomics", HWCAP1(8) },
	{ "armv8_crc32", HWCAP1(7) },
	{ "armv8_2_fhm", HWCAP1(23) },
	{ "armv8_2_sha512", HWCAP1(21) },
	{ "armv8_2_sha3", HWCAP1(17) },
	{ "armv8_3_compnum", HWCAP1(14) },
	{ "arm64", HWCAP_ALWAYS },
	/* hw.optional.arm.<name>, from here on */
	{ "FEAT_LSE", HWCAP1(8) },
	{ "FEAT_LSE2", HWCAP1(25) },
	{ "FEAT_CRC32", HWCAP1(7) },
	{ "FEAT_AES", HWCAP1(3) },
	{ "FEAT_PMULL", HWCAP1(4) },
	{ "FEAT_SHA1", HWCAP1(5) },
	{ "FEAT_SHA256", HWCAP1(6) },
	{ "FEAT_SHA512", HWCAP1(21) },
	{ "FEAT_SHA3", HWCAP1(17) },
	{ "FEAT_FP16", HWCAP1(9) },
	{ "FEAT_RDM", HWCAP1(12) },
	{ "FEAT_JSCVT", HWCAP1(13) },
	{ "FEAT_FCMA", HWCAP1(14) },
	{ "FEAT_LRCPC", HWCAP1(15) },
	{ "FEAT_LRCPC2", HWCAP1(26) },
	{ "FEAT_DPB", HWCAP1(16) },
	{ "FEAT_DotProd", HWCAP1(20) },
	{ "FEAT_FHM", HWCAP1(23) },
	{ "FEAT_DIT", HWCAP1(24) },
	{ "FEAT_FlagM", HWCAP1(27) },
	{ "FEAT_SSBS", HWCAP1(28) },
	{ "FEAT_SB", HWCAP1(29) },
	{ "FEAT_DPB2", HWCAP2(0) },
	{ "FEAT_FlagM2", HWCAP2(7) },
	{ "FEAT_FRINTTS", HWCAP2(8) },
	{ "FEAT_I8MM", HWCAP2(13) },
	{ "FEAT_BF16", HWCAP2(14) },
	{ "FEAT_BTI", HWCAP2(17) },
};
#define HW_FEATURES_ARM_FIRST 11

FEATURE_HANDLER(0)
FEATURE_HANDLER(1)
FEATURE_HANDLER(2)
FEATURE_HANDLER(3)
FEATURE_HANDLER(4)
FEATURE_HANDLER(5)
FEATURE_HANDLER(6)
FEATURE_HANDLER(7)
FEATURE_HANDLER(8)
FEATURE_HANDLER(9)
FEATURE_HANDLER(10)
FEATURE_HANDLER(11)
FEATURE_HANDLER(12)
FEATURE_HANDLER(13)
FEATURE_HANDLER(14)
FEATURE_HANDLER(15)
FEATURE_HANDLER(16)
FEATURE_HANDLER(17)
FEATURE_HANDLER(18)
FEATURE_HANDLER(19)
FEATURE_HANDLER(20)
FEATURE_HANDLER(21)
FEATURE_HANDLER(22)
FEATURE_HANDLER(23)
FEATURE_HANDLER(24)
FEATURE_HANDLER(25)
FEATURE_HANDLER(26)
FEATURE_HANDLER(27)
FEATURE_HANDLER(28)
FEATURE_HANDLER(29)
FEATURE_HANDLER(30)
FEATURE_HANDLER(31)
FEATURE_HANDLER(32)
FEATURE_HANDLER(33)
FEATURE_HANDLER(34)
FEATURE_HANDLER(35)
FEATURE_HANDLER(36)
FEATURE_HANDLER(37)
FEATURE_HANDLER(38)


static sysctl_handler(handle_availcpu);
static sysctl_handler(handle_physicalcpu);
static sysctl_handler(handle_physicalcpu_max);
static sysctl_handler(handle_logicalcpu);
static sysctl_handler(handle_logicalcpu_max);
static sysctl_handler(handle_memsize);
static sysctl_handler(handle_pagesize);
static sysctl_handler(handle_cputype);
static sysctl_handler(handle_cpusubtype);
static sysctl_handler(handle_cputhreadtype);
static sysctl_handler(handle_cpu64bitcapable);
static sysctl_handler(handle_machine);
static sysctl_handler(handle_cpufrequency);
static sysctl_handler(handle_cpufrequency_compat);
static sysctl_handler(handle_cachelinesize);
static sysctl_handler(handle_packages);

const struct known_sysctl sysctls_hw_optional_arm[] = {
	{ .oid = _HW_FEATURE + 11, .type = CTLTYPE_INT, .exttype = "I", .name = "FEAT_LSE", .handler = handle_feature_11 },
	{ .oid = _HW_FEATURE + 12, .type = CTLTYPE_INT, .exttype = "I", .name = "FEAT_LSE2", .handler = handle_feature_12 },
	{ .oid = _HW_FEATURE + 13, .type = CTLTYPE_INT, .exttype = "I", .name = "FEAT_CRC32", .handler = handle_feature_13 },
	{ .oid = _HW_FEATURE + 14, .type = CTLTYPE_INT, .exttype = "I", .name = "FEAT_AES", .handler = handle_feature_14 },
	{ .oid = _HW_FEATURE + 15, .type = CTLTYPE_INT, .exttype = "I", .name = "FEAT_PMULL", .handler = handle_feature_15 },
	{ .oid = _HW_FEATURE + 16, .type = CTLTYPE_INT, .exttype = "I", .name = "FEAT_SHA1", .handler = handle_feature_16 },
	{ .oid = _HW_FEATURE + 17, .type = CTLTYPE_INT, .exttype = "I", .name = "FEAT_SHA256", .handler = handle_feature_17 },
	{ .oid = _HW_FEATURE + 18, .type = CTLTYPE_INT, .exttype = "I", .name = "FEAT_SHA512", .handler = handle_feature_18 },
	{ .oid = _HW_FEATURE + 19, .type = CTLTYPE_INT, .exttype = "I", .name = "FEAT_SHA3", .handler = handle_feature_19 },
	{ .oid = _HW_FEATURE + 20, .type = CTLTYPE_INT, .exttype = "I", .name = "FEAT_FP16", .handler = handle_feature_20 },
	{ .oid = _HW_FEATURE + 21, .type = CTLTYPE_INT, .exttype = "I", .name = "FEAT_RDM", .handler = handle_feature_21 },
	{ .oid = _HW_FEATURE + 22, .type = CTLTYPE_INT, .exttype = "I", .name = "FEAT_JSCVT", .handler = handle_feature_22 },
	{ .oid = _HW_FEATURE + 23, .type = CTLTYPE_INT, .exttype = "I", .name = "FEAT_FCMA", .handler = handle_feature_23 },
	{ .oid = _HW_FEATURE + 24, .type = CTLTYPE_INT, .exttype = "I", .name = "FEAT_LRCPC", .handler = handle_feature_24 },
	{ .oid = _HW_FEATURE + 25, .type = CTLTYPE_INT, .exttype = "I", .name = "FEAT_LRCPC2", .handler = handle_feature_25 },
	{ .oid = _HW_FEATURE + 26, .type = CTLTYPE_INT, .exttype = "I", .name = "FEAT_DPB", .handler = handle_feature_26 },
	{ .oid = _HW_FEATURE + 27, .type = CTLTYPE_INT, .exttype = "I", .name = "FEAT_DotProd", .handler = handle_feature_27 },
	{ .oid = _HW_FEATURE + 28, .type = CTLTYPE_INT, .exttype = "I", .name = "FEAT_FHM", .handler = handle_feature_28 },
	{ .oid = _HW_FEATURE + 29, .type = CTLTYPE_INT, .exttype = "I", .name = "FEAT_DIT", .handler = handle_feature_29 },
	{ .oid = _HW_FEATURE + 30, .type = CTLTYPE_INT, .exttype = "I", .name = "FEAT_FlagM", .handler = handle_feature_30 },
	{ .oid = _HW_FEATURE + 31, .type = CTLTYPE_INT, .exttype = "I", .name = "FEAT_SSBS", .handler = handle_feature_31 },
	{ .oid = _HW_FEATURE + 32, .type = CTLTYPE_INT, .exttype = "I", .name = "FEAT_SB", .handler = handle_feature_32 },
	{ .oid = _HW_FEATURE + 33, .type = CTLTYPE_INT, .exttype = "I", .name = "FEAT_DPB2", .handler = handle_feature_33 },
	{ .oid = _HW_FEATURE + 34, .type = CTLTYPE_INT, .exttype = "I", .name = "FEAT_FlagM2", .handler = handle_feature_34 },
	{ .oid = _HW_FEATURE + 35, .type = CTLTYPE_INT, .exttype = "I", .name = "FEAT_FRINTTS", .handler = handle_feature_35 },
	{ .oid = _HW_FEATURE + 36, .type = CTLTYPE_INT, .exttype = "I", .name = "FEAT_I8MM", .handler = handle_feature_36 },
	{ .oid = _HW_FEATURE + 37, .type = CTLTYPE_INT, .exttype = "I", .name = "FEAT_BF16", .handler = handle_feature_37 },
	{ .oid = _HW_FEATURE + 38, .type = CTLTYPE_INT, .exttype = "I", .name = "FEAT_BTI", .handler = handle_feature_38 },
	{ .oid = -1 }
};

const struct known_sysctl sysctls_hw_optional[] = {
	{ .oid = _HW_FEATURE + 0, .type = CTLTYPE_INT, .exttype = "I", .name = "floatingpoint", .handler = handle_feature_0 },
	{ .oid = _HW_FEATURE + 1, .type = CTLTYPE_INT, .exttype = "I", .name = "neon", .handler = handle_feature_1 },
	{ .oid = _HW_FEATURE + 2, .type = CTLTYPE_INT, .exttype = "I", .name = "neon_hpfp", .handler = handle_feature_2 },
	{ .oid = _HW_FEATURE + 3, .type = CTLTYPE_INT, .exttype = "I", .name = "neon_fp16", .handler = handle_feature_3 },
	{ .oid = _HW_FEATURE + 4, .type = CTLTYPE_INT, .exttype = "I", .name = "armv8_1_atomics", .handler = handle_feature_4 },
	{ .oid = _HW_FEATURE + 5, .type = CTLTYPE_INT, .exttype = "I", .name = "armv8_crc32", .handler = handle_feature_5 },
	{ .oid = _HW_FEATURE + 6, .type = CTLTYPE_INT, .exttype = "I", .name = "armv8_2_fhm", .handler = handle_feature_6 },
	{ .oid = _HW_FEATURE + 7, .type = CTLTYPE_INT, .exttype = "I", .name = "armv8_2_sha512", .handler = handle_feature_7 },
	{ .oid = _HW_FEATURE + 8, .type = CTLTYPE_INT, .exttype = "I", .name = "armv8_2_sha3", .handler = handle_feature_8 },
	{ .oid = _HW_FEATURE + 9, .type = CTLTYPE_INT, .exttype = "I", .name = "armv8_3_compnum", .handler = handle_feature_9 },
	{ .oid = _HW_FEATURE + 10, .type = CTLTYPE_INT, .exttype = "I", .name = "arm64", .handler = handle_feature_10 },
	{ .oid = _HW_OPTIONAL_ARM, .type = CTLTYPE_NODE, .exttype = "", .name = "arm", .subctls = sysctls_hw_optional_arm },
	{ .oid = -1 }
};

const struct known_sysctl sysctls_hw[] = {
	{ .oid = HW_AVAILCPU, .type = CTLTYPE_INT, .exttype = "", .name = "availcpu", .handler = handle_availcpu },
	{ .oid = HW_NCPU, .type = CTLTYPE_INT, .exttype = "", .name = "ncpu", .handler = handle_availcpu },
	{ .oid = _HW_PHYSICAL_CPU, .type = CTLTYPE_INT, .exttype = "", .name = "physicalcpu", .handler = handle_physicalcpu },
	{ .oid = _HW_PHYSICAL_CPU_MAX, .type = CTLTYPE_INT, .exttype = "", .name = "physicalcpu_max", .handler = handle_physicalcpu_max },
	{ .oid = _HW_LOGICAL_CPU, .type = CTLTYPE_INT, .exttype = "", .name = "logicalcpu", .handler = handle_logicalcpu },
	{ .oid = _HW_LOGICAL_CPU_MAX, .type = CTLTYPE_INT, .exttype = "", .name = "logicalcpu_max", .handler = handle_logicalcpu_max },
	{ .oid = HW_MEMSIZE, .type = CTLTYPE_QUAD, .exttype = "U", .name = "memsize", .handler = handle_memsize },
	{ .oid = HW_PAGESIZE, .type = CTLTYPE_INT, .exttype = "I", .name = "pagesize", .handler = handle_pagesize },
	{ .oid = _HW_CPUTYPE, .type = CTLTYPE_INT, .exttype = "", .name = "cputype", .handler = handle_cputype },
	{ .oid = _HW_CPUSUBTYPE, .type = CTLTYPE_INT, .exttype = "", .name = "cpusubtype", .handler = handle_cpusubtype },
	{ .oid = _HW_CPUTHREADTYPE, .type = CTLTYPE_INT, .exttype = "", .name = "cputhreadtype", .handler = handle_cputhreadtype },
	{ .oid = _HW_64BITCAPABLE, .type = CTLTYPE_INT, .exttype = "", .name = "cpu64bit_capable", .handler = handle_cpu64bitcapable },
	{ .oid = HW_MACHINE, .type = CTLTYPE_STRING, .exttype = "S", .name = "machine", .handler = handle_machine },
	{ .oid = _HW_CPUFREQUENCY, .type = CTLTYPE_QUAD, .exttype = "Q", .name = "cpufrequency", .handler = handle_cpufrequency },
	{ .oid = HW_CPU_FREQ, .type = CTLTYPE_INT, .exttype = "I", .name = "cpufrequency_compat", .handler = handle_cpufrequency_compat },
	{ .oid = HW_CACHELINE, .type = CTLTYPE_QUAD, .exttype = "U", .name = "cachelinesize", .handler = handle_cachelinesize },
	{ .oid = _HW_PACKAGES, .type = CTLTYPE_INT, .exttype = "I", .name = "packages", .handler = handle_packages },
	{ .oid = _HW_OPTIONAL, .type = CTLTYPE_NODE, .exttype = "", .name = "optional", .subctls = sysctls_hw_optional },
	{ .oid = -1 }
};

static const struct host_basic_info* gethostinfo(void)
{
	static struct host_basic_info hinfo;

	if (!hinfo.avail_cpus)
	{
		mach_msg_type_number_t hcount = HOST_BASIC_INFO_COUNT;
		mach_port_t hself;

		hself = mach_host_self();
		host_info(hself, HOST_BASIC_INFO, &hinfo, &hcount);
		mach_port_deallocate(mach_task_self(), hself);
	}

	return &hinfo;
}

sysctl_handler(handle_availcpu)
{
	sysctl_handle_size(sizeof(int));
	*((int*) old) = gethostinfo()->avail_cpus;
	return 0;
}

sysctl_handler(handle_physicalcpu)
{
	sysctl_handle_size(sizeof(int));
	*((int*) old) = gethostinfo()->physical_cpu;
	return 0;
}

sysctl_handler(handle_physicalcpu_max)
{
	sysctl_handle_size(sizeof(int));
	*((int*) old) = gethostinfo()->physical_cpu_max;
	return 0;
}

sysctl_handler(handle_logicalcpu)
{
	sysctl_handle_size(sizeof(int));
	*((int*) old) = gethostinfo()->logical_cpu;
	return 0;
}

sysctl_handler(handle_logicalcpu_max)
{
	sysctl_handle_size(sizeof(int));
	*((int*) old) = gethostinfo()->logical_cpu_max;
	return 0;
}

sysctl_handler(handle_memsize)
{

	sysctl_handle_size(sizeof(unsigned long long));
	*((unsigned long long*) old) = gethostinfo()->max_mem;
	return 0;
}

sysctl_handler(handle_pagesize)
{
	if (old == NULL)
	{
		*oldlen = sizeof(int);
		return 0;
	}
	else if (*oldlen < sizeof(int))
	{
		return -EINVAL;
	}
	else
	{
		// Not 4K everywhere: aarch64 kernels are commonly built with 16K pages. mldr puts the
		// host's page shift in the commpage and libsyscall's mach_init_doit() derives this.
		*((int*) old) = (int) vm_page_size;

		// libc's sysconf passes in a `long` for the argument.
		// Apple's code is actually wrong there because it doesn't check the returned size.
		// to deal with this (since apparently it works on real macOS), what we do is zero out
		// the rest of the buffer so the upper half won't be garbage.
		//
		// note that this only works on little endian systems,
		// but that's okay; that's what all Darling platforms are.
		memset((char*)old + sizeof(int), 0, *oldlen - sizeof(int));

		*oldlen = sizeof(int);
	}
	return 0;
}

sysctl_handler(handle_cputype)
{
	sysctl_handle_size(sizeof(int));
	*((int*) old) = gethostinfo()->cpu_type;
	return 0;
}

sysctl_handler(handle_cpusubtype)
{
	sysctl_handle_size(sizeof(int));
	*((int*) old) = gethostinfo()->cpu_subtype;
	return 0;
}

sysctl_handler(handle_cputhreadtype)
{
	sysctl_handle_size(sizeof(int));
	*((int*) old) = gethostinfo()->cpu_threadtype;
	return 0;
}

// Darwin reports hertz; Linux cpufreq reports kilohertz.
static uint64_t cpu_frequency_hz(void)
{
	uint64_t khz = 2400000;
	char buf[16];

	if (read_string("/sys/bus/cpu/devices/cpu0/cpufreq/cpuinfo_max_freq", buf, sizeof(buf)))
		khz = __simple_atoi(buf, NULL);

	return khz * 1000;
}

sysctl_handler(handle_cpufrequency)
{
	sysctl_handle_size(sizeof(uint64_t));
	*((uint64_t*) old) = cpu_frequency_hz();
	return 0;
}

// HW_CPU_FREQ keeps the 32-bit ABI and saturates, as XNU's cpu_clock_rate_hz does.
sysctl_handler(handle_cpufrequency_compat)
{
	uint64_t hz;

	sysctl_handle_size(sizeof(uint32_t));
	hz = cpu_frequency_hz();
	*((uint32_t*) old) = hz > UINT32_MAX ? UINT32_MAX : (uint32_t) hz;
	return 0;
}

sysctl_handler(handle_cpu64bitcapable)
{
	sysctl_handle_size(sizeof(int));
	*((int*) old) = 1; // Darling only supports 64-bit CPUs
	return 0;
}

sysctl_handler(handle_machine)
{
	copyout_string(need_uname()->machine, (char*) old, oldlen);
	return 0;
}

sysctl_handler(handle_cachelinesize)
{
	// A fixed architectural constant, like the page size above. XNU aligns its cache
	// maintenance on 1 << ARM64_CLINE_SHIFT with ARM64_CLINE_SHIFT == 6
	// (osfmk/arm64/proc_reg.h), i.e. 64 bytes on arm64; on arm, MMU_CLINE is 6 for
	// the same reason (osfmk/arm/proc_reg.h).
	sysctl_handle_size(sizeof(unsigned long long));
	*((unsigned long long*) old) = 64;
	return 0;
}

sysctl_handler(handle_packages)
{
    if (!oldlen) return -EINVAL;
    if (_new) return -EPERM;
    sysctl_handle_size(sizeof(uint32_t));
    int fd = sys_open_nocancel("/proc/cpuinfo", BSD_O_RDONLY, 0);
    if (fd < 0) return fd;
    uint32_t packages[256];
    uint32_t count = 0;
    struct rdline_buffer rbuf;
    _readline_init(&rbuf);
    const char* line;
    while ((line = _readline(fd, &rbuf)) != NULL) {
        if (strncmp(line, "physical id", 11) != 0) continue;
        const char* value = line + 11;
        while (*value && *value != ':') ++value;
        if (!*value) continue;
        ++value;
        while (*value == ' ' || *value == '\t') ++value;
        uint32_t package = __simple_atoi(value, NULL);
        uint32_t i;
        for (i = 0; i < count && packages[i] != package; ++i) {}
        if (i == count) {
            if (count == sizeof(packages) / sizeof(packages[0])) {
                close_internal(fd);
                return -EOVERFLOW;
            }
            packages[count++] = package;
        }
    }
    close_internal(fd);
    /* Architectures without a physical-id field describe one package. */
    *(uint32_t*)old = count ? count : 1;
    return 0;
}

#if defined(__aarch64__) || defined(__arm64__)
/* AT_HWCAP and AT_HWCAP2 from /proc/self/auxv, read once. */
static int read_hwcaps(unsigned long long caps[2])
{
	static volatile int state = 0; /* 0: not read, 1: read, -1: unavailable, -2: reading */
	static unsigned long long cached[2];
	if (__builtin_expect(state == 0, 0)) {
		if (__sync_bool_compare_and_swap(&state, 0, -2)) {
			int local_state = -1;
			int fd = sys_open_nocancel("/proc/self/auxv", BSD_O_RDONLY, 0);
			if (fd >= 0) {
				unsigned long long pair[2];
				while (sys_read_nocancel(fd, pair, sizeof(pair)) == sizeof(pair) && pair[0] != 0 /* AT_NULL */) {
					if (pair[0] == 16 /* AT_HWCAP */) {
						cached[0] = pair[1];
						local_state = 1;
					} else if (pair[0] == 26 /* AT_HWCAP2 */) {
						cached[1] = pair[1];
					}
				}
				close_internal(fd);
			}
			__sync_synchronize();
			state = local_state;
		} else {
			while (state == -2) {
				/* spin wait until initialization completes */
			}
		}
	}
	caps[0] = cached[0];
	caps[1] = cached[1];
	return state == 1;
}
#endif

static long feature_value(int index, void* old, unsigned long* oldlen, void* _new)
{
	if (!oldlen)
		return -EINVAL;
	if (_new)
		return -EPERM;
	sysctl_handle_size(sizeof(int));
	int value = 0;
#if defined(__aarch64__) || defined(__arm64__)
	int bit = hw_features[index].bit;
	unsigned long long caps[2];
	if (bit == HWCAP_ALWAYS)
		value = 1;
	else if (read_hwcaps(caps))
		value = (caps[bit / 64] >> (bit % 64)) & 1;
#endif
	*(int*) old = value;
	return 0;
}
