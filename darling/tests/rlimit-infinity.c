#include <errno.h>
#include <stdio.h>
#include <sys/resource.h>
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr, "line %d: %s errno=%d\n", __LINE__, #x, errno);          \
      return 1;                                                                \
    }                                                                          \
  } while (0)
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
  // Round trip through a soft limit whose hard limit is unlimited.
  for (unsigned i = 0; i < sizeof(resources) / sizeof(resources[0]); i++) {
    struct rlimit l, saved;
    int r = resources[i].resource;
    CHECK(getrlimit(r, &saved) == 0);
    if (saved.rlim_max != RLIM_INFINITY || r == RLIMIT_NOFILE)
      continue;
    l.rlim_cur = 64ULL << 20;
    l.rlim_max = RLIM_INFINITY;
    CHECK(setrlimit(r, &l) == 0);
    CHECK(getrlimit(r, &l) == 0);
    CHECK(l.rlim_cur == 64ULL << 20 && l.rlim_max == RLIM_INFINITY);
    l.rlim_cur = RLIM_INFINITY;
    CHECK(setrlimit(r, &l) == 0);
    CHECK(getrlimit(r, &l) == 0);
    CHECK(l.rlim_cur == RLIM_INFINITY && l.rlim_max == RLIM_INFINITY);
    CHECK(setrlimit(r, &saved) == 0);
    printf("PASS: %s round trip through RLIM_INFINITY\n", resources[i].name);
    return 0;
  }
  fprintf(stderr, "SKIP: no adjustable unlimited resource\n");
  return 77;
}
