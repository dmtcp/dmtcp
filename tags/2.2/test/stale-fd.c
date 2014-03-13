#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>

//in this example, a disconnected fd is left open at checkpoint time
//child closes both sides of socketpair
//parent closes one side

int main(int argc, char* argv[])
{
  int count = 1;
  int sockets[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) < 0) {
    perror("socketpair()");
    return -1;
  }

  const char* me;

  if(fork()>0){
    //parent closes one
    close(sockets[0]);
    me = "parent";
  }else{
    //child closes both
    close(sockets[0]);
    close(sockets[1]);
    me = "child";
  }

  while (1)
  {
    printf("%s %d\n", me, count++);
    sleep(2);
  }

	printf("%s done\n",me);
  return 0;
}
