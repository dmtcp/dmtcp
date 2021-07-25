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

class GroupTests : public CppUnit::TestFixture
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
    }

    void tearDown()
    {
      // MPI_Finalize();
      CLEAR_LOG();
    }

    void testGroupAPI()
    {
      MPI_Group group = MPI_GROUP_NULL;
      CPPUNIT_ASSERT(MPI_Comm_group(_comm, &group) == MPI_SUCCESS);
      CPPUNIT_ASSERT(group != MPI_GROUP_NULL);
      int size = -1;
      CPPUNIT_ASSERT(MPI_Group_size(group, &size) == MPI_SUCCESS);
      CPPUNIT_ASSERT(size == 1);
    }

    CPPUNIT_TEST_SUITE(GroupTests);
    CPPUNIT_TEST(testGroupAPI);
    CPPUNIT_TEST_SUITE_END();
};

int
main(int argc, char **argv)
{
  CppUnit::TextUi::TestRunner runner;
  runner.addTest(GroupTests::suite());
  return runner.run("", false, true, false) ? 0 : -1;
}
