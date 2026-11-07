# Run the real sys_utimes, built for a target without __NR_utimes (aarch64), against the host's
# utimensat on a temporary file.
# Usage: ruby darling/tests/utimes-timespec.rb [path/to/emulation]
require 'tmpdir'
root = File.expand_path(ARGV[0] || '../src/libsystem_kernel/emulation', __dir__)
source = File.read(File.join(root, 'src/xnu_syscall/bsd/impl/time/utimes.c'))
body = source[/^long sys_utimes\(.*?^\}/m]
abort 'sys_utimes not found' unless body
program = <<~C
  #define _GNU_SOURCE
  #include <assert.h>
  #include <errno.h>
  #include <fcntl.h>
  #include <stdio.h>
  #include <string.h>
  #include <sys/stat.h>
  #include <sys/syscall.h>
  #include <time.h>
  #include <unistd.h>
  #undef __NR_utimes
  struct bsd_timeval { unsigned long tv_sec; int tv_usec; };
  struct linux_timeval { unsigned long tv_sec; long tv_usec; };
  struct vchroot_expand_args { int flags; int dfd; char path[4096]; };
  #define VCHROOT_FOLLOW 1
  #define LINUX_AT_FDCWD AT_FDCWD
  #define LINUX_EINVAL EINVAL
  static int syscalls;
  #define LINUX_SYSCALL(nr, a, b, c, d) (syscalls++, syscall(nr, a, b, c, d) < 0 ? -errno : 0)
  static long errno_linux_to_bsd(long e) { return e; }
  static int get_perthread_wd(void) { return AT_FDCWD; }
  static int vchroot_expand(struct vchroot_expand_args* a) { (void)a; return 0; }
  #{body}
  int main(int argc, char **argv) {
      int fd = open(argv[1], O_RDWR | O_CREAT, 0600);
      assert(fd >= 0);
      struct bsd_timeval tv[2] = { { 1000000000, 250000 }, { 1100000000, 999999 } };
      struct stat st;
      assert(sys_utimes(argv[1], tv) == 0 && fstat(fd, &st) == 0);
      if (st.st_atim.tv_sec != 1000000000 || st.st_atim.tv_nsec != 250000000 ||
          st.st_mtim.tv_sec != 1100000000 || st.st_mtim.tv_nsec != 999999000) {
          printf("FAIL: atime %ld.%09ld mtime %ld.%09ld\\n", (long)st.st_atim.tv_sec,
              st.st_atim.tv_nsec, (long)st.st_mtim.tv_sec, st.st_mtim.tv_nsec);
          return 1;
      }
      time_t before = time(NULL);
      assert(sys_utimes(argv[1], NULL) == 0 && fstat(fd, &st) == 0);
      assert(st.st_mtim.tv_sec >= before && st.st_mtim.tv_sec <= time(NULL));
      int before_bad = syscalls;
      struct bsd_timeval bad[2] = { { 1, 1000000 }, { 1, 0 } };
      assert(sys_utimes(argv[1], bad) == -EINVAL);
      struct bsd_timeval bad2[2] = { { 1, 0 }, { 1, -1 } };
      assert(sys_utimes(argv[1], bad2) == -EINVAL);
      assert(syscalls == before_bad);
      puts("PASS: microseconds become nanoseconds, NULL means now, bad usec is EINVAL");
      return 0;
  }
C
Dir.mktmpdir('utimes-timespec') do |dir|
  input = File.join(dir, 'probe.c'); output = File.join(dir, 'probe')
  File.write(input, program)
  abort 'compile failed' unless system(ENV.fetch('CC', 'clang'), '-O1', '-g',
      '-Wall', '-fsanitize=address,undefined', input, '-o', output)
  abort 'utimes regression' unless system(output, File.join(dir, 'file'), rlimit_core: 0)
end
