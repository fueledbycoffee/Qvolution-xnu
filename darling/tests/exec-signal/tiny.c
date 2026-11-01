#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1
#include <signal.h>
#include <string.h>
#include <time.h>

int main(int argc, char **argv)
{
    sigset_t mask;
    if (argc != 2 || sigprocmask(SIG_SETMASK, NULL, &mask)) return 2;
    int blocked = !strcmp(argv[1], "blocked");
    if (sigismember(&mask, SIGUSR1) != 1 || sigismember(&mask, SIGWINCH) != blocked) return 3;
    /* Caught signals must have reverted to default (default SIGWINCH ignores).
     * Keep a post-exec delivery interval as well as the startup interval. */
    if (raise(SIGWINCH)) return 4;
    struct timespec pause = {0, 1000000};
    nanosleep(&pause, NULL);
    return 0;
}
