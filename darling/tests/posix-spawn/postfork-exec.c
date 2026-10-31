/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Run against a separately built, owned child that exits zero. */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
extern char **environ;
static void check(int value,const char *label)
{ if(!value) { fprintf(stderr,"FAIL: %s\n",label);exit(1); } }
static long long milliseconds(void)
{ struct timespec t;check(clock_gettime(CLOCK_MONOTONIC,&t)==0,"monotonic clock");return (long long)t.tv_sec*1000+t.tv_nsec/1000000; }
static void collect(pid_t pid)
{
    long long deadline=milliseconds()+5000;int status;
    for(;;) {
        pid_t result=waitpid(pid,&status,WNOHANG);
        if(result==pid) { check(WIFEXITED(status)&&WEXITSTATUS(status)==0,"owned helper exits zero");return; }
        check(result==0||(result<0&&errno==EINTR),"owned child wait");
        if(milliseconds()>=deadline) { kill(pid,SIGKILL);while(waitpid(pid,&status,0)<0&&errno==EINTR){}check(0,"owned helper deadline"); }
        usleep(1000);
    }
}
int main(int argc,char **argv)
{
    check(argc==2&&argv[1][0]=='/',"absolute owned helper argument");
    char *child[]={argv[1],NULL};pid_t pid=fork();check(pid>=0,"ordinary fork control");
    if(pid==0) {
        int fd=open("/dev/null",O_WRONLY);if(fd<0||dup2(fd,STDOUT_FILENO)<0)_exit(125);
        if(fd!=STDOUT_FILENO)close(fd);execve(child[0],child,environ);_exit(127);
    }
    collect(pid);
    for(int mode=0;mode<2;mode++) {
        posix_spawn_file_actions_t actions;posix_spawnattr_t attributes;
        check(posix_spawn_file_actions_init(&actions)==0,"file actions init");
        check(posix_spawnattr_init(&attributes)==0,"attributes init");
        check(posix_spawn_file_actions_addopen(&actions,STDOUT_FILENO,"/dev/null",O_WRONLY,0)==0,"stdout discard action");
#ifdef POSIX_SPAWN_CLOEXEC_DEFAULT
        if(mode)check(posix_spawnattr_setflags(&attributes,POSIX_SPAWN_CLOEXEC_DEFAULT)==0,"close-on-exec default");
#endif
        pid=-1;int status=posix_spawn(&pid,child[0],&actions,&attributes,child,environ);
        posix_spawnattr_destroy(&attributes);posix_spawn_file_actions_destroy(&actions);
        check(status==0,"spawn works without full libSystem atfork handlers");check(pid>0,"successful child PID");collect(pid);
    }
    puts("PASS: ordinary fork and both spawn modes execute the same owned helper");
    return 0;
}
