// This file defines functions:  mtcp_state_XXX()
// The pthread versions were added by Jason Ansel to eliminate need for futex.
// Futex is specific to Linux.


#include "mtcp_internal.h"
#include <errno.h>

#if USE_FUTEX
# include "mtcp_futex.h"
#endif

#ifndef USE_FUTEX
# include <pthread.h>
#endif

__attribute__ ((visibility ("hidden")))
void mtcp_state_init(MtcpState * state, int value)
{
#if USE_FUTEX
  state->value = value;
#else
  pthread_mutex_init(&state->mutex,NULL);
  pthread_cond_init(&state->cond,NULL);
  state->value = value;
#endif
}

void mtcp_state_destroy(MtcpState * state)
{
#if USE_FUTEX
  //no action
#else
  pthread_mutex_destroy(&state->mutex);
  pthread_cond_destroy(&state->cond);
#endif
}

__attribute__ ((visibility ("hidden")))
void mtcp_state_futex(MtcpState * state, int func, int val,
                      struct timespec const *timeout)
{
#if USE_FUTEX
  int rc;

  /* (int *) cast needed since state->value is "int volatile"  - Gene */
  while ((rc = mtcp_futex ((int *)&state->value, func, val, timeout)) < 0
         && rc > -4096) { /* large unsigned int from kernel can appear neg. */
    rc = -rc;
    if ((rc == ETIMEDOUT) || (rc == EWOULDBLOCK)) break;
    if (rc != EINTR) {
      MTCP_PRINTF("futex error %d.\n", rc);
      MTCP_PRINTF("value, func, val, timeout: (%p, %d, %d, %p, NULL, 0)\n",
                  &state->value, func, val, timeout);
      mtcp_abort ();
    }
  }
#else
  int rv = -1;
  pthread_mutex_lock(&state->mutex);
  switch(func){
    case FUTEX_WAIT:
      if(timeout == NULL) {
        rv=pthread_cond_wait(&state->cond,&state->mutex);
      } else {
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
      MTCP_PRINTF("unknown func=%d",func);
      mtcp_abort();
  }
  if(rv != 0 && rv != ETIMEDOUT){
    MTCP_PRINTF("pthread_cond_* failure func=%d,val=%d",func,val);
    mtcp_abort();
  }
  pthread_mutex_unlock(&state->mutex);
#endif
}

__attribute__ ((visibility ("hidden")))
int mtcp_state_set(MtcpState * state, int value, int oldval)
{
  return atomic_setif_int(&state->value, value, oldval);
}

__attribute__ ((visibility ("hidden")))
int mtcp_state_value(MtcpState * state)
{
  return state->value;
}
