#!/usr/bin/env python3
"""Compile the actual conversion body with explicit synthetic vchroot services.
No guest/runtime/kernel transport or procfs alias result is claimed.
Optional --source accepts an old exact duct.c for a failing baseline.
"""
import argparse,pathlib,re,subprocess,tempfile,os
p=argparse.ArgumentParser();p.add_argument('--source',type=pathlib.Path);a=p.parse_args()
repo=pathlib.Path(__file__).resolve().parents[3]
source=a.source or repo/'darling/src/libsystem_kernel/emulation/src/xnu_syscall/bsd/helper/network/duct.c'
header=repo/'darling/src/libsystem_kernel/emulation/include/xnu_syscall/bsd/helper/network/duct.h'
body=re.sub(r'^#include[^\n]*\n','',source.read_text(),flags=re.M)
harness=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>
#undef ENAMETOOLONG
#define ENAMETOOLONG 63
#define PF_LOCAL 1
#define LINUX_PF_LOCAL 1
struct vchroot_expand_args {char path[4096];unsigned flags;int dfd;};
struct vchroot_unexpand_args {char path[4096];};
#define VCHROOT_FOLLOW 1
static char prefix[4096], reversePrefix[4096];
static int expansions;
#define __simple_snprintf snprintf
static int dserver_rpc_vchroot_socket_root(char*p,uint64_t size,uint64_t*length,int32_t*pid,int32_t*fd){return -ENAMETOOLONG;}
static int get_perthread_wd(void){return -100;}
static int sfamily_bsd_to_linux(int f){return f==30?10:f;}
static int sfamily_linux_to_bsd(int f){return f==10?30:f;}
static int errno_linux_to_bsd(int e){return e;}
static int vchroot_expand(struct vchroot_expand_args *v){expansions++;size_t n=strlen(prefix);memmove(v->path+n,v->path,strlen(v->path)+1);memcpy(v->path,prefix,n);return 0;}
static int vchroot_unexpand(struct vchroot_unexpand_args *v){size_t n=strlen(reversePrefix);memmove(v->path+n,v->path,strlen(v->path)+1);memcpy(v->path,reversePrefix,n);return 0;}
'''
tests=r'''
#define CHECK(c,m) do{if(!(c)){fprintf(stderr,"FAIL: %s at line %d\n",m,__LINE__);return 1;}}while(0)
static void input(struct sockaddr_fixup *in,int len){memset(in,0,sizeof *in);in->bsd_family=PF_LOCAL;memset(in->bsd_sun_path,'s',len);}
int main(void){
 struct sockaddr_fixup in,out;
 memset(prefix,'p',68);prefix[68]=0;input(&in,47);
 int r=sockaddr_fixup_from_bsd(&out,&in,49);
 CHECK(r==-ENAMETOOLONG,"115-byte expansion rejected instead of silently selecting directory/other endpoint");
 for(int n=0;n<108;n++){memset(prefix,'p',n);prefix[n]=0;input(&in,1);r=sockaddr_fixup_from_bsd(&out,&in,3);CHECK((n+1<108)?(r==n+4&&strlen(out.sun_path)==(size_t)n+1):(r==-ENAMETOOLONG),"Linux expanded boundary");}
 prefix[0]=0;input(&in,103);r=sockaddr_fixup_from_bsd(&out,&in,105);CHECK(r==106&&strlen(out.sun_path)==103,"SUN_LEN excludes NUL,103byte guest name retained");
 input(&in,104);r=sockaddr_fixup_from_bsd(&out,&in,106);CHECK(r==107&&strlen(out.sun_path)==104,"length-bounded104byte non-NUL guest name retained");
 input(&in,103);r=sockaddr_fixup_from_bsd(&out,&in,106);CHECK(r==106&&strlen(out.sun_path)==103,"NUL included in provided length accepted");
 int bad[]={-1,0,1,513};unsigned char tiny=0;
 for(unsigned i=0;i<sizeof bad/sizeof *bad;i++){int before=expansions;CHECK(sockaddr_fixup_size_from_bsd(&tiny,bad[i])==sizeof out,"invalid size cannot request huge allocation/read family");CHECK(sockaddr_fixup_from_bsd(&out,&tiny,bad[i])==-EINVAL&&expansions==before,"invalid length rejected before source read/expansion");}
 input(&in,1);CHECK(sockaddr_fixup_from_bsd(&out,&in,107)==-EINVAL,"oversized Darwin local layout rejected");
 memset(&in,0x45,sizeof in);in.bsd_family=2;CHECK(sockaddr_fixup_from_bsd(&out,&in,16)==16&&out.linux_family==2&&!memcmp(out.sun_path,in.sun_path,14),"IPv4 bytes unchanged");
 puts("PASS: actual AF_LOCAL conversion length guards under ASan/UBSan; synthetic vchroot, no transport claim");return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='qv-unix-bounds-') as d:
 f=pathlib.Path(d)/'bounds.c';binary=pathlib.Path(d)/'bounds'
 f.write_text(harness+'\n'+header.read_text()+'\n'+body+'\n'+tests)
 subprocess.run(['clang','-std=c11','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(f),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True,env={**os.environ,"ASAN_OPTIONS":"detect_leaks=0"})
