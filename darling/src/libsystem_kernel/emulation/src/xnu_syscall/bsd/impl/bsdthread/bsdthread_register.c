#include <darling/emulation/xnu_syscall/bsd/impl/bsdthread/bsdthread_register.h>

#include <sys/errno.h>
#include <stdint.h>
#include <pthread/tsd_private.h>

#include <darling/emulation/common/base.h>
#include <darling/emulation/conversion/errno.h>
#include <darling/emulation/linux_premigration/signal/sigexc.h>
#include <darling/emulation/linux_premigration/linux-syscalls/linux.h>
#include <darling/emulation/common/simple.h>

#include <darlingserver/rpc.h>

int pthread_obj_size;
bsdthread_entry_t pthread_entry_point;
bsdwqthread_entry_t wqueue_entry_point;

#define PTHREAD_FEATURE_DISPATCHFUNC 1
#define PTHREAD_FEATURE_QOS_MAINTENANCE 0x10
#define PTHREAD_FEATURE_QOS_DEFAULT 0x40000000
#define WORKQ_FEATURE_FINEPRIO 0x02
#define WORKQ_FEATURE_KEVENT 0x40

struct _pthread_registration_data {
	uint64_t version;

	uint64_t dispatch_queue_offset;
	uint64_t main_qos;
	uint32_t tsd_offset;
	uint32_t return_to_kernel_offset;
	uint32_t mach_thread_self_offset;
} __attribute__ ((packed));

static struct _pthread_registration_data g_pth_regdata;

long sys_bsdthread_register(void* thread_start, void* wqthread, int pthsize,
		const struct _pthread_registration_data* pth_regdata,
		unsigned long pth_regdata_len, unsigned long long dpq_offset)
{
	pthread_obj_size = pthsize;
	pthread_entry_point = (bsdthread_entry_t) thread_start;
	wqueue_entry_point = (bsdwqthread_entry_t) wqthread;
	g_pth_regdata = *pth_regdata;

	uintptr_t pthread_handle = _pthread_getspecific_direct(_PTHREAD_TSD_SLOT_PTHREAD_SELF);
	uintptr_t dispatch_qaddr = pthread_handle + g_pth_regdata.tsd_offset + g_pth_regdata.dispatch_queue_offset;
	if (dserver_rpc_set_thread_handles(pthread_handle, dispatch_qaddr) < 0) {
		__simple_abort();
	}

	return /* WORKQ_FEATURE_KEVENT | WORKQ_FEATURE_FINEPRIO | PTHREAD_FEATURE_QOS_MAINTENANCE 
		| PTHREAD_FEATURE_DISPATCHFUNC | PTHREAD_FEATURE_QOS_DEFAULT */ 0;
}

