#ifndef THREADS_SIGNAL_H
#define THREADS_SIGNAL_H

#include <stdint.h>
#include <debug.h>
#include <list.h>

typedef unsigned short sigset_t;

enum sig_value
{
    SIG_CHLD = 0,
    SIG_CPU = 1,
    SIG_UBLOCK = 2,
    SIG_USR = 3,
    SIG_KILL = 4
};

enum how
{
    SIG_BLOCK = 0,
    SIG_UNBLOCK = 1,
    SIG_SETMASK = 2
};

enum sig_handler
{
    SIG_IGN = 0,
    SIG_DFL = 1
};

struct signal
{
    enum sig_value signum;      /* Signal type value. */
    struct list_elem elem;      /* List element added to signal lists. */
    int sender;
};

/* Signal handler functions. */
void CHLD_handler(int sender);
void KILL_handler(int sender);
void CPU_handler(void);
void USR_handler(int sender);

int signal_(int signum, int handler);
int kill(int tid, int signum);

/* Signal masking functions. */
int sigemptyset(sigset_t *set);
int sigfillset(sigset_t *set);
int sigaddset(sigset_t *set, int signum);
int sigdelset(sigset_t *set, int signum);
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);

#endif /* threads/signal.h */
