#include "../src/dmtcpmessagetypes.h"
#include "../src/workerstate.h"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>

namespace {

void
writeAll(int fd, const void *buffer, size_t bytes)
{
  const char *cursor = static_cast<const char *>(buffer);
  while (bytes > 0) {
    ssize_t written = write(fd, cursor, bytes);
    if (written == -1 && errno == EINTR) {
      continue;
    }
    if (written <= 0) {
      throw std::runtime_error("write failed");
    }
    cursor += written;
    bytes -= written;
  }
}

void
readAll(int fd, void *buffer, size_t bytes)
{
  char *cursor = static_cast<char *>(buffer);
  while (bytes > 0) {
    ssize_t received = read(fd, cursor, bytes);
    if (received == -1 && errno == EINTR) {
      continue;
    }
    if (received <= 0) {
      throw std::runtime_error("read failed");
    }
    cursor += received;
    bytes -= received;
  }
}

int
connectToCoordinator(const char *host, int port)
{
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd == -1) {
    throw std::runtime_error("socket failed");
  }

  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port));
  if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
    close(fd);
    throw std::runtime_error("invalid IPv4 coordinator host");
  }

  if (connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == -1) {
    close(fd);
    throw std::runtime_error("connect failed");
  }

  return fd;
}

std::string
handshakeExtraData(const char *progname)
{
  char hostname[256] = {};
  if (gethostname(hostname, sizeof(hostname) - 1) == -1) {
    throw std::runtime_error("gethostname failed");
  }

  std::string data(hostname);
  data.push_back('\0');
  data.append(progname);
  data.push_back('\0');
  return data;
}

int
parsePort(const char *text)
{
  char *end = nullptr;
  errno = 0;
  long port = strtol(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0' || port <= 0 || port > 65535) {
    throw std::runtime_error("invalid port");
  }
  return static_cast<int>(port);
}

int
parseHoldSeconds(int argc, char **argv)
{
  if (argc == 3) {
    return 5;
  }
  if (argc == 5 && strcmp(argv[3], "--hold-seconds") == 0) {
    return parsePort(argv[4]);
  }
  throw std::runtime_error(
    "usage: coordinator_synthetic_worker HOST PORT [--hold-seconds SECONDS]");
}

} // namespace

int
main(int argc, char **argv)
{
  try {
    if (argc != 3 && argc != 5) {
      throw std::runtime_error(
        "usage: coordinator_synthetic_worker HOST PORT [--hold-seconds SECONDS]");
    }

    const char *host = argv[1];
    int port = parsePort(argv[2]);
    int holdSeconds = parseHoldSeconds(argc, argv);

    dmtcp::WorkerState::setCurrentState(dmtcp::WorkerState::RUNNING);
    dmtcp::DmtcpMessage hello(dmtcp::DMT_NEW_WORKER);
    hello.virtualPid = -1;
    hello.realPid = getpid();

    std::string extraData = handshakeExtraData("coordinator_synthetic_worker");
    hello.extraBytes = extraData.size();

    int fd = connectToCoordinator(host, port);
    writeAll(fd, &hello, sizeof(hello));
    writeAll(fd, extraData.data(), extraData.size());

    dmtcp::DmtcpMessage reply;
    readAll(fd, &reply, sizeof(reply));
    if (!reply.isValid() || reply.type != dmtcp::DMT_ACCEPT ||
        reply.virtualPid == -1) {
      close(fd);
      throw std::runtime_error("coordinator rejected synthetic worker");
    }

    std::cout << "accepted virtual_pid=" << reply.virtualPid << '\n';
    std::cout.flush();
    std::this_thread::sleep_for(std::chrono::seconds(holdSeconds));
    close(fd);
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }
}
