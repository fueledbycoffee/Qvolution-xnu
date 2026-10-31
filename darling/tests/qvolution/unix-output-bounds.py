#!/usr/bin/env python3
"""Original actual-source wrapper/copyout regression; synthetic kernel only."""
import argparse,pathlib,re,subprocess,tempfile,os
p=argparse.ArgumentParser();p.add_argument('--repo',type=pathlib.Path,default=pathlib.Path(__file__).resolve().parents[3]);p.add_argument('--baseline',metavar='REVISION');p.add_argument('--mode',default='getsockname');a=p.parse_args();repo=a.repo.resolve()
base=pathlib.Path('darling/src/libsystem_kernel/emulation');files=['src/xnu_syscall/bsd/helper/network/duct.c']+['src/xnu_syscall/bsd/impl/network/'+x+'.c' for x in ('getsockname','getpeername','recvfrom','accept')]
def read(f):return subprocess.check_output(['git','show',a.baseline+':'+str(f)],cwd=repo,text=True) if a.baseline else (repo/f).read_text()
head=read(base/'include/xnu_syscall/bsd/helper/network/duct.h')
source='\n'.join(re.sub(r'^#include[^\n]*\n','',read(base/f),flags=re.M) for f in files)
harness=r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#undef ENAMETOOLONG
#define ENAMETOOLONG 63
#define PF_LOCAL 1
#define LINUX_PF_LOCAL 1
#define F_GETFL 3
#define F_SETFL 4
#define BSD_O_NONBLOCK 4
#define CANCELATION_POINT() ((void)0)
#define __NR_getsockname probe_getsockname
#define __NR_getpeername probe_getpeername
#define __NR_recvfrom probe_recvfrom
#define __NR_accept4 probe_accept4
#define LINUX_SYSCALL(n,...) n(__VA_ARGS__)
struct vchroot_expand_args {char path[4096];unsigned flags;int dfd;};
struct vchroot_unexpand_args {char path[4096];};
#define VCHROOT_FOLLOW 1
static int failReverse,accepted,closed,flagCalls,nativeCalls,nativeOverride=-1;
static const int acceptedFD=73;
#define __simple_snprintf snprintf
static int dserver_rpc_vchroot_socket_root(char*p,uint64_t size,uint64_t*length,int32_t*pid,int32_t*fd){return -ENAMETOOLONG;}
static int get_perthread_wd(void){return -100;}
static int sfamily_bsd_to_linux(int f){return f;}
static int sfamily_linux_to_bsd(int f){return f;}
static int msgflags_bsd_to_linux(int f){return f;}
static int errno_linux_to_bsd(int e){return e;}
static int vchroot_expand(struct vchroot_expand_args*v){return 0;}
static int vchroot_unexpand(struct vchroot_unexpand_args*v){return failReverse?-ENAMETOOLONG:0;}
long sys_accept_nocancel(int,void*,int*);
long sys_recvfrom_nocancel(int,void*,unsigned long,int,void*,int*);
static long sys_close_nocancel(int fd){if(fd!=acceptedFD)abort();closed++;accepted=0;return 0;}
static long sys_fcntl_nocancel(int fd,int command,long value){flagCalls++;return command==F_GETFL?BSD_O_NONBLOCK:0;}
static const char name[]="/var/run/qv-original-output-socket-123456789";
static int kernelAddress(void*out,int*len){
 nativeCalls++;if(!out)return 0;if(!len)return -EFAULT;
 unsigned char bytes[512]={1,0};memcpy(bytes+2,name,sizeof name);int full=nativeOverride>=0?nativeOverride:2+sizeof name,capacity=*len;if(capacity<0)return -EINVAL;
 if(capacity>0){int n=capacity<full?capacity:full;if(n>sizeof bytes)n=sizeof bytes;memcpy(out,bytes,n);}*len=full;return 0;
}
static long probe_getsockname(int fd,void*out,int*len){return kernelAddress(out,len);}
static long probe_getpeername(int fd,void*out,int*len){return kernelAddress(out,len);}
static long probe_recvfrom(int fd,void*bytes,unsigned long capacity,int flags,void*out,int*len){int e=kernelAddress(out,len);if(e)return e;if(capacity)((char*)bytes)[0]='Q';return capacity?1:0;}
static long probe_accept4(int fd,void*out,int*len,int flags){int e=kernelAddress(out,len);if(e)return e;accepted=1;return acceptedFD;}
'''
tests=r'''
#define CHECK(c,m) do{if(!(c)){fprintf(stderr,"FAIL: %s line%d\n",m,__LINE__);return 1;}}while(0)
static long call(const char*mode,void*address,int*len){char byte=0;if(!strcmp(mode,"getsockname"))return sys_getsockname(5,address,len);if(!strcmp(mode,"getpeername"))return sys_getpeername(5,address,len);if(!strcmp(mode,"recvfrom"))return sys_recvfrom_nocancel(5,&byte,1,0,address,len);return sys_accept_nocancel(5,address,len);}
int main(int argc,char**argv){const char*mode=argc>1?argv[1]:"getsockname";
 if(!strcmp(mode,"reverse")){
  struct sockaddr_fixup input,output;
  for(int n=103;n<=105;n++){memset(&input,0,sizeof input);input.linux_family=LINUX_PF_LOCAL;memset(input.sun_path,'r',n);int r=sockaddr_fixup_from_linux(&output,&input,n+3);CHECK(n<=104?(r==106&&!memcmp(output.bsd_sun_path,input.sun_path,n)):r==-ENAMETOOLONG,"103/104/105-byte reverse pathname boundaries");}
  memset(&input,0,sizeof input);input.linux_family=LINUX_PF_LOCAL;CHECK(sockaddr_fixup_from_linux(&output,&input,2)==2&&output.bsd_length==2,"unnamed local socket has header only");puts("PASS: actual reverse boundary and unnamed source");return 0;
 }
 if(!strcmp(mode,"accept-error")){unsigned char address[128]={0};int len=sizeof address;failReverse=1;long r=call("accept",address,&len);CHECK(r==-ENAMETOOLONG,"conversion failure reaches caller");CHECK(closed==1&&!accepted&&flagCalls==0,"newly accepted descriptor closed exactly once on conversion error");puts("PASS: actual accept source closes only newly accepted FD on conversion error");return 0;}
 for(int cap=1;cap<=106;cap++){
  unsigned char storage[256];memset(storage,0xa5,sizeof storage);int len=cap;long r=call(mode,storage+8,&len);
  CHECK(r==(!strcmp(mode,"accept")?acceptedFD:!strcmp(mode,"recvfrom")?1:0),"return value preserved");
  for(int i=0;i<8;i++)CHECK(storage[i]==0xa5,"leading canary preserved");
  for(int i=8+cap;i<256;i++)CHECK(storage[i]==0xa5,"caller capacity respected, trailing canary preserved");
  CHECK(len==2+sizeof name,"reports full translated required length");CHECK(storage[8]==len&& (cap<2||storage[9]==1),"Darwin header");
  if(cap>2)CHECK(!memcmp(storage+10,name,cap-2<(int)sizeof name?cap-2:sizeof name),"correct bounded pathname prefix");
  if(accepted){accepted=0;closed=0;}
 }
 {int len=0;long r=call(mode,NULL,&len);CHECK(r==(!strcmp(mode,"accept")?acceptedFD:!strcmp(mode,"recvfrom")?1:0),"null address behavior/zero capacity");if(!strcmp(mode,"getsockname")||!strcmp(mode,"getpeername"))CHECK(len==2+sizeof name,"zero-capacity length query");}
 accepted=0;closed=0;
 {int before=nativeCalls;unsigned char address[8];CHECK(call(mode,address,NULL)==-EFAULT&&nativeCalls==before,"nonnull address/null length rejected before syscall");}
 {unsigned char address[128];memset(address,0xa5,sizeof address);int len=sizeof address;nativeOverride=513;CHECK(call(mode,address,&len)==-EINVAL,"oversized native reported length rejected");for(unsigned i=0;i<sizeof address;i++)CHECK(address[i]==0xa5,"native overflow rejection preserves guest bytes");if(!strcmp(mode,"accept"))CHECK(closed==1&&!accepted,"oversized native address closes newly accepted FD");nativeOverride=-1;accepted=0;closed=0;}
 if(!strcmp(mode,"recvfrom")){unsigned char address[8];memset(address,0xa5,sizeof address);int len=sizeof address;nativeOverride=0;CHECK(call(mode,address,&len)==1&&len==0,"zero source address preserves received byte result");for(unsigned i=0;i<sizeof address;i++)CHECK(address[i]==0xa5,"zero source writes no address bytes");nativeOverride=-1;}
 {int len=-1,before=nativeCalls;unsigned char address[8];long r=call(mode,address,&len);CHECK(r==-EINVAL&&nativeCalls==before,"negative capacity rejected before consuming accept/receive");}
 puts("PASS: actual wrapper/copyout source respects capacities and return values; synthetic kernel only");return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='qv-output-bounds-') as d:
 f=pathlib.Path(d)/'probe.c';binary=pathlib.Path(d)/'probe';f.write_text(harness+'\n'+head+'\n'+source+'\n'+tests)
 subprocess.run(['clang','-std=c11','-Wno-implicit-function-declaration','-fsanitize=address,undefined',str(f),'-o',str(binary)],check=True)
 subprocess.run([str(binary),a.mode],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
