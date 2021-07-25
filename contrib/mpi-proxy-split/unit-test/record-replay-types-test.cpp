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

class TypesTests : public CppUnit::TestFixture
{
  private:
    int _count;
    int *_array;
    const size_t len = 100;

  public:
    void setUp()
    {
      int flag = 0;
      this->_count = 10;
      this->_array = new int[len];
      if (MPI_Initialized(&flag) == MPI_SUCCESS && !flag) {
        MPI_Init(NULL, NULL);
      }
    }

    void tearDown()
    {
      if (this->_array) {
        delete this->_array;
      }
      CLEAR_LOG();
      // MPI_Finalize();
    }

    void testTypeContiguous()
    {
      MPI_Datatype type = MPI_INT;
      MPI_Datatype real1 = MPI_DATATYPE_NULL;

      CPPUNIT_ASSERT(VIRTUAL_TO_REAL_TYPE(type) == MPI_INT);
      CPPUNIT_ASSERT(VIRTUAL_TO_REAL_TYPE(real1) == MPI_DATATYPE_NULL);

      CPPUNIT_ASSERT(MPI_Type_contiguous(_count, type, &real1) ==
                     MPI_SUCCESS);
      CPPUNIT_ASSERT(real1 != MPI_DATATYPE_NULL);
      MPI_Datatype virtType = ADD_NEW_TYPE(real1);
      CPPUNIT_ASSERT(VIRTUAL_TO_REAL_TYPE(virtType) == real1);

      CPPUNIT_ASSERT(LOG_CALL(restoreTypes, Type_contiguous,
                              &_count, &type, &virtType) != NULL);
      CPPUNIT_ASSERT(RESTORE_MPI_STATE() == MPI_SUCCESS);

      CPPUNIT_ASSERT(VIRTUAL_TO_REAL_TYPE(virtType) != real1);
    }

    void testTypeCommit()
    {
      MPI_Datatype type = MPI_INT;
      MPI_Datatype real1 = MPI_DATATYPE_NULL;

      CPPUNIT_ASSERT(VIRTUAL_TO_REAL_TYPE(type) == MPI_INT);
      CPPUNIT_ASSERT(VIRTUAL_TO_REAL_TYPE(real1) == MPI_DATATYPE_NULL);

      // Ask MPI for a new datatype
      CPPUNIT_ASSERT(MPI_Type_contiguous(_count, type, &real1) ==
                     MPI_SUCCESS);
      CPPUNIT_ASSERT(real1 != MPI_DATATYPE_NULL);
      MPI_Datatype virtType = ADD_NEW_TYPE(real1);
      CPPUNIT_ASSERT(VIRTUAL_TO_REAL_TYPE(virtType) == real1);
      CPPUNIT_ASSERT(LOG_CALL(restoreTypes, Type_contiguous,
                              &_count, &type, &virtType) != NULL);

      // Commit the new datatype
      CPPUNIT_ASSERT(MPI_Type_commit(&real1) == MPI_SUCCESS);
      CPPUNIT_ASSERT(LOG_CALL(restoreTypes, Type_commit, &real1) != NULL);
      CPPUNIT_ASSERT(VIRTUAL_TO_REAL_TYPE(virtType) == real1);
      int size = -1;
      CPPUNIT_ASSERT(MPI_Type_size(real1, &size) == MPI_SUCCESS);
      CPPUNIT_ASSERT(size == _count * sizeof(*_array));

      // Replay the log
      CPPUNIT_ASSERT(RESTORE_MPI_STATE() == MPI_SUCCESS);

      // Verify after replaying the log
      MPI_Datatype real2 = VIRTUAL_TO_REAL_TYPE(virtType);
      CPPUNIT_ASSERT(real2 != real1);
      CPPUNIT_ASSERT(MPI_Type_size(real2, &size) == MPI_SUCCESS);
      CPPUNIT_ASSERT(size == _count * sizeof(*_array));
    }

    CPPUNIT_TEST_SUITE(TypesTests);
    CPPUNIT_TEST(testTypeContiguous);
    CPPUNIT_TEST(testTypeCommit);
    CPPUNIT_TEST_SUITE_END();
};

int
main(int argc, char **argv)
{
  CppUnit::TextUi::TestRunner runner;
  runner.addTest(TypesTests::suite());
  return runner.run("", false, true, false) ? 0 : -1;
}
