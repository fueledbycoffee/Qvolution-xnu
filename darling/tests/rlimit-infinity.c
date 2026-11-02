#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr, "line %d: %s errno=%d\n", __LINE__, #x, errno);          \
      return 1;                                                                \
    }                                                                          \
  } while (0)
// The host kernel's own view of this process's data-size limit, as two
// strings ("unlimited" or a decimal byte count). Darling exposes the host
// /proc at /proc; this reads what the setter actually stored on Linux.
static int linux_data_limit(char *soft, char *hard) {
  FILE *f = fopen("/proc/self/limits", "r");
  char line[256];
  int found = 0;
  if (!f)
    return 0;
  while (!found && fgets(line, sizeof line, f))
    found = !strncmp(line, "Max data size", 13) &&
            sscanf(line + 13, "%31s %31s", soft, hard) == 2;
  fclose(f);
  return found;
}
static int expect_linux(const char *soft, const char *hard) {
  char s[32], h[32];
  CHECK(linux_data_limit(s, h));
  if (strcmp(s, soft) || strcmp(h, hard)) {
    fprintf(stderr, "linux data limit %s/%s, expected %s/%s\n", s, h, soft, hard);
    return 1;
  }
  return 0;
}
// Runs in a child because some cases lower the hard limit for good.
static int setter_cases(void) {
  struct rlimit l;
  // A finite soft limit under an unlimited hard limit.
  l.rlim_cur = 64ULL << 20;
  l.rlim_max = RLIM_INFINITY;
  CHECK(setrlimit(RLIMIT_DATA, &l) == 0);
  if (expect_linux("67108864", "unlimited"))
    return 1;
  // Back to unlimited.
  l.rlim_cur = RLIM_INFINITY;
  CHECK(setrlimit(RLIMIT_DATA, &l) == 0);
  if (expect_linux("unlimited", "unlimited"))
    return 1;
  CHECK(getrlimit(RLIMIT_DATA, &l) == 0);
  CHECK(l.rlim_cur == RLIM_INFINITY && l.rlim_max == RLIM_INFINITY);
  // A soft limit above the hard limit is invalid, also above RLIM_INFINITY.
  l.rlim_cur = RLIM_INFINITY + 1;
  l.rlim_max = RLIM_INFINITY;
  errno = 0;
  CHECK(setrlimit(RLIMIT_DATA, &l) == -1 && errno == EINVAL);
  l.rlim_cur = RLIM_INFINITY;
  l.rlim_max = 128ULL << 20;
  errno = 0;
  CHECK(setrlimit(RLIMIT_DATA, &l) == -1 && errno == EINVAL);
  if (expect_linux("unlimited", "unlimited"))
    return 1;
  // Finite soft and hard limits are stored unchanged.
  l.rlim_cur = 64ULL << 20;
  l.rlim_max = 128ULL << 20;
  CHECK(setrlimit(RLIMIT_DATA, &l) == 0);
  if (expect_linux("67108864", "134217728"))
    return 1;
  CHECK(getrlimit(RLIMIT_DATA, &l) == 0);
  CHECK(l.rlim_cur == 64ULL << 20 && l.rlim_max == 128ULL << 20);
  return 0;
}
static int check_setter(void) {
  struct rlimit l;
  char s[32], h[32];
  CHECK(getrlimit(RLIMIT_DATA, &l) == 0);
  CHECK(linux_data_limit(s, h));
  if (l.rlim_max != RLIM_INFINITY || strcmp(h, "unlimited")) {
    fprintf(stderr, "SKIP: the data-size hard limit is not unlimited\n");
    return 77;
  }
  fflush(stdout);
  pid_t child = fork();
  CHECK(child >= 0);
  if (child == 0)
    _exit(setter_cases());
  int status;
  CHECK(waitpid(child, &status, 0) == child);
  CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  printf("PASS: limits read back within RLIM_INFINITY; setrlimit stores "
         "Linux unlimited, finite pairs unchanged, rejects soft > hard\n");
  return 0;
}
static const struct {
  int resource;
  const char *name;
} resources[] = {
    {RLIMIT_CPU, "cpu"},     {RLIMIT_FSIZE, "fsize"},   {RLIMIT_DATA, "data"},
    {RLIMIT_STACK, "stack"}, {RLIMIT_CORE, "core"},     {RLIMIT_AS, "as"},
    {RLIMIT_MEMLOCK, "memlock"}, {RLIMIT_NPROC, "nproc"}, {RLIMIT_NOFILE, "nofile"},
};
int main(void) {
  int unlimited = 0;
  for (unsigned i = 0; i < sizeof(resources) / sizeof(resources[0]); i++) {
    struct rlimit l;
    CHECK(getrlimit(resources[i].resource, &l) == 0);
    printf("%s cur=%llu max=%llu\n", resources[i].name,
           (unsigned long long)l.rlim_cur, (unsigned long long)l.rlim_max);
    // RLIM_INFINITY is the largest value a Darwin limit can take.
    CHECK(l.rlim_cur <= RLIM_INFINITY && l.rlim_max <= RLIM_INFINITY);
    CHECK(l.rlim_cur <= l.rlim_max);
    unlimited += l.rlim_max == RLIM_INFINITY;
  }
  if (!unlimited) {
    fprintf(stderr, "SKIP: no resource is unlimited on this host\n");
    return 77;
  }
  return check_setter();
}
