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

struct Options {
  const char *host = nullptr;
  int port = -1;
  int holdSeconds = 5;
  bool expectKill = false;
};

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
parsePositiveInt(const char *text)
{
  return parsePort(text);
}

Options
parseOptions(int argc, char **argv)
{
  if (argc < 3) {
    throw std::runtime_error(
      "usage: coordinator_synthetic_worker HOST PORT "
      "[--hold-seconds SECONDS] [--expect-kill]");
  }

  Options options;
  options.host = argv[1];
  options.port = parsePort(argv[2]);

  for (int i = 3; i < argc; ++i) {
    if (strcmp(argv[i], "--hold-seconds") == 0) {
      if (++i == argc) {
        throw std::runtime_error("--hold-seconds requires a value");
      }
      options.holdSeconds = parsePositiveInt(argv[i]);
    } else if (strcmp(argv[i], "--expect-kill") == 0) {
      options.expectKill = true;
    } else {
      throw std::runtime_error("unknown argument");
    }
  }

  return options;
}

} // namespace

int
main(int argc, char **argv)
{
  try {
    Options options = parseOptions(argc, argv);

    dmtcp::WorkerState::setCurrentState(dmtcp::WorkerState::RUNNING);
    dmtcp::DmtcpMessage hello(dmtcp::DMT_NEW_WORKER);
    hello.virtualPid = -1;
    hello.realPid = getpid();

    std::string extraData = handshakeExtraData("coordinator_synthetic_worker");
    hello.extraBytes = extraData.size();

    int fd = connectToCoordinator(options.host, options.port);
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

    if (options.expectKill) {
      dmtcp::DmtcpMessage msg;
      readAll(fd, &msg, sizeof(msg));
      if (!msg.isValid() || msg.type != dmtcp::DMT_KILL_PEER) {
        close(fd);
        throw std::runtime_error("expected DMT_KILL_PEER");
      }
      std::cout << "received DMT_KILL_PEER\n";
      std::cout.flush();
    } else {
      std::this_thread::sleep_for(std::chrono::seconds(options.holdSeconds));
    }

    close(fd);
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }
}
