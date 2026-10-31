/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Exact owned fork/helper bodies with a sibling server writing one authored int.
 * No foreign process, image, code or returned address is inspected or printed. */
#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>
static int request_pipe[2],reply_pipe[2];
static long result(long value) { return value<0?-errno:value; }
#define LINUX_SYSCALL(number,...) result(syscall(number,##__VA_ARGS__))
static long errno_linux_to_bsd(long value) { return value; }
typedef struct { void (*close)(int); } guard_entry_options_t;
enum { guard_flag_prevent_close=1,guard_flag_close_on_fork=2 };
static int get_perthread_wd(void) { return -1; }
static void guard_table_postfork_child(void) {}
static void __dserver_per_thread_socket_refresh(void) {}
static int __dserver_process_lifetime_pipe_refresh(void) { return -1; }
static int __dserver_per_thread_socket(void) { return -1; }
static int __dserver_get_process_lifetime_pipe(void) { return -1; }
static void __dserver_close_socket(int fd) { (void)fd; }
static void __dserver_close_process_lifetime_pipe(int fd) { (void)fd; }
static void guard_table_add(int fd,int flags,const guard_entry_options_t *options) { (void)fd;(void)flags;(void)options; }
static int sys_fchdir(int fd) { (void)fd;return 0; }
static void fail(const char *label) { fprintf(stderr,"FAIL: %s\n",label);exit(1); }
static void transfer(int fd,void *p,size_t length,int writing)
{
    char *bytes=p;
    while(length) {
        ssize_t n=writing?write(fd,bytes,length):read(fd,bytes,length);
        if(n<0&&errno==EINTR)continue;
        if(n<=0)fail("own control pipe");bytes+=n;length-=n;
    }
}
struct request { pid_t target;uintptr_t destination; };
static int dserver_rpc_checkin(int forked,void *stack,int lifetime)
{
    (void)forked;(void)stack;(void)lifetime;
    int owned_value=0;
    struct request request={getpid(),(uintptr_t)&owned_value};int reply;
    transfer(request_pipe[1],&request,sizeof(request),1);
    transfer(reply_pipe[0],&reply,sizeof(reply),0);
    if(reply||owned_value!=0x12345678)fail("sibling server cannot write authored child buffer");
    return 0;
}
#define __simple_printf printf
#define __simple_abort abort
/* PRODUCTION_HELPER */
/* PRODUCTION_FORK */
int main(void)
{
    int self_value=0,value=0x12345678;
    struct iovec self_local={&value,sizeof(value)},self_remote={&self_value,sizeof(self_value)};
    if(process_vm_writev(getpid(),&self_local,1,&self_remote,1,0)!=sizeof(value)||self_value!=value) {
        fprintf(stderr,"UNQUALIFIED: own process_vm_writev unavailable errno=%d\n",errno);return 77;
    }
    FILE *scope=fopen("/proc/sys/kernel/yama/ptrace_scope","r");int mode=-1;
    if(!scope||fscanf(scope,"%d",&mode)!=1||mode!=1) {
        if(scope)fclose(scope);puts("UNQUALIFIED: requires unchanged Yama relational mode");return 77;
    }
    fclose(scope);
    if(pipe(request_pipe)||pipe(reply_pipe))fail("own pipes");
    pid_t server=fork();if(server<0)fail("own sibling server fork");
    if(server==0) {
        struct request request;transfer(request_pipe[0],&request,sizeof(request),0);
        int value=0x12345678;struct iovec local={&value,sizeof(value)},remote={(void *)request.destination,sizeof(value)};
        int reply=process_vm_writev(request.target,&local,1,&remote,1,0)==sizeof(value)?0:errno;
        transfer(reply_pipe[1],&reply,sizeof(reply),1);_exit(0);
    }
    long child=sys_fork();if(child<0)fail("production raw fork");if(child==0)_exit(0);
    int child_status,server_status;
    if(waitpid(child,&child_status,0)!=child||waitpid(server,&server_status,0)!=server)fail("own children reaped");
    if(!WIFEXITED(child_status)||WEXITSTATUS(child_status)||!WIFEXITED(server_status)||WEXITSTATUS(server_status))fail("production raw child server-access setup");
    puts("PASS: production raw fork restores existing process-local server access");
    return 0;
}
