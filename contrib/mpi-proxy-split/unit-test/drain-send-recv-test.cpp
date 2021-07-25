#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/ui/text/TestRunner.h>

#include <mpi.h>
#include <string.h>

#include "dmtcp.h"
#include "drain_send_recv_packets.h"
#include "libproxy.h"
#include "lookup_service.h"
#include "mpi_copybits.h"
#include "split_process.h"
#include "virtual-ids.h"

#undef DMTCP_PLUGIN_ENABLE_CKPT
#undef DMTCP_PLUGIN_DISABLE_CKPT
#undef JUMP_TO_LOWER_HALF
#undef RETURN_TO_UPPER_HALF
#undef NEXT_FUNC

using namespace dmtcp_mpi;

static dmtcp::LookupService lsObj;
proxyDlsym_t pdlsym = NULL;
struct LowerHalfInfo_t info;
int g_numMmaps = 0;
MmapInfo_t *g_list = NULL;
MemRange_t *g_range = NULL;
wr_counts_t g_counts = {0};

// Mock DMTCP coordinator APIs

EXTERNC int
dmtcp_send_key_val_pair_to_coordinator(const char *id, const void *key,
                                       uint32_t key_len, const void *val,
                                       uint32_t val_len)
{
  size_t keylen = (size_t)key_len;
  size_t vallen = (size_t)val_len;

  lsObj.addKeyValue(id, key, key_len, val, vallen);
  return 1;
}

// On input, val points to a buffer in user memory and *val_len is the maximum
// size of that buffer (the memory allocated by user).
// On output, we copy data to val, and set *val_len to the actual buffer size
// (to the size of the data that we copied to the user buffer).
EXTERNC int
dmtcp_send_query_to_coordinator(const char *id,
                                const void *key, uint32_t key_len,
                                void *val, uint32_t *val_len)
{
  void *buf = NULL;
  size_t keylen = (size_t)key_len;
  size_t vallen = (size_t)val_len;

  lsObj.query(id, key, keylen, &buf, &vallen);
  *val_len = (uint32_t)vallen;
  if (buf) {
    memcpy(val, buf, *val_len);
    delete[] (char *)buf;
  }
  return (int)*val_len;
}

EXTERNC int
dmtcp_send_query_all_to_coordinator(const char *id, void **buf, int *len)
{
  void *val = NULL;
  size_t buflen = 0;

  lsObj.queryAll(id, &val, &buflen);
  *len = (int)buflen;
  if (&val) {
    *buf = JALLOC_HELPER_MALLOC(*len);
    memcpy(*buf, val, *len);
    delete[] (char *)val;
  }
  return 0;
}

EXTERNC const char*
dmtcp_get_computation_id_str(void)
{
  return "dummy-computation";
}

SwitchContext::SwitchContext(unsigned long lowerHalfFs)
{
}

SwitchContext::~SwitchContext()
{
}

class DrainTests : public CppUnit::TestFixture
{
  private:
    MPI_Comm _comm;

  public:
    void setUp()
    {
      int flag = 0;
      this->_comm = MPI_COMM_WORLD;
      if (MPI_Initialized(&flag) == MPI_SUCCESS && !flag) {
        MPI_Init(NULL, NULL);
      }
      resetDrainCounters();
    }

    void tearDown()
    {
      // MPI_Finalize();
    }

    void testSendDrain()
    {
      const int TWO = 2;
      int sbuf = 5;
      int rbuf = 0;
      int flag = 0;
      MPI_Request reqs[TWO] = {0};
      MPI_Status sts[TWO] = {0};
      int size = 0;
      CPPUNIT_ASSERT(MPI_Type_size(MPI_INT, &size) == MPI_SUCCESS);
      for (int i = 0; i < TWO; i++) {
        CPPUNIT_ASSERT(MPI_Isend(&sbuf, 1, MPI_INT, 0,
                                 0, _comm, &reqs[i]) == MPI_SUCCESS);
        addPendingRequestToLog(ISEND_REQUEST, &sbuf, NULL, 1,
                               MPI_INT, 0, 0, _comm, &reqs[i]);
        updateLocalSends();
      }
      getLocalRankInfo();
      registerLocalSendsAndRecvs();
      drainMpiPackets();
      for (int i = 0; i < TWO; i++) {
        CPPUNIT_ASSERT(isServicedRequest(&reqs[i], &flag, &sts[i]));
        CPPUNIT_ASSERT(consumeBufferedPacket(&rbuf, 1, MPI_INT, 0,
                                             0, _comm, &sts[i], size) ==
                       MPI_SUCCESS);
        CPPUNIT_ASSERT(rbuf == sbuf);
      }
    }