// Before you make any changes to the assembly code, I strongly recommend looking at the `_thread_start`
// code. `_thread_start` does not follow the usual calling conventions you would expect. You can find the
// source in `libpthread/src/pthread_asm.S`.
void pthread_entry_point_wrapper(void* self, int thread_port, void* funptr,
		void* funarg, unsigned long stack_addr, unsigned int flags)
{
	sigexc_thread_setup();

	uintptr_t pthread_handle = (unsigned long) self;
	uintptr_t dispatch_qaddr = pthread_handle + g_pth_regdata.tsd_offset + g_pth_regdata.dispatch_queue_offset;
	if (dserver_rpc_set_thread_handles(pthread_handle, dispatch_qaddr) < 0) {
		__simple_abort();
	}

	// No additional function calls should occur beyond this point. Otherwise, we will risk our
	// registers being call-clobbered. I recommend reading the following doc for more details:
	// https://gcc.gnu.org/onlinedocs/gcc/Local-Register-Variables.html
#if __x86_64__
	register void*         arg1 asm("rdi") = self;
	register int           arg2 asm("esi") = thread_port;
	register void*         arg3 asm("rdx") = funptr;
	register void*         arg4 asm("rcx") = funarg;
	register unsigned long arg5 asm("r8")  = stack_addr;
	register unsigned int  arg6 asm("r9d") = flags;
#elif __i386__
	register void*         arg1 asm("eax") = self;
	register int           arg2 asm("ebx") = thread_port;
	register void*         arg3 asm("ecx") = funptr;
	register void*         arg4 asm("edx") = funarg;
	register unsigned long arg5 asm("edi") = stack_addr;
	register unsigned int  arg6 asm("esi") = flags;
#elif defined(__aarch64__) || defined(__arm64__)
	register void*         arg1 asm("x0") = self;
	register int           arg2 asm("w1") = thread_port;
	register void*         arg3 asm("x2") = funptr;
	register void*         arg4 asm("x3") = funarg;
	register unsigned long arg5 asm("x4") = stack_addr;
	register unsigned int  arg6 asm("w5") = flags;
#endif

#if __x86_64__
	__asm__ __volatile__ (
		// `_thread_start` does not expect the stack to be aligned (using the
		// call instruction will add the return address to the stack, causing
		// it to be aligned).
		"jmpq *%[pthread_entry_point]\n"
		::
		// Arguments follow the usual x86_64 calling conventions.
		"r"(arg1),"r"(arg2),"r"(arg3),"r"(arg4),"r"(arg5),"r"(arg6),
		[pthread_entry_point] "r"(pthread_entry_point)
	);
#elif __i386__
	__asm__ __volatile__ (
		// `_thread_start` does not expect the stack to be aligned.
		"jmpl *%[pthread_entry_point]\n"
		::
		// `_thread_start` does not follow the i386 calling conventions
		// for arguments. Instead of storing the arguments into the stack,
		// the arguments are stored in the registers.
		"r"(arg1),"r"(arg2),"r"(arg3),"r"(arg4),"r"(arg5),"r"(arg6),
		[pthread_entry_point] "rmi"(pthread_entry_point)
	);
#elif defined(__aarch64__) || defined(__arm64__)
	__asm__ __volatile__ (
		"br %[pthread_entry_point]\n"
		::
		"r"(arg1),"r"(arg2),"r"(arg3),"r"(arg4),"r"(arg5),"r"(arg6),
		[pthread_entry_point] "r"(pthread_entry_point)
	);
#else
	#error "Missing assembly for architecture"
	// pthread_entry_point(self, thread_port, funptr, funarg, stack_addr, flags);
#endif

	// `_thread_start` actually does actually return, but since we are jumping to 
	// (instead of calling) `_thread_start`, `_thread_start` will return to the 
	// method that called this method.
	__builtin_unreachable();
}

void wqueue_entry_point_wrapper(void* self, int thread_port, void* stackaddr,
		void* item, int reuse, int nevents)
{
	sigexc_thread_setup();
	wqueue_entry_point_asm_jump(self, thread_port, stackaddr, item, reuse, nevents);
}

