#!/usr/bin/env python3
"""Actual conversion body + native kernel filesystem/socket test; synthetic RPC/vchroot."""
import pathlib,re,subprocess,tempfile,os
repo=pathlib.Path(__file__).resolve().parents[3];base=repo/'darling/src/libsystem_kernel/emulation'
body=re.sub(r'^#include[^\n]*\n','',(base/'src/xnu_syscall/bsd/helper/network/duct.c').read_text(),flags=re.M)
head=(base/'include/xnu_syscall/bsd/helper/network/duct.h').read_text()
harness=r'''
#define _GNU_SOURCE
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#undef ENAMETOOLONG
#define ENAMETOOLONG 63
#define LINUX_PF_LOCAL 1
#define VCHROOT_FOLLOW 1
#define __simple_snprintf snprintf
struct vchroot_expand_args{char path[4096];unsigned flags;int dfd;};
struct vchroot_unexpand_args{char path[4096];};
static char root[4096];static int rootFD,serverPID,symlinkMode;
static int sfamily_bsd_to_linux(int f){return f;}
static int sfamily_linux_to_bsd(int f){return f;}
static int errno_linux_to_bsd(int e){return e==-36?-63:e;}
static int get_perthread_wd(void){return -100;}
static int dserver_rpc_vchroot_socket_root(char*p,uint64_t cap,uint64_t*n,int32_t*pid,int32_t*fd){*n=strlen(root);if(*n>=cap)return -36;strcpy(p,root);*pid=serverPID;*fd=rootFD;return 0;}
static int vchroot_expand(struct vchroot_expand_args*v){char full[4096];if(strlen(root)+strlen(v->path)>=sizeof full)return -36;strcpy(full,root);if(symlinkMode&&!strncmp(v->path,"/var/run/link/",14)){strcat(full,"/elsewhere/");strcat(full,v->path+14);}else strcat(full,v->path);strcpy(v->path,full);return 0;}
static int vchroot_unexpand(struct vchroot_unexpand_args*v){size_t n=strlen(root);if(!strncmp(v->path,root,n)&&(v->path[n]=='/'||!v->path[n]))memmove(v->path,v->path+n,strlen(v->path+n)+1);else{memmove(v->path+19,v->path,strlen(v->path)+1);memcpy(v->path,"/Volumes/SystemRoot",19);}return 0;}
'''
tests=r'''
static int address(const char*path,struct sockaddr_fixup*out){struct sockaddr_fixup in={0};in.bsd_family=PF_LOCAL;assert(strlen(path)<=sizeof in.bsd_sun_path);memcpy(in.bsd_sun_path,path,strlen(path));in.bsd_length=2+strlen(path);return sockaddr_fixup_from_bsd(out,&in,in.bsd_length);}
static void reverse(const void*address,int n,const char*expected){union sockaddr_fixup_storage out;int len=sizeof out;assert(sockaddr_fixup_copyout(out.bytes,sizeof out,&len,address,n)==0);assert(out.address.bsd_family==PF_LOCAL&&!strcmp(out.address.bsd_sun_path,expected));}
static void edges(const void*native,int n,const char*otherRoot){
 struct sockaddr_fixup unknown={0},out;unknown.linux_family=AF_UNIX;strcpy(unknown.sun_path,"/proc/999999/fd/3/var/run/b");int r=sockaddr_fixup_from_linux(&out,&unknown,3+strlen(unknown.sun_path));assert(r>0&&!strncmp(out.bsd_sun_path,"/Volumes/SystemRoot/",20));
 snprintf(unknown.sun_path,sizeof unknown.sun_path,"/proc/%d/fd/%d/../escape",serverPID,rootFD);r=sockaddr_fixup_from_linux(&out,&unknown,3+strlen(unknown.sun_path));assert(r>0&&!strncmp(out.bsd_sun_path,"/Volumes/SystemRoot/",20));
 char actualRoot[4096];strcpy(actualRoot,root);int savedFD=rootFD;strcpy(root,otherRoot);rootFD=open(root,O_RDONLY|O_DIRECTORY|O_CLOEXEC);assert(rootFD>=0);r=sockaddr_fixup_from_linux(&out,native,n);assert(r<0||strcmp(out.bsd_sun_path,"/var/run/b"));close(rootFD);rootFD=savedFD;strcpy(root,actualRoot); // Other root cannot reinterpret this alias as its own guest endpoint.
 snprintf(unknown.sun_path,sizeof unknown.sun_path,"/proc/%d/fd/%d/var/run/link/a",serverPID,rootFD);symlinkMode=1;r=sockaddr_fixup_from_linux(&out,&unknown,3+strlen(unknown.sun_path));symlinkMode=0;assert(r>0&&!strncmp(out.bsd_sun_path,"/Volumes/SystemRoot/",20)); // A controlled symlink-resolution mismatch is never promoted to guest identity.
 char huge[105];memset(huge,'z',104);huge[0]='/';huge[104]=0;assert(address(huge,&out)==-ENAMETOOLONG); // Remaining alias spelling cannot fit: honest failure, no shortened endpoint.
 }
int main(int argc,char**argv){assert(argc==3&&strlen(argv[1])<sizeof root);strcpy(root,argv[1]);char self[32]={0};assert(readlink("/proc/self",self,sizeof self-1)>0);serverPID=atoi(self);assert(serverPID>0);rootFD=open(root,O_RDONLY|O_DIRECTORY|O_CLOEXEC);assert(rootFD>=0);
 struct sockaddr_fixup a,b;int alen=address("/var/run/a",&a),blen=address("/var/run/b",&b);assert(alen>0&&blen>0&&strcmp(a.sun_path,b.sun_path));assert(!strncmp(a.sun_path,"/proc/",6));if(getenv("QV_MAPPING_SOURCE_ONLY")){reverse(&a,alen,"/var/run/a");reverse(&b,blen,"/var/run/b");edges(&b,blen,argv[2]);close(rootFD);puts("PASS: actual conversion alias edges with synthetic RPC/vchroot; no kernel transport run");return 0;}
 int receiver=socket(AF_UNIX,SOCK_DGRAM,0);int bound=receiver>=0?bind(receiver,(struct sockaddr*)&a,alen):-1;if(bound<0)fprintf(stderr,"native socket=%d bind=%d errno=%d alias=%s len=%d\n",receiver,bound,errno,a.sun_path,alen);assert(receiver>=0&&bound==0);
 char original[4096];strcpy(original,root);strcat(original,"/var/run/a");struct stat st;assert(lstat(original,&st)==0&&S_ISSOCK(st.st_mode)&&chmod(original,0600)==0);
 union sockaddr_fixup_storage native;unsigned n=sizeof native;assert(getsockname(receiver,(struct sockaddr*)native.bytes,&n)==0);reverse(native.bytes,n,"/var/run/a");
 unsigned char small[20];memset(small,0xa5,sizeof small);int reported=1;assert(sockaddr_fixup_copyout(small,1,&reported,native.bytes,n)==0&&reported>1);for(int i=1;i<20;i++)assert(small[i]==0xa5);
 pid_t child=fork();assert(child>=0);if(!child){close(receiver);close(rootFD);int fills[8];for(int i=0;i<8;i++)fills[i]=open("/dev/null",O_RDONLY);int sender=socket(AF_UNIX,SOCK_DGRAM,0);assert(sender>=0&&bind(sender,(struct sockaddr*)&b,blen)==0);assert(sendto(sender,"Q",1,0,(struct sockaddr*)&a,alen)==1);close(sender);for(int i=0;i<8;i++)close(fills[i]);_exit(0);}
 int status;assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);char byte=0;n=sizeof native;assert(recvfrom(receiver,&byte,1,0,(struct sockaddr*)native.bytes,&n)==1&&byte=='Q');reverse(native.bytes,n,"/var/run/b"); // Sender exited; the root descriptor belongs to the holder.
 edges(native.bytes,n,argv[2]);
 close(receiver);strcpy(original,root);strcat(original,"/var/run/a");unlink(original);strcpy(original,root);strcat(original,"/var/run/b");unlink(original);close(rootFD);
 puts("PASS: actual conversion/native kernel long-root nodes, tiny copyout, distinct endpoints, post-sender-exit reverse names; synthetic RPC/vchroot only");
}
'''
with tempfile.TemporaryDirectory(prefix='qv-unix-mapping-') as d, tempfile.TemporaryDirectory(prefix='qv-root-'+100*'r') as own, tempfile.TemporaryDirectory(prefix='qv-other-root-') as other:
 root=pathlib.Path(own);(root/'var/run').mkdir(parents=True);f=pathlib.Path(d)/'mapping.c';binary=pathlib.Path(d)/'mapping';f.write_text(harness+'\n'+head+'\n'+body+'\n'+tests)
 subprocess.run(['clang','-std=c11','-fsanitize=address,undefined',str(f),'-o',str(binary)],check=True)
 subprocess.run([str(binary),str(root),other],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
