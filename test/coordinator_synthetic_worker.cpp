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
  bool expectCheckpoint = false;
  bool expectDuplicateCheckpoint = false;
  bool expectRejectNotRestarting = false;
  bool expectKvdb = false;
  bool expectInvalidProtocolReject = false;
  bool expectOversizedExtraReject = false;
  bool sendPartialMessage = false;
  bool restartWorker = false;
  int numPeers = 0;
  bool invalidCompGroup = false;
  std::string barrier;
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

bool
readMessageOrEof(int fd, dmtcp::DmtcpMessage *msg)
{
  char *cursor = reinterpret_cast<char *>(msg);
  size_t bytes = sizeof(*msg);
  bool sawBytes = false;
  while (bytes > 0) {
    ssize_t received = read(fd, cursor, bytes);
    if (received == -1 && errno == EINTR) {
      continue;
    }
    if (received == 0 && !sawBytes) {
      return false;
    }
    if (received <= 0) {
      throw std::runtime_error("partial message read failed");
    }
    sawBytes = true;
    cursor += received;
    bytes -= received;
  }
  return true;
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

dmtcp::UniquePid
syntheticRestartCompGroup()
{
  return dmtcp::UniquePid(0x64746d746370ULL, 1, 0x72657374617274ULL, 0);
}

void
sendBarrier(int fd, const std::string& barrier)
{
  dmtcp::DmtcpMessage msg(dmtcp::DMT_BARRIER);
  if (barrier.size() >= sizeof(msg.barrier)) {
    throw std::runtime_error("barrier name is too long");
  }
  std::strncpy(msg.barrier, barrier.c_str(), sizeof(msg.barrier) - 1);
  writeAll(fd, &msg, sizeof(msg));
}

std::string
readExtraString(int fd, uint32_t bytes)
{
  std::string extra(bytes, '\0');
  if (bytes != 0) {
    readAll(fd, extra.data(), bytes);
    if (!extra.empty() && extra.back() == '\0') {
      extra.pop_back();
    }
  }
  return extra;
}

void
sendKvdbRequest(int fd,
                dmtcp::kvdb::KVDBRequest request,
                const char *id,
                const char *key,
                const char *val)
{
  dmtcp::DmtcpMessage msg(dmtcp::DMT_KVDB_REQUEST);
  msg.kvdbRequest = request;
  if (std::strlen(id) >= sizeof(msg.kvdbId)) {
    throw std::runtime_error("KVDB id is too long");
  }
  std::strncpy(msg.kvdbId, id, sizeof(msg.kvdbId) - 1);

  std::string payload(key);
  payload.push_back('\0');
  payload.append(val);
  payload.push_back('\0');

  msg.keyLen = std::strlen(key) + 1;
  msg.valLen = std::strlen(val) + 1;
  msg.extraBytes = payload.size();

  writeAll(fd, &msg, sizeof(msg));
  writeAll(fd, payload.data(), payload.size());
}

std::string
readKvdbValue(int fd)
{
  dmtcp::DmtcpMessage msg;
  readAll(fd, &msg, sizeof(msg));
  if (!msg.isValid() || msg.type != dmtcp::DMT_KVDB_RESPONSE ||
      msg.kvdbResponse != dmtcp::kvdb::KVDBResponse::SUCCESS) {
    throw std::runtime_error("expected successful DMT_KVDB_RESPONSE");
  }
  return readExtraString(fd, msg.extraBytes);
}

std::string
waitForBarrierRelease(int fd)
{
  dmtcp::DmtcpMessage msg;
  readAll(fd, &msg, sizeof(msg));
  if (!msg.isValid() || msg.type != dmtcp::DMT_BARRIER_RELEASED) {
    throw std::runtime_error("expected DMT_BARRIER_RELEASED");
  }
  return readExtraString(fd, msg.extraBytes);
}

Options
parseOptions(int argc, char **argv)
{
  if (argc < 3) {
    throw std::runtime_error(
      "usage: coordinator_synthetic_worker HOST PORT "
      "[--hold-seconds SECONDS] [--expect-kill] [--expect-checkpoint] "
      "[--expect-duplicate-checkpoint-after-update] "
      "[--expect-reject-not-restarting] "
      "[--expect-kvdb] "
      "[--expect-invalid-protocol-reject] "
      "[--expect-oversized-extra-reject] "
      "[--send-partial-message] "
      "[--restart-worker] [--num-peers PEERS] "
      "[--invalid-comp-group] [--barrier NAME]");
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
    } else if (strcmp(argv[i], "--expect-checkpoint") == 0) {
      options.expectCheckpoint = true;
    } else if (strcmp(argv[i],
                      "--expect-duplicate-checkpoint-after-update") == 0) {
      options.expectDuplicateCheckpoint = true;
    } else if (strcmp(argv[i], "--expect-reject-not-restarting") == 0) {
      options.expectRejectNotRestarting = true;
    } else if (strcmp(argv[i], "--expect-kvdb") == 0) {
      options.expectKvdb = true;
    } else if (strcmp(argv[i], "--expect-invalid-protocol-reject") == 0) {
      options.expectInvalidProtocolReject = true;
    } else if (strcmp(argv[i], "--expect-oversized-extra-reject") == 0) {
      options.expectOversizedExtraReject = true;
    } else if (strcmp(argv[i], "--send-partial-message") == 0) {
      options.sendPartialMessage = true;
    } else if (strcmp(argv[i], "--restart-worker") == 0) {
      options.restartWorker = true;
    } else if (strcmp(argv[i], "--num-peers") == 0) {
      if (++i == argc) {
        throw std::runtime_error("--num-peers requires a value");
      }
      options.numPeers = parsePositiveInt(argv[i]);
    } else if (strcmp(argv[i], "--invalid-comp-group") == 0) {
      options.invalidCompGroup = true;
    } else if (strcmp(argv[i], "--barrier") == 0) {
      if (++i == argc) {
        throw std::runtime_error("--barrier requires a value");
      }
      options.barrier = argv[i];
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
    const bool restartHandshake =
      options.expectRejectNotRestarting || options.restartWorker;

    dmtcp::WorkerState::setCurrentState(
      restartHandshake ? dmtcp::WorkerState::RESTARTING
                       : dmtcp::WorkerState::RUNNING);
    dmtcp::DmtcpMessage hello(restartHandshake
                              ? dmtcp::DMT_RESTART_WORKER
                              : dmtcp::DMT_NEW_WORKER);
    hello.virtualPid = -1;
    hello.realPid = getpid();
    if (options.restartWorker) {
      hello.compGroup = syntheticRestartCompGroup();
      hello.numPeers = options.numPeers;
    }
    if (options.invalidCompGroup) {
      hello.compGroup = dmtcp::UniquePid(1, 1, 1);
    }

    std::string extraData;
    if (options.expectInvalidProtocolReject) {
      std::memset(hello._magicBits, 'X', sizeof(hello._magicBits));
    } else if (options.expectOversizedExtraReject) {
      hello.extraBytes = DMTCP_MAX_MESSAGE_EXTRA_BYTES + 1;
    } else {
      extraData = handshakeExtraData("coordinator_synthetic_worker");
      hello.extraBytes = extraData.size();
    }

    int fd = connectToCoordinator(options.host, options.port);
    if (options.sendPartialMessage) {
      writeAll(fd, &hello, sizeof(hello) / 2);
      close(fd);
      std::cout << "sent partial protocol message\n";
      std::cout.flush();
      return 0;
    }

    writeAll(fd, &hello, sizeof(hello));
    if (!extraData.empty()) {
      writeAll(fd, extraData.data(), extraData.size());
    }

    if (options.expectInvalidProtocolReject ||
        options.expectOversizedExtraReject) {
      dmtcp::DmtcpMessage reply;
      if (readMessageOrEof(fd, &reply)) {
        close(fd);
        throw std::runtime_error("coordinator accepted invalid protocol");
      }
      std::cout << "rejected invalid protocol\n";
      std::cout.flush();
      close(fd);
      return 0;
    }

    dmtcp::DmtcpMessage reply;
    readAll(fd, &reply, sizeof(reply));
    if (options.expectRejectNotRestarting) {
      if (!reply.isValid() || reply.type != dmtcp::DMT_REJECT_NOT_RESTARTING) {
        close(fd);
        throw std::runtime_error("expected DMT_REJECT_NOT_RESTARTING");
      }
      std::cout << "rejected DMT_REJECT_NOT_RESTARTING\n";
      std::cout.flush();
      close(fd);
      return 0;
    }

    if (options.invalidCompGroup) {
      if (!reply.isValid() || reply.type != dmtcp::DMT_REJECT_WRONG_COMP) {
        close(fd);
        throw std::runtime_error("expected DMT_REJECT_WRONG_COMP");
      }
      std::cout << "rejected DMT_REJECT_WRONG_COMP\n";
      std::cout.flush();
      close(fd);
      return 0;
    }

    if (!reply.isValid() || reply.type != dmtcp::DMT_ACCEPT ||
        (!options.restartWorker && reply.virtualPid == -1)) {
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
    } else if (options.expectCheckpoint) {
      dmtcp::DmtcpMessage msg;
      readAll(fd, &msg, sizeof(msg));
      if (!msg.isValid() || msg.type != dmtcp::DMT_DO_CHECKPOINT) {
        close(fd);
        throw std::runtime_error("expected DMT_DO_CHECKPOINT");
      }
      std::cout << "received DMT_DO_CHECKPOINT\n";
      std::cout.flush();
      std::this_thread::sleep_for(std::chrono::seconds(options.holdSeconds));
    } else if (options.expectDuplicateCheckpoint) {
      dmtcp::DmtcpMessage msg;
      readAll(fd, &msg, sizeof(msg));
      if (!msg.isValid() || msg.type != dmtcp::DMT_DO_CHECKPOINT) {
        close(fd);
        throw std::runtime_error("expected first DMT_DO_CHECKPOINT");
      }

      dmtcp::DmtcpMessage update(
        dmtcp::DMT_UPDATE_PROCESS_INFO_AFTER_INIT_OR_EXEC);
      std::string progname = "coordinator_synthetic_worker_exec";
      update.extraBytes = progname.size() + 1;
      writeAll(fd, &update, sizeof(update));
      writeAll(fd, progname.c_str(), update.extraBytes);

      readAll(fd, &msg, sizeof(msg));
      if (!msg.isValid() || msg.type != dmtcp::DMT_DO_CHECKPOINT) {
        close(fd);
        throw std::runtime_error("expected duplicate DMT_DO_CHECKPOINT");
      }
      std::cout << "received duplicate DMT_DO_CHECKPOINT\n";
      std::cout.flush();
      std::this_thread::sleep_for(std::chrono::seconds(options.holdSeconds));
    } else if (options.expectKvdb) {
      sendKvdbRequest(fd, dmtcp::kvdb::KVDBRequest::SET,
                      "synthetic-db", "synthetic-key", "synthetic-value");
      std::string oldVal = readKvdbValue(fd);
      sendKvdbRequest(fd, dmtcp::kvdb::KVDBRequest::GET,
                      "synthetic-db", "synthetic-key", "");
      std::string val = readKvdbValue(fd);
      std::cout << "kvdb old=" << oldVal << " value=" << val << '\n';
      std::cout.flush();
      std::this_thread::sleep_for(std::chrono::seconds(options.holdSeconds));
    } else if (!options.barrier.empty()) {
      sendBarrier(fd, options.barrier);
      std::string released = waitForBarrierRelease(fd);
      if (released != options.barrier) {
        close(fd);
        throw std::runtime_error("barrier release name mismatch");
      }
      std::cout << "released barrier=" << released << '\n';
      std::cout.flush();
      std::this_thread::sleep_for(std::chrono::seconds(options.holdSeconds));
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
