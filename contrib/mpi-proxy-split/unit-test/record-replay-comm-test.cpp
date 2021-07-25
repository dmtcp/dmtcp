#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/ui/text/TestRunner.h>

#include <mpi.h>

#include "record-replay.h"
#include "virtual-ids.h"

#undef DMTCP_PLUGIN_ENABLE_CKPT
#undef DMTCP_PLUGIN_DISABLE_CKPT
#undef JUMP_TO_LOWER_HALF
#undef RETURN_TO_UPPER_HALF

using namespace dmtcp_mpi;

class CommTests : public CppUnit::TestFixture
{
  private:
    MPI_Comm _comm;
    MPI_Comm _virtComm;

  public:
    void setUp()
    {
      int flag = 0;
      this->_comm = MPI_COMM_WORLD;
      this->_virtComm = -1;
      if (MPI_Initialized(&flag) == MPI_SUCCESS && !flag) {
        MPI_Init(NULL, NULL);
      }
    }

    void tearDown()
    {
      CLEAR_LOG();
      // MPI_Finalize();
    }

    void testCommDup()
    {
      CPPUNIT_ASSERT(VIRTUAL_TO_REAL_COMM(_comm) == MPI_COMM_WORLD);
      MPI_Comm real1 = MPI_COMM_NULL;
      // Create Comm dup
      CPPUNIT_ASSERT(MPI_Comm_dup(_comm, &real1) == MPI_SUCCESS);
      CPPUNIT_ASSERT(real1 != MPI_COMM_NULL);
      // Add it to virtual table
      _virtComm = ADD_NEW_COMM(real1);
      CPPUNIT_ASSERT(_virtComm != -1);
      MPI_Comm oldvirt = _virtComm;
      CPPUNIT_ASSERT(VIRTUAL_TO_REAL_COMM(_comm) == MPI_COMM_WORLD);
      // Log the call
      LOG_CALL(restoreComms, Comm_dup, &_comm, &_virtComm);
      // Replay the call
      CPPUNIT_ASSERT(RESTORE_MPI_STATE() == MPI_SUCCESS);
      // Verify state after replay
      CPPUNIT_ASSERT(_virtComm == oldvirt);
      CPPUNIT_ASSERT(VIRTUAL_TO_REAL_COMM(_virtComm) != real1);
    }

    void testCommSplit()
    {
      CPPUNIT_ASSERT(VIRTUAL_TO_REAL_COMM(_comm) == MPI_COMM_WORLD);
      MPI_Comm real1 = MPI_COMM_NULL;
      // Create Comm split
      int color = 0;
      int key = 0;
      CPPUNIT_ASSERT(MPI_Comm_split(_comm, color, key, &real1) == MPI_SUCCESS);
      CPPUNIT_ASSERT(real1 != MPI_COMM_NULL);
      // Add it to virtual table
      _virtComm = ADD_NEW_COMM(real1);
      CPPUNIT_ASSERT(_virtComm != -1);
      MPI_Comm oldvirt = _virtComm;
      LOG_CALL(restoreComms, Comm_split, &_comm, &color, &key, &_virtComm);
      // Replay the call
      CPPUNIT_ASSERT(RESTORE_MPI_STATE() == MPI_SUCCESS);
      // Verify state after replay
      CPPUNIT_ASSERT(_virtComm == oldvirt);
      CPPUNIT_ASSERT(VIRTUAL_TO_REAL_COMM(_virtComm) != real1);
    }

    void testCommCreate()
    {
      CPPUNIT_ASSERT(VIRTUAL_TO_REAL_COMM(_comm) == MPI_COMM_WORLD);
      MPI_Comm real1 = MPI_COMM_NULL;
      // Create Comm split
      MPI_Group group = -1;
      CPPUNIT_ASSERT(MPI_Comm_group(_comm, &group) == MPI_SUCCESS);
      CPPUNIT_ASSERT(group != -1);
      CPPUNIT_ASSERT(MPI_Comm_create(_comm, group, &real1) == MPI_SUCCESS);
      CPPUNIT_ASSERT(real1 != MPI_COMM_NULL);
      // Add it to virtual table
      _virtComm = ADD_NEW_COMM(real1);
      CPPUNIT_ASSERT(_virtComm != -1);
      MPI_Comm oldvirt = _virtComm;
      LOG_CALL(restoreComms, Comm_create, &_comm, &group, &_virtComm);
      // Replay the call
      CPPUNIT_ASSERT(RESTORE_MPI_STATE() == MPI_SUCCESS);
      // Verify state after replay
      CPPUNIT_ASSERT(_virtComm == oldvirt);
      CPPUNIT_ASSERT(VIRTUAL_TO_REAL_COMM(_virtComm) != real1);
    }

    CPPUNIT_TEST_SUITE(CommTests);
    CPPUNIT_TEST(testCommDup);
    CPPUNIT_TEST(testCommSplit);
    CPPUNIT_TEST(testCommCreate);
    CPPUNIT_TEST_SUITE_END();
};

int
main(int argc, char **argv)
{
  CppUnit::TextUi::TestRunner runner;
  runner.addTest(CommTests::suite());
  return runner.run("", false, true, false) ? 0 : -1;
}
