// Original real guest UNIX pathname and lifecycle contract. No host APIs or stubs.
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#define CHECK(c,m) do{if(!(c)){fprintf(stderr,"FAIL: %s errno=%d\n",m,errno);result=1;goto done;}}while(0)
static int addr(const char*p,struct sockaddr_un*a){memset(a,0,sizeof *a);a->sun_family=AF_UNIX;if(strlen(p)>=sizeof a->sun_path)return -1;strcpy(a->sun_path,p);a->sun_len=SUN_LEN(a);return 0;}
static int bound(const char*p,int type){struct sockaddr_un a;if(addr(p,&a))return -1;int fd=socket(AF_UNIX,type,0);if(fd<0)return -1;if(bind(fd,(struct sockaddr*)&a,a.sun_len)<0){close(fd);return -1;}return fd;}
static int exact(int fd,int peer,const char*path){struct{struct sockaddr_un a;unsigned char tail[32];} out;memset(&out,0xa5,sizeof out);socklen_t n=sizeof out.a;int r=peer?getpeername(fd,(struct sockaddr*)&out.a,&n):getsockname(fd,(struct sockaddr*)&out.a,&n);if(r||out.a.sun_family!=AF_UNIX||strcmp(out.a.sun_path,path))return 0;for(unsigned i=0;i<sizeof out.tail;i++)if(out.tail[i]!=0xa5)return 0;return 1;}
int main(void){int result=0,r=-1,l=-1,c=-1,a=-1,s=-1,status=0;pid_t child=-1;char directory[]="/var/run/qvolution-root-alias-original-XXXXXX",paths[7][150]={{0}};int owned[7]={0};
 CHECK(mkdtemp(directory),"own directory");const char*names[]={"r","s","l","c","real","link","real/n"};for(int i=0;i<7;i++)snprintf(paths[i],sizeof paths[i],"%s/%s",directory,names[i]);
 r=bound(paths[0],SOCK_DGRAM);CHECK(r>=0,"long-prefix real datagram bind");owned[0]=1;CHECK(exact(r,0,paths[0]),"getsockname returns exact guest path");
 {struct stat st;CHECK(lstat(paths[0],&st)==0&&S_ISSOCK(st.st_mode)&&chmod(paths[0],0600)==0,"original guest socket node/chmod");}
 child=fork();CHECK(child>=0,"fork original sender");if(!child){close(r);int fillers[8];for(int i=0;i<8;i++)fillers[i]=open("/dev/null",O_RDONLY);int sender=bound(paths[1],SOCK_DGRAM);if(sender<0)_exit(3);struct sockaddr_un destination;if(addr(paths[0],&destination))_exit(4);int ok=sendto(sender,"Q",1,0,(struct sockaddr*)&destination,destination.sun_len)==1;close(sender);for(int i=0;i<8;i++)if(fillers[i]>=0)close(fillers[i]);_exit(ok?0:5);}
 CHECK(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0,"sender exits after successful send");child=-1;owned[1]=1;
 {struct{struct sockaddr_un a;unsigned char tail[32];} peer;memset(&peer,0xa5,sizeof peer);socklen_t n=sizeof peer.a;char byte=0;CHECK(recvfrom(r,&byte,1,0,(struct sockaddr*)&peer.a,&n)==1&&byte=='Q',"real datagram after sender exit");CHECK(peer.a.sun_family==AF_UNIX&&!strcmp(peer.a.sun_path,paths[1]),"post-sender-exit source guest name");for(unsigned i=0;i<sizeof peer.tail;i++)CHECK(peer.tail[i]==0xa5,"source address canary");}
 l=bound(paths[2],SOCK_STREAM);CHECK(l>=0&&listen(l,2)==0,"real listener");owned[2]=1;c=bound(paths[3],SOCK_STREAM);CHECK(c>=0,"real bound client");owned[3]=1;{struct sockaddr_un destination;CHECK(addr(paths[2],&destination)==0&&connect(c,(struct sockaddr*)&destination,destination.sun_len)==0,"real stream connect");}
 CHECK(exact(c,1,paths[2]),"getpeername returns guest listener path");
 {struct sockaddr_un peer;socklen_t n=sizeof peer;a=accept(l,(struct sockaddr*)&peer,&n);CHECK(a>=0&&peer.sun_family==AF_UNIX&&!strcmp(peer.sun_path,paths[3]),"accept returns exact guest client path");}
 {struct{unsigned char a[1],tail[32];} tiny;memset(&tiny,0xa5,sizeof tiny);socklen_t n=1;CHECK(getsockname(r,(struct sockaddr*)tiny.a,&n)==0&&n>1,"tiny capacity reports full guest length");for(unsigned i=0;i<sizeof tiny.tail;i++)CHECK(tiny.tail[i]==0xa5,"tiny alias-output canary");}
 CHECK(mkdir(paths[4],0700)==0,"own symlink target directory");owned[4]=1;CHECK(symlink("real",paths[5])==0,"own relative directory symlink");owned[5]=1;
 {char via[150];snprintf(via,sizeof via,"%s/n",paths[5]);s=bound(via,SOCK_DGRAM);CHECK(s>=0,"symlinked guest parent binds canonical node");owned[6]=1;CHECK(exact(s,0,paths[6]),"existing canonical symlink policy preserved");}
 puts("PASS: actual guest long-prefix UNIX bind/query/connect/accept, post-sender-exit datagram names, FD-layout difference and symlink/copyout");
done:
 if(child>0)waitpid(child,&status,0);if(s>=0)close(s);if(a>=0)close(a);if(c>=0)close(c);if(l>=0)close(l);if(r>=0)close(r);
 for(int i=6;i>=0;i--)if(owned[i]){if(i==4)rmdir(paths[i]);else unlink(paths[i]);}rmdir(directory);return result;
}
