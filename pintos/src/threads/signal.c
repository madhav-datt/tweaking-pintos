//#include "threads/signal.h"
#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/**
 * Signal handling function
 * Set sigmask bits value for signal processing
 */
int signal_(int signum, int handler)
{
    struct thread* t = thread_current();

    /* SIG_CHLD */
    if (signum == 0 && handler == SIG_IGN)
        t->sigmask |= 1;
    else if (signum == 0 && handler == SIG_DFL)
        t->sigmask &= 14;

    /* SIG_CPU */
    else if (signum == 1 && handler == SIG_IGN)
        t->sigmask |= 2;
    else if (signum == 1 && handler == SIG_DFL)
        t->sigmask &= 13;

    /* SIG_UBLOCK */
    else if (signum == 2 && handler == SIG_IGN)
        t->sigmask |= 4;
    else if (signum == 2 && handler == SIG_DFL)
        t->sigmask &= 11;

    /* SIG_USR */
    else if (signum == 3 && handler == SIG_IGN)
        t->sigmask |= 8;
    else if (signum == 3 && handler == SIG_DFL)
        t->sigmask &= 7;

    /* SIG_KILL - do nothing. */

    return 0;
}

/**
 * Send signal signum to process tid
 */
int kill(int tid, int signum)
{
    struct thread* t = validated_tid(tid);
    if (t == NULL)
        return -1;

    if (signum == SIG_UBLOCK)
    {
        unsigned short tmp_sigmask = (t->sigmask) & 4;
        if (tmp_sigmask !=0)
            return -1;
        else if (t->status != THREAD_BLOCKED)
            return 0;

        list_push_back (&unblock_list, &t->unblock_elem);
    }

    else if (signum == SIG_USR)
    {
        struct signal* sig;
        bool is_sig_usr = false;
        unsigned short tmp_sigmask = (t->sigmask) & 8;
        if (tmp_sigmask != 0)
            return -1;

        for (struct list_elem* tmp_elem = list_begin (&t->pending_signals); tmp_elem != list_end (&t->pending_signals);
             tmp_elem = list_next (tmp_elem))
        {
            sig = list_entry (tmp_elem, struct signal, elem);
            if(sig->signum == (unsigned) signum)
            {
                sig->sender = thread_current()->tid;
                is_sig_usr = true;
                break;
            }
        }

        if (!is_sig_usr)
        {
            sig = (struct signal*) malloc(1 * sizeof(struct signal));
            sig->sender = thread_current()->tid;
            sig->signum = signum;
            list_push_back (&t->pending_signals, &sig->elem);
        }
    }

    else if (signum == SIG_KILL)
    {
        struct signal* sig;
        bool parent_found = false;
        bool prev_sig_kill = false;
        struct thread* tmp_thread = t;

        for (;;)
        {
            if (thread_current()->tid == tmp_thread->parent_thread->tid)
            {
                parent_found = true;
                break;
            }

            if(tmp_thread->parent_thread->tid == 1)
                break;

            tmp_thread = tmp_thread->parent_thread;
        }

        if(!parent_found)
            return -1;

        for (struct list_elem* tmp_elem = list_begin (&t->pending_signals); tmp_elem != list_end (&t->pending_signals);
             tmp_elem = list_next (tmp_elem))
        {
            sig = list_entry (tmp_elem, struct signal, elem);
            if(sig->signum == (unsigned) signum)
            {
                prev_sig_kill = true;
                sig->sender = thread_current()->tid;
                break;
            }
        }

        if (!prev_sig_kill)
        {
            sig = (struct signal*) malloc(1 * sizeof(struct signal));
            sig->sender = thread_current()->tid;
            sig->signum = signum;
            list_push_back (&t->pending_signals, &sig->elem);
        }
    }

    else
        return -1;
    return 0;
}

/**
 * Initialize the signal set given by set to empty,
 * with all signals excluded from the set.
 */
int sigemptyset(sigset_t* set)
{
    *set = 0;
    return 0;
}

/**
 * Initialize set to full
 * Include all signals
 */
int sigfillset(sigset_t* set)
{
    *set = 31;
    return 0;
}

/**
 * Delete signal signum from set.
 */
int sigdelset(sigset_t* set, int signum)
{
    if (signum < 0 || signum > 4)
        return -1;
    int set_mask_val[] = {30, 29, 27, 23, 15};
    *set &= set_mask_val[signum];
    return 0;
}

/**
 * Add signal signum from set.
 */
int sigaddset(sigset_t* set, int signum)
{
    if (signum < 0 || signum > 4)
        return -1;
    int set_mask_val[] = {1, 2, 4, 8, 16};
    *set |= set_mask_val[signum];
    return 0;
}

/**
 * Examine and change blocked signals
 */
int sigprocmask(int how, const sigset_t* set, sigset_t* oldset)
{
    struct thread* t = thread_current();
    if (oldset != NULL)
        *oldset = t->sigmask;

    if (how == SIG_BLOCK)
        t->sigmask |= (*set);
    else if (how == SIG_UNBLOCK)
        t->sigmask &= ~(*set);
    else if (how == SIG_SETMASK)
        t->sigmask = (*set);

    return 0;
}

/* Default signal handler functions for SIG_KILL, SIG_CHLD, SIG_CPU, and SIG_USR. */

void CHLD_handler(int sender)
{
    printf("SIG_CHLD from thread %d to %d", sender, thread_current()->tid);
    thread_current()->alive_children--;
    printf("Total children created: %d; Children alive: %d", thread_current()->total_children,
           thread_current()->alive_children);
}

void KILL_handler(int sender)
{
    printf("SIG_KILL from thread %d to %d\n", sender, thread_current()->tid);
    thread_exit();
}

void CPU_handler(void)
{
    printf("SIG_CPU received by thread %d\n", thread_current()->tid);
    thread_exit();
}

void USR_handler(int sender)
{
    printf("SIG_USR from thread %d to %d\n", sender, thread_current()->tid);
}