    void testSendDrainOnDiffComm()
    {
      const int TWO = 2;
      int sbuf = 5;
      int rbuf = 0;
      int flag = 0;
      MPI_Request reqs[TWO] = {0};
      MPI_Status sts[TWO] = {0};
      int size = 0;
      MPI_Comm newcomm = MPI_COMM_NULL;
      CPPUNIT_ASSERT(MPI_Comm_dup(_comm, &newcomm) == MPI_SUCCESS);
      CPPUNIT_ASSERT(MPI_Type_size(MPI_INT, &size) == MPI_SUCCESS);
      for (int i = 0; i < TWO; i++) {
        CPPUNIT_ASSERT(MPI_Isend(&sbuf, 1, MPI_INT, 0,
                                 0, newcomm, &reqs[i]) == MPI_SUCCESS);
        addPendingRequestToLog(ISEND_REQUEST, &sbuf, NULL, 1,
                               MPI_INT, 0, 0, newcomm, &reqs[i]);
        updateLocalSends();
      }
      getLocalRankInfo();
      registerLocalSendsAndRecvs();
      drainMpiPackets();
      for (int i = 0; i < TWO; i++) {
        int rc;
        CPPUNIT_ASSERT(isServicedRequest(&reqs[i], &flag, &sts[i]));
        CPPUNIT_ASSERT(isBufferedPacket(0, 0, newcomm, &flag, &sts[i], &rc));
        CPPUNIT_ASSERT(consumeBufferedPacket(&rbuf, 1, MPI_INT, 0,
                                             0, newcomm, &sts[i], size) ==
                       MPI_SUCCESS);
        CPPUNIT_ASSERT(rbuf == sbuf);
      }
    }

    void testRecvDrain()
    {
      const int TWO = 2;
      int sbuf = 5;
      int rbuf = 0;
      int flag = 0;
      MPI_Request reqs[TWO] = {0};
      MPI_Status sts[TWO] = {0};
      int size = 0;
      MPI_Comm newcomm = MPI_COMM_NULL;
      CPPUNIT_ASSERT(MPI_Comm_dup(_comm, &newcomm) == MPI_SUCCESS);
      CPPUNIT_ASSERT(MPI_Type_size(MPI_INT, &size) == MPI_SUCCESS);
      for (int i = 0; i < TWO; i++) {
        CPPUNIT_ASSERT(MPI_Irecv(&rbuf, 1, MPI_INT, 0,
                                 0, newcomm, &reqs[i]) == MPI_SUCCESS);
        addPendingRequestToLog(IRECV_REQUEST, NULL, &rbuf, 1,
                               MPI_INT, 0, 0, newcomm, &reqs[i]);
      }
      // Checkpoint
      getLocalRankInfo();
      registerLocalSendsAndRecvs();
      drainMpiPackets();
      // Resume
      for (int i = 0; i < TWO; i++) {
        int rc;
        CPPUNIT_ASSERT(!isServicedRequest(&reqs[i], &flag, &sts[i]));
        CPPUNIT_ASSERT(MPI_Send(&sbuf, 1, MPI_INT,
                                0, 0, newcomm) == MPI_SUCCESS);
        MPI_Wait(&reqs[i], &sts[i]);
        CPPUNIT_ASSERT(rbuf == sbuf);
      }
      // Restart
      verifyLocalInfoOnRestart();
      replayMpiOnRestart();
      // Resume
      for (int i = 0; i < TWO; i++) {
        int rc;
        CPPUNIT_ASSERT(!isServicedRequest(&reqs[i], &flag, &sts[i]));
        CPPUNIT_ASSERT(MPI_Send(&sbuf, 1, MPI_INT,
                                0, 0, newcomm) == MPI_SUCCESS);
        MPI_Wait(&reqs[i], &sts[i]);
        CPPUNIT_ASSERT(rbuf == sbuf);
      }
    }

    CPPUNIT_TEST_SUITE(DrainTests);
    CPPUNIT_TEST(testSendDrain);
    CPPUNIT_TEST(testSendDrainOnDiffComm);
    CPPUNIT_TEST(testRecvDrain);
    CPPUNIT_TEST_SUITE_END();
};

int
main(int argc, char **argv, char **envp)
{
  CppUnit::TextUi::TestRunner runner;
  runner.addTest(DrainTests::suite());
  return runner.run("", false, true, false) ? 0 : -1;
}
