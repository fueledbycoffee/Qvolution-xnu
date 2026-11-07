#!/usr/bin/env python3
import os
import re
import sys
import tempfile
import subprocess

here = os.path.dirname(os.path.abspath(__file__))
emulation_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "../src/libsystem_kernel/emulation")
source_path = os.path.join(emulation_dir, "src/xnu_syscall/bsd/helper/misc/sysctl_proc.c")

with open(source_path, "r", encoding="utf-8") as f:
    source = f.read()

m = re.search(r"const char\* next_stat_elem\(char\*\* buf\)\n\{.*?\n\}\n\nvoid skip_stat_elems\(char\*\* buf, int count\)\n\{.*?\n\}", source, re.S)
if not m:
    sys.exit("next_stat_elem / skip_stat_elems not found in " + source_path)
body = m.group(0)

program = f"""
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

{body}

int main(void) {{
    // Case 1: comm with closing parenthesis and spaces
    {{
        char line[] = "1 (a) b) S 7 1";
        char *p = line;
        assert(strcmp(next_stat_elem(&p), "1") == 0);
        assert(strcmp(next_stat_elem(&p), "a) b") == 0);
        assert(strcmp(next_stat_elem(&p), "S") == 0);
        assert(strcmp(next_stat_elem(&p), "7") == 0);
        assert(strcmp(next_stat_elem(&p), "1") == 0);
        assert(next_stat_elem(&p) == NULL);
    }}

    // Case 2: nested parentheses
    {{
        char line[] = "1 ((x)) R 7 1";
        char *p = line;
        assert(strcmp(next_stat_elem(&p), "1") == 0);
        assert(strcmp(next_stat_elem(&p), "(x)") == 0);
        assert(strcmp(next_stat_elem(&p), "R") == 0);
        assert(strcmp(next_stat_elem(&p), "7") == 0);
        assert(strcmp(next_stat_elem(&p), "1") == 0);
        assert(next_stat_elem(&p) == NULL);
    }}

    // Case 3: plain process name
    {{
        char line[] = "1 (plain) Z 7 1";
        char *p = line;
        assert(strcmp(next_stat_elem(&p), "1") == 0);
        assert(strcmp(next_stat_elem(&p), "plain") == 0);
        assert(strcmp(next_stat_elem(&p), "Z") == 0);
        assert(strcmp(next_stat_elem(&p), "7") == 0);
        assert(strcmp(next_stat_elem(&p), "1") == 0);
        assert(next_stat_elem(&p) == NULL);
    }}

    // Case 4: missing ')' must return NULL and not read past buffer
    {{
        char line[] = "1 (missing";
        char *p = line;
        assert(strcmp(next_stat_elem(&p), "1") == 0);
        assert(next_stat_elem(&p) == NULL);
        assert(*p == '\\0');
        assert(next_stat_elem(&p) == NULL);
    }}

    // Case 5: empty comm '()'
    {{
        char line[] = "1 () S 7";
        char *p = line;
        assert(strcmp(next_stat_elem(&p), "1") == 0);
        assert(strcmp(next_stat_elem(&p), "") == 0);
        assert(strcmp(next_stat_elem(&p), "S") == 0);
        assert(strcmp(next_stat_elem(&p), "7") == 0);
        assert(next_stat_elem(&p) == NULL);
    }}

    // Case 6: complex process name with nested parentheses and spaces
    {{
        char line[] = "42 (Web Content (Pre-rendered)) D 100 42";
        char *p = line;
        assert(strcmp(next_stat_elem(&p), "42") == 0);
        assert(strcmp(next_stat_elem(&p), "Web Content (Pre-rendered)") == 0);
        assert(strcmp(next_stat_elem(&p), "D") == 0);
        assert(strcmp(next_stat_elem(&p), "100") == 0);
        assert(strcmp(next_stat_elem(&p), "42") == 0);
        assert(next_stat_elem(&p) == NULL);
    }}

    // Case 7: comm ending exactly at buffer termination with no trailing space
    {{
        char line[] = "1 (standalone)";
        char *p = line;
        assert(strcmp(next_stat_elem(&p), "1") == 0);
        assert(strcmp(next_stat_elem(&p), "standalone") == 0);
        assert(next_stat_elem(&p) == NULL);
    }}

    // Case 8: full stat field sequencing matching load_proc (fields 1 to 19)
    {{
        // 1:pid 2:comm 3:state 4:ppid 5:pgid 6:sid 7:tty_nr
        // 8:tpgid 9:flags 10:minflt 11:cminflt 12:majflt 13:cmajflt 14:utime 15:stime 16:cutime 17:cstime
        // 18:priority 19:nice 20:num_threads
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
    }}

    puts("PASS: proc stat comm parsing with parentheses and bounds checking");
    return 0;
}}
"""

with tempfile.TemporaryDirectory(prefix="proc-stat-comm-") as tmpdir:
    src_file = os.path.join(tmpdir, "test.c")
    bin_file = os.path.join(tmpdir, "test")
    with open(src_file, "w") as f:
        f.write(program)

    cc = os.environ.get("CC", "gcc")
    cmd = [cc, "-O1", "-g", "-Wall", "-fsanitize=address,undefined", src_file, "-o", bin_file]
    subprocess.run(cmd, check=True)
    subprocess.run([bin_file], check=True)
