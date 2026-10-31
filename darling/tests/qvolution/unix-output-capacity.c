// Original public POSIX socket contract; own pathname nodes only, no vendor code.
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
static int endpoint(const char*path,int type){int fd=socket(AF_UNIX,type,0);if(fd<0)return -1;struct sockaddr_un a={0};a.sun_family=AF_UNIX;if(strlen(path)>=sizeof a.sun_path){close(fd);return -1;}strcpy(a.sun_path,path);a.sun_len=SUN_LEN(&a);if(bind(fd,(struct sockaddr*)&a,a.sun_len)<0){close(fd);return -1;}return fd;}
static int connectTo(int fd,const char*path){struct sockaddr_un a={0};a.sun_family=AF_UNIX;strcpy(a.sun_path,path);a.sun_len=SUN_LEN(&a);return connect(fd,(struct sockaddr*)&a,a.sun_len);}
#define CHECK(c,m) do{if(!(c)){fprintf(stderr,"FAIL: %s errno=%d\n",m,errno);result=1;goto done;}}while(0)
int main(int argc,char**argv){
 int result=0,server=-1,client=-1,accepted=-1,owned[2]={0,0};char directory[]="/var/run/qvo-XXXXXX",paths[2][150]={{0}};const char*mode=argc>1?argv[1]:"getsockname";
 CHECK(!strcmp(mode,"getsockname")||!strcmp(mode,"getpeername")||!strcmp(mode,"recvfrom")||!strcmp(mode,"accept"),"valid mode");
 CHECK(mkdtemp(directory),"own diagnostic directory");snprintf(paths[0],sizeof paths[0],"%s/r",directory);snprintf(paths[1],sizeof paths[1],"%s/s",directory);
 int datagram=!strcmp(mode,"recvfrom");server=endpoint(paths[0],datagram?SOCK_DGRAM:SOCK_STREAM);CHECK(server>=0,"own bound server");owned[0]=1;
 if(strcmp(mode,"getsockname")){
  client=endpoint(paths[1],datagram?SOCK_DGRAM:SOCK_STREAM);CHECK(client>=0,"own bound client");owned[1]=1;
  if(datagram){struct sockaddr_un a={0};a.sun_family=AF_UNIX;strcpy(a.sun_path,paths[0]);a.sun_len=SUN_LEN(&a);CHECK(sendto(client,"Q",1,0,(struct sockaddr*)&a,a.sun_len)==1,"send original byte");}
  else{CHECK(listen(server,2)==0&&connectTo(client,paths[0])==0,"queued own connection");}
 }
 struct{unsigned char before[8],address[1],after[256];} bounded;memset(&bounded,0xa5,sizeof bounded);socklen_t capacity=1;int r;
 if(!strcmp(mode,"getsockname"))r=getsockname(server,(struct sockaddr*)bounded.address,&capacity);
 else if(!strcmp(mode,"getpeername"))r=getpeername(client,(struct sockaddr*)bounded.address,&capacity);
 else if(datagram){char byte=0;r=recvfrom(server,&byte,1,0,(struct sockaddr*)bounded.address,&capacity);CHECK(r==1&&byte=='Q',"actual payload result preserved");}
 else{accepted=accept(server,(struct sockaddr*)bounded.address,&capacity);r=accepted;}
 CHECK(r>=0,"public socket query/receive/accept returns success");
 for(unsigned i=0;i<sizeof bounded.before;i++)CHECK(bounded.before[i]==0xa5,"leading canary preserved");
 for(unsigned i=0;i<sizeof bounded.after;i++)CHECK(bounded.after[i]==0xa5,"trailing canary: kernel-needed length never expands caller capacity");
 CHECK(capacity>1&&capacity<=sizeof(struct sockaddr_un),"full required guest address length reported");
 puts("PASS: actual guest socket caller capacity/canaries and return value");
done:
 if(accepted>=0)close(accepted);if(client>=0)close(client);if(server>=0)close(server);for(int i=0;i<2;i++)if(owned[i])unlink(paths[i]);rmdir(directory);return result;
}
