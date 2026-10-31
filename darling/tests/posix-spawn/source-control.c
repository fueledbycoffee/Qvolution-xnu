/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Host control for the exact production sys_posix_spawn function body.
 * Only NULL attributes/actions are exercised; the declarations below are test
 * adapters, not a public Darwin ABI or a second spawn implementation. */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#define _POSIX_SPAWN_DISABLE_ASLR 0x0100
#define LINUX_ADDR_NO_RANDOMIZE 0x40000
#define LINUX_O_CLOEXEC O_CLOEXEC
#define BSD_O_RDONLY O_RDONLY
#define POSIX_SPAWN_SETEXEC 0x0400
#define POSIX_SPAWN_START_SUSPENDED 0x0080
#define POSIX_SPAWN_CLOEXEC_DEFAULT 0x4000
#define POSIX_SPAWN_RESETIDS 0x0001
#define POSIX_SPAWN_SETPGROUP 0x0002
#define POSIX_SPAWN_SETSIGMASK 0x0008
enum { XNU_PSFA_CLOSE, XNU_PSFA_DUP2, XNU_PSFA_OPEN,
       XNU_PSFA_CHDIR, XNU_PSFA_FCHDIR, XNU_PSFA_INHERIT,
       XNU_PSFA_FILEPORT_DUP2 };
struct linux_dirent64 { uint64_t ino;int64_t off;unsigned short d_reclen;unsigned char type;char d_name[]; };
struct xnu__psfa_action {
    int psfaa_type,psfaa_filedes;
    struct { int psfad_newfiledes; } psfaa_dup2args;
    struct { char *psfao_path;int psfao_oflag,psfao_mode; } psfaa_openargs;
    struct { char *psfac_path; } psfaa_chdirargs;
};
struct test_attributes { int psa_flags,psa_pgroup,psa_sigmask,psa_binprefs[4]; };
struct test_actions { int psfa_act_count;struct xnu__psfa_action *psfa_act_acts; };
struct _posix_spawn_args_desc { struct test_attributes *attrp;struct test_actions *factp; };
typedef int _libkernel_functions_t;
_libkernel_functions_t _libkernel_functions;
static int fail_fork,interrupt_read;
static long result(long value) { return value<0?-errno:value; }
#define LINUX_SYSCALL(number,...) result(syscall(number,__VA_ARGS__))
static long sys_fork(void) { return fail_fork?-EAGAIN:result(fork()); }
static long sys_read(int fd,void *p,size_t n)
{ if(interrupt_read) { interrupt_read=0;return -EINTR; } return result(read(fd,p,n)); }
static long sys_write(int fd,const void *p,size_t n) { return result(write(fd,p,n)); }
static long close_internal(int fd) { return result(close(fd)); }
static void sys_exit(int code) { _exit(code); }
static long sys_execve(const char *p,char **argv,char **env) { return result(execve(p,argv,env)); }
static __attribute__((unused)) long sys_wait4_nocancel(int pid,int *status,int options,void *usage)
{ return result(wait4(pid,status,options,usage)); }
static long errno_linux_to_bsd(long code) { return code; }
static int mach_driver_get_fd(void) { return -100; }
static long sys_open(const char *p,int flags,int mode) { return result(open(p,flags,mode)); }
static long sys_fcntl(int fd,int cmd,long arg) { return result(fcntl(fd,cmd,arg)); }
static long sys_dup(int fd) { return result(dup(fd)); }
static long sys_dup2(int from,int to) { return result(dup2(from,to)); }
static long sys_getuid(void) { return getuid(); }
static long sys_getgid(void) { return getgid(); }
static long sys_setuid(int uid) { return result(setuid(uid)); }
static long sys_setgid(int gid) { return result(setgid(gid)); }
static long sys_setpgid(int pid,int group) { return result(setpgid(pid,group)); }
static long sys_sigprocmask(int how,const int *set,void *old) { (void)how;(void)set;(void)old;return -ENOSYS; }
static long sys_chdir(const char *path) { return result(chdir(path)); }
static long sys_fchdir(int fd) { return result(fchdir(fd)); }
static int dserver_rpc_stop_after_exec(void) { return -ENOSYS; }
#define __simple_atoi(s,end) strtol(s,end,10)
#define __simple_sprintf sprintf
#define __simple_printf printf
#define __simple_abort abort

/* The runner inserts the exact production function here. */
/* PRODUCTION_FUNCTION */

static void check(int condition,const char *label)
{ if(!condition) { fprintf(stderr,"FAIL: %s\n",label);exit(1); } }
static int descriptor_count(void)
{
    DIR *directory=opendir("/proc/self/fd");check(directory!=NULL,"own fd directory");
    int count=0;while(readdir(directory))count++;closedir(directory);return count;
}
extern char **environ;
int main(int argc,char **argv)
{
    check(argc==2,"own child path argument");
    for(int interrupted=0;interrupted<2;interrupted++) {
        int pid=-77;char *missing[]={"/nonexistent/qvolution-source-control",NULL};
        interrupt_read=interrupted;
        long status=sys_posix_spawn(&pid,missing[0],NULL,missing,environ);
        check(status==-ENOENT,"reported child exec failure remains an error");
        check(pid==-77,"failure does not publish child PID");
        int child_status;errno=0;
        check(waitpid(-1,&child_status,WNOHANG)==-1&&errno==ECHILD,"failed child reaped before returning");
    }
    int before=descriptor_count(),pid=-77;char *child[]={argv[1],NULL};fail_fork=1;
    check(sys_posix_spawn(&pid,child[0],NULL,child,environ)==-EAGAIN,"fork failure propagated");
    check(pid==-77&&descriptor_count()==before,"fork failure keeps PID and closes internal pipe");fail_fork=0;
    check(sys_posix_spawn(&pid,child[0],NULL,child,environ)==0&&pid>0,"successful exec publishes child");
    int status;check(waitpid(pid,&status,0)==pid&&WIFEXITED(status)&&WEXITSTATUS(status)==37,"successful child remains caller-owned");
    puts("PASS: production spawn failure/reap/PID/interrupt/fd ownership");
    return 0;
}
