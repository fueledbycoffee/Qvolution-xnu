# sys_proc_info(PROC_INFO_CALL_LISTPIDS) on the host with a fake _sysctl_proc process table.
require 'tmpdir'
require 'open3'
root=File.realpath(ARGV.fetch(0))
source=File.read("#{root}/darling/src/libsystem_kernel/emulation/src/xnu_syscall/bsd/impl/misc/proc_info.c")
dispatch=source[/^long sys_proc_info\(.*?^\}\n/m] or abort 'sys_proc_info missing'
listpids=source[/^static long _proc_listpids\([^;]*?\)\n\{.*?^\}\n/m] || ''
kern_proc=File.read("#{root}/bsd/sys/sysctl.h").scan(/^#define KERN_PROC_(?:ALL|PGRP|TTY|UID|RUID)\s+\d+.*$/).join("\n")
proc_types=File.read("#{root}/bsd/sys/proc_info.h").scan(/^#define PROC_\w+_(?:PIDS|ONLY)\s+\d+.*$/).join("\n")
Dir.mktmpdir('proc-listpids-') do |dir|
  File.write("#{dir}/probe.c", <<~C)
    #include <assert.h>
    #include <errno.h>
    #include <stdint.h>
    #include <stdlib.h>
    #include <string.h>
    #include <sys/types.h>
    #{kern_proc}
    #{proc_types}
    /* Only the fields _proc_listpids reads. */
    struct kinfo_proc { struct { pid_t p_pid; } kp_proc; struct { pid_t e_ppid, e_pgid; uid_t uid; int tdev; } kp_eproc; };
    /* Stand-ins for the rest of sys_proc_info. */
    #define SYSLOG_ACTION_READ_ALL 3
    #define LINUX_PR_SET_NAME 15
    #define __NR_prctl 0
    #define PROC_INFO_CALL_PIDRUSAGE 9
    #define LINUX_SYSCALL(...) 0
    #define __simple_printf(...) ((void)0)
    static int __linux_syslog(int a, char *b, int c) { return 0; }
    static int errno_linux_to_bsd(int e) { return e; }
    static long _proc_pidinfo_pidrusage(int32_t a, uint32_t b, void *c, int32_t d) { return 0; }
    static long _proc_pidinfo(int32_t a, uint32_t b, uint64_t c, void *d, int32_t e) { return 0; }
    static long _proc_listpids(uint32_t type, uint32_t typeinfo, void* buffer, int32_t bufsize);
    static pid_t sys_getpid(void) { return 100; }

    /* pid, ppid, pgid, uid, tty */
    static const struct kinfo_proc table[] = {
      {{1},{0,1,0,0}}, {{100},{1,100,501,0x400}}, {{101},{100,100,501,0x400}},
      {{102},{100,102,0,0}}, {{103},{1,103,501,0}},
    };
    static int nprocs=5;
    int _sysctl_proc(int what, int flag, struct kinfo_proc *out, unsigned long *buflen) {
      struct kinfo_proc match[16];
      int n=0;
      for (int i=0; i<nprocs; i++) {
        const struct kinfo_proc *p=&table[i];
        if ((what==KERN_PROC_PGRP && p->kp_eproc.e_pgid!=flag) ||
            (what==KERN_PROC_TTY && p->kp_eproc.tdev!=flag) ||
            ((what==KERN_PROC_UID || what==KERN_PROC_RUID) && p->kp_eproc.uid!=(uid_t)flag))
          continue;
        match[n++]=*p;
      }
      if (out==NULL || *buflen<n*sizeof(*out)) {
        *buflen=n*sizeof(*out);
        return out ? -ENOMEM : 0;
      }
      memcpy(out,match,n*sizeof(*out));
      *buflen=n*sizeof(*out);
      return 0;
    }
    #{dispatch}
    #{listpids}
    #define LIST(type, info, buf, size) sys_proc_info(1, (type), (info), 0, (buf), (size))
    static int has(const int *buf, long bytes, int pid) {
      for (long i=0; i<bytes/(long)sizeof(int); i++) if (buf[i]==pid) return 1;
      return 0;
    }
    int main(void) {
      int buf[32];
      long r;

      /* NULL buffer: XNU sizes for every process plus 20. */
      assert(LIST(PROC_ALL_PIDS,0,NULL,0)==(5+20)*sizeof(int));
      r=LIST(PROC_ALL_PIDS,0,buf,sizeof(buf));
      assert(r==5*sizeof(int) && has(buf,r,1) && has(buf,r,100) && has(buf,r,103));

      /* proc_listchildpids() */
      r=LIST(PROC_PPID_ONLY,100,buf,sizeof(buf));
      assert(r==2*sizeof(int) && has(buf,r,101) && has(buf,r,102));
      r=LIST(PROC_PGRP_ONLY,100,buf,sizeof(buf));
      assert(r==2*sizeof(int) && has(buf,r,100) && has(buf,r,101));
      r=LIST(PROC_UID_ONLY,501,buf,sizeof(buf));
      assert(r==3*sizeof(int) && !has(buf,r,1) && !has(buf,r,102));
      r=LIST(PROC_RUID_ONLY,0,buf,sizeof(buf));
      assert(r==2*sizeof(int) && has(buf,r,1) && has(buf,r,102));
      r=LIST(PROC_TTY_ONLY,0x400,buf,sizeof(buf));
      assert(r==2*sizeof(int));
      assert(LIST(PROC_KDBG_ONLY,0,buf,sizeof(buf))==0);
      assert(LIST(42,0,buf,sizeof(buf))==0);

      /* A short buffer is filled, not overrun; under one pid is ENOMEM. */
      buf[2]=-7;
      assert(LIST(PROC_ALL_PIDS,0,buf,2*sizeof(int))==2*sizeof(int) && buf[2]==-7);
      assert(LIST(PROC_ALL_PIDS,0,buf,3)==-ENOMEM);
    }
  C
  %w[-O0 -O2].each do |opt|
    out,status=Open3.capture2e('clang',opt,"#{dir}/probe.c",'-o',"#{dir}/probe")
    abort out unless status.success?
    out,status=Open3.capture2e("#{dir}/probe")
    abort out unless status.success?
  end
  puts 'PASS proc_listpids lists every process and filters by type at O0/O2'
end
