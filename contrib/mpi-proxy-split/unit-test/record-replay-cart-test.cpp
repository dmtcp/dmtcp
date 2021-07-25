#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/ui/text/TestRunner.h>

#include <mpi.h>
#include <string.h>

#include "record-replay.h"
#include "virtual-ids.h"

#undef DMTCP_PLUGIN_ENABLE_CKPT
#undef DMTCP_PLUGIN_DISABLE_CKPT
#undef JUMP_TO_LOWER_HALF
#undef RETURN_TO_UPPER_HALF

using namespace dmtcp_mpi;

class CartTests : public CppUnit::TestFixture
{
  private:
    MPI_Comm _comm;
    int _ndims;
    int *_dims;
    int *_periods;
    int *_coords;
    int _reorder;

  public:
    void setUp()
    {
      int flag = 0;
      this->_comm = MPI_COMM_WORLD;
      this->_ndims = 1;
      this->_dims = new int[this->_ndims];
      this->_periods = new int[this->_ndims];
      this->_coords = new int[this->_ndims];
      this->_reorder = 0;
      if (MPI_Initialized(&flag) == MPI_SUCCESS && !flag) {
        MPI_Init(NULL, NULL);
      }
      this->_dims[0] = 1;
      this->_periods[0] = 0;
      this->_coords[0] = 0;
    }

    void tearDown()
    {
      if (this->_dims) {
        delete this->_dims;
      }
      if (this->_periods) {
        delete this->_periods;
      }
      CLEAR_LOG();
      // MPI_Finalize();
    }

    void testCartCreate()
    {
      // Create a new cart comm
      MPI_Comm real1 = MPI_COMM_NULL;
      int newrank1 = -1;
      CPPUNIT_ASSERT(MPI_Cart_create(_comm, _ndims, _dims, _periods,
                                     _reorder, &real1) == MPI_SUCCESS);
      CPPUNIT_ASSERT(real1 != MPI_COMM_NULL);
      MPI_Comm virtComm = ADD_NEW_COMM(real1);
      CPPUNIT_ASSERT(VIRTUAL_TO_REAL_COMM(virtComm) == real1);
      CPPUNIT_ASSERT(REAL_TO_VIRTUAL_COMM(real1) == virtComm);
      CPPUNIT_ASSERT(MPI_Cart_map(real1, _ndims, _dims,
                                  _periods, &newrank1) == MPI_SUCCESS);
      CPPUNIT_ASSERT(MPI_Cart_get(real1, _ndims, _dims,
                                  _periods, _coords) == MPI_SUCCESS);
      // Log the call
      FncArg ds = CREATE_LOG_BUF(_dims, _ndims);
      FncArg ps = CREATE_LOG_BUF(_periods, _ndims);
      CPPUNIT_ASSERT(LOG_CALL(restoreCarts, Cart_create, &_comm, &_ndims,
                              &ds, &ps, &_reorder, &virtComm) != NULL);
      // Replay the call
      CPPUNIT_ASSERT(RESTORE_MPI_STATE() == MPI_SUCCESS);
      MPI_Comm real2 = VIRTUAL_TO_REAL_COMM(virtComm);
      CPPUNIT_ASSERT(real1 != real2);

      // Test the state after replay
      int newrank2 = -1;
      int dims[_ndims], periods[_ndims], coords[_ndims];
      CPPUNIT_ASSERT(MPI_Cart_get(real2, _ndims, dims,
                                  periods, coords) == MPI_SUCCESS);
      CPPUNIT_ASSERT(memcmp(dims, _dims, _ndims) == 0);
      CPPUNIT_ASSERT(memcmp(periods, _periods, _ndims) == 0);
      CPPUNIT_ASSERT(memcmp(coords, _coords, _ndims) == 0);
      CPPUNIT_ASSERT(MPI_Cart_rank(real2, coords, &newrank2) == MPI_SUCCESS);
      CPPUNIT_ASSERT(newrank1 == newrank2);
    }

    void testCartMap()
    {
      int newrank1 = -1;
      CPPUNIT_ASSERT(MPI_Cart_map(_comm, _ndims, _dims,
                                  _periods, &newrank1) == MPI_SUCCESS);
      CPPUNIT_ASSERT(newrank1 != -1);
      FncArg ds = CREATE_LOG_BUF(_dims, _ndims * sizeof(int));
      FncArg ps = CREATE_LOG_BUF(_periods, _ndims * sizeof(int));
      CPPUNIT_ASSERT(LOG_CALL(restoreCarts, Cart_map, &_comm, &_ndims,
                              &ds, &ps, &newrank1) != NULL);
      CPPUNIT_ASSERT(RESTORE_MPI_STATE() == MPI_SUCCESS);
      // TODO: Not sure how to test that the mapping is still there
    }

    void testCartSub()
    {
      // TODO:
    }

    CPPUNIT_TEST_SUITE(CartTests);
    CPPUNIT_TEST(testCartCreate);
    CPPUNIT_TEST(testCartMap);
    CPPUNIT_TEST(testCartSub);
    CPPUNIT_TEST_SUITE_END();
};

int
main(int argc, char **argv)
{
  CppUnit::TextUi::TestRunner runner;
  runner.addTest(CartTests::suite());
  return runner.run("", false, true, false) ? 0 : -1;
}
