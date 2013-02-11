
#include <unistd.h>
#include <sys/syscall.h>

#include "util.h"
#include "protectedfds.h"
#include "jfilesystem.h"
#include "socketconnection.h"
#include "socketconnlist.h"
#include "kernelbufferdrainer.h"
#include "connectionrewirer.h"

using namespace dmtcp;

void dmtcp_SocketConnList_ProcessEvent(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  dmtcp::SocketConnList::instance().processEvent(event, data);
}

void dmtcp_SocketConn_ProcessFdEvent(int event, int arg1, int arg2)
{
  if (event == SYS_close) {
    SocketConnList::instance().processClose(arg1);
  } else if (event == SYS_dup) {
    SocketConnList::instance().processDup(arg1, arg2);
  } else {
    JASSERT(false);
  }
}

static dmtcp::SocketConnList *socketConnList = NULL;
dmtcp::SocketConnList& dmtcp::SocketConnList::instance()
{
  if (socketConnList == NULL) {
    socketConnList = new SocketConnList();
  }
  return *socketConnList;
}

void dmtcp::SocketConnList::preCheckpointDrain()
{
  // First, let all the Connection prepare for drain
  ConnectionList::preCheckpointDrain();

  //this will block until draining is complete
  KernelBufferDrainer::instance().monitorSockets(DRAINER_CHECK_FREQ);
  //handle disconnected sockets
  const vector<ConnectionIdentifier>& discn =
    KernelBufferDrainer::instance().getDisconnectedSockets();
  for (size_t i = 0; i < discn.size(); ++i) {
    const ConnectionIdentifier& id = discn[i];
    TcpConnection *con =
      (TcpConnection*) SocketConnList::instance().getConnection(id);
    JTRACE("recreating disconnected socket") (id);

    //reading from the socket, and taking the error, resulted in an
    //implicit close().
    //we will create a new, broken socket that is not closed
    con->onError();
    break;
  }
}

void dmtcp::SocketConnList::postRestart()
{
  ConnectionRewirer::instance().openRestoreSocket();
  ConnectionList::postRestart();
}

void dmtcp::SocketConnList::registerNSData(bool isRestart)
{
  if (isRestart) {
    ConnectionRewirer::instance().registerNSData();
  }
  ConnectionList::registerNSData(isRestart);
}

void dmtcp::SocketConnList::sendQueries(bool isRestart)
{
  if (isRestart) {
    ConnectionRewirer::instance().sendQueries();
    // Also frees the object when done.
    ConnectionRewirer::instance().doReconnect();
  }
  ConnectionList::sendQueries(isRestart);
}

void dmtcp::SocketConnList::refill(bool isRestart)
{
  KernelBufferDrainer::instance().refillAllSockets();
  ConnectionList::refill(isRestart);
}

void dmtcp::SocketConnList::scanForPreExisting()
{
  // FIXME: Detect stdin/out/err fds to detect duplicates.
  dmtcp::vector<int> fds = jalib::Filesystem::ListOpenFds();
  for (size_t i = 0; i < fds.size(); ++i) {
    int fd = fds[i];
    if (!Util::isValidFd(fd)) continue;
    if (dmtcp_is_protected_fd(fd)) continue;

    dmtcp::string device = jalib::Filesystem::GetDeviceName(fd);

    JTRACE("scanning pre-existing device") (fd) (device);
    if (device == jalib::Filesystem::GetControllingTerm()) {
    } else if(dmtcp_is_bq_file && dmtcp_is_bq_file(device.c_str())) {
    } else if( fd <= 2 ){
    } else if (Util::strStartsWith(device, "/")) {
    } else {
      JNOTE("found pre-existing socket... will not be restored")
        (fd) (device);
      TcpConnection* con = new TcpConnection(0, 0, 0);
      con->markPreExisting();
      add(fd, con);
    }
  }
}

dmtcp::Connection *dmtcp::SocketConnList::createDummyConnection(int type)
{
  if (type == Connection::TCP) {
    return new TcpConnection();
  } else if (type == Connection::RAW){
    return new RawSocketConnection();
  }
  return NULL;
}
