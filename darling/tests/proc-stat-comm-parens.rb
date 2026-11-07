# Exercise next_stat_elem() with parentheses, spaces, and edge cases in comm field.
# Usage: ruby darling/tests/proc-stat-comm-parens.rb [path/to/emulation]
require 'tmpdir'
root = File.expand_path(ARGV[0] || '../src/libsystem_kernel/emulation', __dir__)
source = File.read(File.join(root, 'src/xnu_syscall/bsd/helper/misc/sysctl_proc.c'))
body = source[/^const char\* next_stat_elem\(char\*\* buf\)\n\{.*?\n\}\n\nvoid skip_stat_elems\(char\*\* buf, int count\)\n\{.*?\n\}/m]
abort 'next_stat_elem / skip_stat_elems not found' unless body

program = <<~C
  #include <assert.h>
  #include <stdio.h>
  #include <stdlib.h>
  #include <string.h>

  #{body}

  int main(void) {
      // Case 1: comm with closing parenthesis and spaces
      {
          char line[] = "1 (a) b) S 7 1";
          char *p = line;
          assert(strcmp(next_stat_elem(&p), "1") == 0);
          assert(strcmp(next_stat_elem(&p), "a) b") == 0);
          assert(strcmp(next_stat_elem(&p), "S") == 0);
          assert(strcmp(next_stat_elem(&p), "7") == 0);
          assert(strcmp(next_stat_elem(&p), "1") == 0);
          assert(next_stat_elem(&p) == NULL);
      }

      // Case 2: nested parentheses
      {
          char line[] = "1 ((x)) R 7 1";
          char *p = line;
          assert(strcmp(next_stat_elem(&p), "1") == 0);
          assert(strcmp(next_stat_elem(&p), "(x)") == 0);
          assert(strcmp(next_stat_elem(&p), "R") == 0);
          assert(strcmp(next_stat_elem(&p), "7") == 0);
          assert(strcmp(next_stat_elem(&p), "1") == 0);
          assert(next_stat_elem(&p) == NULL);
      }

      // Case 3: plain process name
      {
          char line[] = "1 (plain) Z 7 1";
          char *p = line;
          assert(strcmp(next_stat_elem(&p), "1") == 0);
          assert(strcmp(next_stat_elem(&p), "plain") == 0);
          assert(strcmp(next_stat_elem(&p), "Z") == 0);
          assert(strcmp(next_stat_elem(&p), "7") == 0);
          assert(strcmp(next_stat_elem(&p), "1") == 0);
          assert(next_stat_elem(&p) == NULL);
      }

      // Case 4: missing ')' must return NULL and not read past buffer
      {
          char line[] = "1 (missing";
          char *p = line;
          assert(strcmp(next_stat_elem(&p), "1") == 0);
          assert(next_stat_elem(&p) == NULL);
          assert(*p == '\\0');
          assert(next_stat_elem(&p) == NULL);
      }

      // Case 5: empty comm '()'
      {
          char line[] = "1 () S 7";
          char *p = line;
          assert(strcmp(next_stat_elem(&p), "1") == 0);
          assert(strcmp(next_stat_elem(&p), "") == 0);
          assert(strcmp(next_stat_elem(&p), "S") == 0);
          assert(strcmp(next_stat_elem(&p), "7") == 0);
          assert(next_stat_elem(&p) == NULL);
      }

      // Case 6: complex process name with nested parentheses and spaces
      {
          char line[] = "42 (Web Content (Pre-rendered)) D 100 42";
          char *p = line;
          assert(strcmp(next_stat_elem(&p), "42") == 0);
          assert(strcmp(next_stat_elem(&p), "Web Content (Pre-rendered)") == 0);
          assert(strcmp(next_stat_elem(&p), "D") == 0);
          assert(strcmp(next_stat_elem(&p), "100") == 0);
          assert(strcmp(next_stat_elem(&p), "42") == 0);
          assert(next_stat_elem(&p) == NULL);
      }

      // Case 7: comm ending exactly at buffer termination with no trailing space
      {
          char line[] = "1 (standalone)";
          char *p = line;
          assert(strcmp(next_stat_elem(&p), "1") == 0);
          assert(strcmp(next_stat_elem(&p), "standalone") == 0);
          assert(next_stat_elem(&p) == NULL);
      }

      // Case 8: full stat field sequencing matching load_proc (fields 1 to 19)
      {
          char line[] = "100 (worker daemon (v2)) S 1 100 100 0 -1 4194560 10 20 30 40 500 600 700 800 20 5 4";
          char *p = line;

          skip_stat_elems(&p, 1); // skip pid
          assert(strcmp(next_stat_elem(&p), "worker daemon (v2)") == 0); // comm
          assert(strcmp(next_stat_elem(&p), "S") == 0); // state
          assert(strcmp(next_stat_elem(&p), "1") == 0); // ppid
          assert(strcmp(next_stat_elem(&p), "100") == 0); // pgid
          assert(strcmp(next_stat_elem(&p), "100") == 0); // sid
          assert(strcmp(next_stat_elem(&p), "0") == 0); // tty_nr

          skip_stat_elems(&p, 10); // skip 10 fields (tpgid .. cstime)
          assert(strcmp(next_stat_elem(&p), "20") == 0); // priority
          assert(strcmp(next_stat_elem(&p), "5") == 0); // nice
      }

      puts("PASS: proc stat comm parsing with parentheses and bounds checking");
      return 0;
  }
C

Dir.mktmpdir('proc-stat-comm-parens') do |dir|
  input = File.join(dir, 'test.c')
  output = File.join(dir, 'test')
  File.write(input, program)
  cc = ENV.fetch('CC', 'clang')
  abort 'compile failed' unless system(cc, '-O1', '-g', '-Wall', '-fsanitize=address,undefined', input, '-o', output)
  abort 'test failed' unless system(output, rlimit_core: 0)
end
