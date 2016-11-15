#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <iostream>
#undef NDEBUG
#include <assert.h>


void doAccept(int &acceptSock, int listenSock);
void doConnect(int &connectSock,
               struct hostent *host,
               int port,
               const char *hostname);

int
main(int argc, char **argv)
{
  if (argc != 4 && argc != 5) {
    printf("usage: player listen-port connect-host connect-port [starter]\n");
    return -1;
  }

  int listenSock = socket(AF_INET, SOCK_STREAM, 0);
  int connectSock = socket(AF_INET, SOCK_STREAM, 0);
  int acceptSock = -1;
  assert(listenSock > 0 && connectSock > 0);

  int listenPort = atoi(argv[1]);
  hostent *connectHost = gethostbyname(argv[2]);
  int connectPort = atoi(argv[3]);
  assert(listenPort > 0);
  assert(connectHost != NULL);
  assert(connectPort > 0);

  bool isStarterNode = (argc == 5);

  // bind listen socket
  {
    sockaddr_in listenAddy;
    memset(&listenAddy, 0, sizeof(listenAddy));
    listenAddy.sin_family = AF_INET;
    listenAddy.sin_addr.s_addr = INADDR_ANY;
    listenAddy.sin_port = htons(listenPort);

    assert(bind(listenSock, (sockaddr *)&listenAddy, sizeof(listenAddy)) >= 0);

    assert(listen(listenSock, 5) >= 0);
  }

  {
    // std::cout << "starter node? [y/n] ";
    // std::string c;
    // std::cin >> c;
    // if ( c[0]=='y' || c[0]=='Y' ) isStarterNode = true;
  }

  if (!isStarterNode) {
    std::cout << "accepting..." << std::endl;
    doAccept(acceptSock, listenSock);
    std::cout << "connecting..." << std::endl;
    doConnect(connectSock, connectHost, connectPort, argv[2]);
  } else {
    std::cout << "connecting..." << std::endl;
    doConnect(connectSock, connectHost, connectPort, argv[2]);
    std::cout << "accepting..." << std::endl;
    doAccept(acceptSock, listenSock);
  }
  std::cout << "ready" << std::endl;

  for (;;) {
    std::cout << "throw [a-z] or catch [C]?";
    std::cout.flush();
    std::string c = " ";
    std::cin >> c;
    if (c[0] >= 'a' && c[0] <= 'z') {
      std::cout << "throwing a '" << c[0] << "' to next player..." << std::endl;
      assert(write(connectSock, &c[0], 1) == 1);
      fsync(connectSock);
      std::cout << "throw complete." << std::endl;
    } else {
      std::cout << "catching from previos player..." << std::endl;
      assert(read(acceptSock, &c[0], 1) == 1);
      std::cout << "caught a '" << c[0] << "'." << std::endl;
    }
  }

  return 0;
}

void
doAccept(int &acceptSock, int listenSock)
{
  assert((acceptSock = accept(listenSock, NULL, NULL)) > 0);
}

void
doConnect(int &connectSock, hostent *host, int port, const char *hostname)
{
  sockaddr_in addr;

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  memcpy(&addr.sin_addr.s_addr, host->h_addr, host->h_length);
  addr.sin_port = htons(port);
  int fd = -1;
  for (size_t i = 0; i < 20; i++) {
    fd = connect(connectSock, (sockaddr *)&addr, sizeof(addr));
    if (fd != -1) {
      break;
    }

    // Sleep for 100 ms to allow the other process to do a bind.
    struct timespec t;
    t.tv_sec = 0;
    t.tv_nsec = 100 * 1000 * 1000;
    nanosleep(&t, NULL);
  }

  // If connect fails even after 20 tries, give up.
  assert(fd >= 0);
}
