#include <linux/unistd.h>
#include <asm/ldt.h> 
#include <stdio.h> 
#include <unistd.h> 
#include <string.h> 

#include <pthread.h>
#include <errno.h>
#include "mtcp_internal.h"

int mtcp_sys_errno;

void *start_routine(void *arg) {
  struct user_desc u_info;
int i;
for (i = -1; i < 256; i++) {
  u_info.entry_number = i;
  if (-1 == mtcp_sys_get_thread_area(&u_info) && mtcp_sys_errno == ENOSYS)
	printf("error: %s\n", strerror(mtcp_sys_errno));
  else {printf("SUCCESS!!!!\n"); exit(0);}
}
  return NULL;
}

int main() {

  pthread_t thread;
  if (0!=pthread_create(&thread, NULL, start_routine, NULL))
    printf("pthread: %s\n", strerror(errno));
  sleep(2);
}