// Before you make any changes to the assembly code, I strongly recommend looking at the `_start_wqthread`
// code. `_start_wqthread` does not follow the usual calling conventions you would expect. You can find the
// source in `libpthread/src/pthread_asm.S`.
void wqueue_entry_point_asm_jump(void* self, int thread_port, void* stackaddr,
		void* item, int reuse, int nevents) {

	// Every (re)entry into `_start_wqthread` starts on a fresh stack. XNU's workqueue sets
	// the stack pointer to the thread's stack top on each entry (libpthread
	// kern/kern_support.c, workq_thread_get_addrs: `self` lies immediately above the
	// stack and stack_top = self & -C_WORKQ_STK_ALIGN; workq_set_register_state:
	// rsp/esp = stack_top). `_start_wqthread` builds its frame from the stack pointer it
	// is given, so jumping to it with the caller's stack pointer keeps every frame from
	// the previous work item down to workq_kernreturn: about 0.6 KiB per reuse on x86_64,
	// which overflows the 8 MiB workqueue stack after some 14,000 reuses. Darling
	// allocates workqueue threads the same way (mldr dthread_structure_allocate: the
	// dthread sits right above the stack), so `self` aligned down is the top. Nothing
	// returns into the abandoned frames: a terminating thread leaves through
	// __darling_thread_terminate's longjmp to the host stack.
	const uintptr_t stack_top = (uintptr_t)self & ~(uintptr_t)15;

	// No additional function calls should occur beyond this point. Otherwise, we will risk our
	// registers being call-clobbered. I recommend reading the following doc for more details:
	// https://gcc.gnu.org/onlinedocs/gcc/Local-Register-Variables.html
#if __i386__
	// All six general registers carry arguments, so the entry point travels on the new
	// stack: store it just below the top (dead space of the previous incarnation, unused
	// padding on first entry) and `ret` into it, which leaves esp at the top.
	((uintptr_t*)stack_top)[-1] = (uintptr_t)wqueue_entry_point;
	uintptr_t entry_sp = stack_top - sizeof(uintptr_t);
#endif
#if __x86_64__
	register uintptr_t top asm("r10") = stack_top;
	register void* entry asm("r11") = (void*)wqueue_entry_point;
	register void* arg1 asm("rdi") = self;
	register int   arg2 asm("esi") = thread_port;
	register void* arg3 asm("rdx") = stackaddr;
	register void* arg4 asm("rcx") = item;
	register int   arg5 asm("r8d") = reuse;
	register int   arg6 asm("r9d") = nevents;
#elif __i386__
	register void* arg1 asm("eax") = self;
	register int   arg2 asm("ebx") = thread_port;
	register void* arg3 asm("ecx") = stackaddr;
	register void* arg4 asm("edx") = item;
	register int   arg5 asm("edi") = reuse;
	register int   arg6 asm("esi") = nevents;
#elif defined(__aarch64__) || defined(__arm64__)
	register void* arg1 asm("x0") = self;
	register int   arg2 asm("w1") = thread_port;
	register void* arg3 asm("x2") = stackaddr;
	register void* arg4 asm("x3") = item;
	register int   arg5 asm("w4") = reuse;
	register int   arg6 asm("w5") = nevents;
#endif

#if __x86_64__
	__asm__ __volatile__ (
		// Discard the previous frames: start at the stack top, as XNU does, with no
		// frame chain. `_start_wqthread` pushes its own sentinel and aligns itself.
		"movq %[top], %%rsp\n"
		"xorq %%rbp, %%rbp\n"
		"jmpq *%[entry]\n"
		::
		// Arguments follow the usual x86_64 calling conventions.
		"r"(arg1),"r"(arg2),"r"(arg3),"r"(arg4),"r"(arg5),"r"(arg6),
		[top] "r"(top), [entry] "r"(entry)
		: "memory"
	);
#elif __i386__
	__asm__ __volatile__ (
		// Discard the previous frames: esp = top - 4 holds the entry point, and `ret`
		// jumps there with esp at the stack top, as XNU sets it.
		// `_start_wqthread` does not follow the i386 calling conventions
		// for arguments. Instead of storing the arguments into the stack,
		// the arguments are stored in the registers.
		"movl %[entry_sp], %%esp\n"
		"xorl %%ebp, %%ebp\n"
		"ret\n"
		::
		"r"(arg1),"r"(arg2),"r"(arg3),"r"(arg4),"r"(arg5),"r"(arg6),
		[entry_sp] "m"(entry_sp)
		: "memory"
	);
#elif defined(__aarch64__) || defined(__arm64__)
	__asm__ __volatile__ (
		// Workqueue threads are allocated with `self` immediately above
		// the stack. Re-entry must discard the previous workqueue frames;
		// stackaddr (x2) is the bottom, not the top, of that stack.
		// Save the target before clearing FP/LR in case the compiler used
		// either register for the input operand.
		"mov x16, %[wqueue_entry_point]\n"
		"mov x29, xzr\n"
		"mov x30, xzr\n"
		"mov sp, %[stack_top]\n"
		"br x16\n"
		::
		"r"(arg1),"r"(arg2),"r"(arg3),"r"(arg4),"r"(arg5),"r"(arg6),
		[stack_top] "r"(arg1),
		[wqueue_entry_point] "r"(wqueue_entry_point)
		: "memory", "x16"
	);
#else
	#error "Missing assembly for architecture"
	// wqueue_entry_point(self, thread_port, stackaddr, item, reuse, nevents);
#endif

	// _start_wqthread never returns
	__builtin_unreachable();
}
