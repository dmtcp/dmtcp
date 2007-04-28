// This file defines functions:  mtcp_state_XXX()
// They were added by Jason Ansel to eliminate need for futex.
// Futex is specific to Linux.

#ifdef __x86_64__
// The alternative to using futex is to load in the pthread library,
//  which would be a real pain.  The __i386__ arch doesn't seem to be bothered
//  by this.
# define USE_FUTEX
#endif

#include "mtcp_internal.h"
#include <asm/ldt.h>      // for struct user_desc
#include <asm/segment.h>  // for GDT_ENTRY_TLS_... stuff
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/sem.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>


__attribute__ ((visibility ("hidden")))
   void mtcp_state_init(MtcpState * state, int value){
#ifdef USE_FUTEX
    state->value = value;
#else
    pthread_mutex_init(&state->mutex,NULL);
    pthread_cond_init(&state->cond,NULL);
    state->value = value;
#endif
}
void mtcp_state_destroy(MtcpState * state){
#ifdef USE_FUTEX
   //no action
#else
    pthread_mutex_destroy(&state->mutex);
    pthread_cond_destroy(&state->cond);
#endif
}

__attribute__ ((visibility ("hidden")))
   void mtcp_state_futex(MtcpState * state, int func, int val, struct timespec const *timeout){
#ifdef USE_FUTEX
    if ((mtcp_sys_kernel_futex (&state->value, func, val, timeout, NULL, 0) < 0)
        && (errno != ETIMEDOUT) && (errno != EWOULDBLOCK)
        && (mtcp_sys_errno != EINTR)) {
        mtcp_printf ("mtcp futex_ec: futex error: %s\n", strerror (errno));
        mtcp_abort ();
    }
#else
    int rv = -1;
    pthread_mutex_lock(&state->mutex);
    switch(func){
        case FUTEX_WAIT:
            if(timeout == NULL)
                rv=pthread_cond_wait(&state->cond,&state->mutex);
            else
            {
                struct timespec tmp = *timeout;
                tmp.tv_sec += time(NULL);
                rv=pthread_cond_timedwait(&state->cond,&state->mutex,&tmp);
            }
            break;
            
        case FUTEX_WAKE:
            if(val==1)
                rv=pthread_cond_signal(&state->cond);
            else
                rv=pthread_cond_broadcast(&state->cond);
            break;
                    
        default:
            mtcp_printf("mtcp_state_futex* : unknown func=%d",func);
            mtcp_abort();
    }
    if(rv != 0 && rv != ETIMEDOUT){
        mtcp_printf("mtcp_state_futex* : pthread_cond_* failure func=%d,val=%d",func,val);
        mtcp_abort();
    }
    pthread_mutex_unlock(&state->mutex);
#endif
}

__attribute__ ((visibility ("hidden")))
   int mtcp_state_set(MtcpState * state, int value, int oldval){
    return atomic_setif_int(&state->value, value, oldval); 
}

__attribute__ ((visibility ("hidden")))
   int mtcp_state_value(MtcpState * state){
    return state->value;
}
