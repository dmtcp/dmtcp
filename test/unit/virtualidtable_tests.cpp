#include "virtualidtable.h"

#ifdef ASSERT_EQ
# undef ASSERT_EQ
#endif
#ifdef ASSERT_TRUE
# undef ASSERT_TRUE
#endif

#include "unit_test.h"

#include <cstdlib>

namespace jalib {

void *
JAllocDispatcher::allocate(size_t n)
{
  return std::malloc(n);
}

void
JAllocDispatcher::deallocate(void *ptr, size_t)
{
  std::free(ptr);
}

} // namespace jalib

extern "C" void
DmtcpMutexInit(DmtcpMutex *mutex, DmtcpMutexType type)
{
  mutex->type = type;
  mutex->futex = 0;
  mutex->owner = 0;
  mutex->count = 0;
}

extern "C" int
DmtcpMutexLock(DmtcpMutex *mutex)
{
  mutex->owner = 1;
  mutex->count++;
  return 0;
}

extern "C" int
DmtcpMutexUnlock(DmtcpMutex *mutex)
{
  mutex->owner = 0;
  mutex->count = 0;
  return 0;
}

namespace {

void virtualIdTableAllocatesAndResolvesIds()
{
  dmtcp::VirtualIdTable<int> table("unit", 100, 3, 3);
  int id = 0;

  ASSERT_TRUE(table.getNewVirtualId(&id));
  ASSERT_EQ(id, 101);

  table.updateMapping(id, 501);

  ASSERT_TRUE(table.virtualIdExists(id));
  ASSERT_TRUE(!table.virtualIdExists(999));
  ASSERT_TRUE(table.realIdExists(501));
  ASSERT_EQ(table.virtualToReal(id), 501);
  ASSERT_EQ(table.realToVirtual(501), id);
}

void virtualIdTableReusesIdsAfterClear()
{
  dmtcp::VirtualIdTable<int> table("unit", 200, 3, 3);
  int first = 0;
  int second = 0;

  ASSERT_TRUE(table.getNewVirtualId(&first));
  table.updateMapping(first, 601);
  ASSERT_TRUE(table.getNewVirtualId(&second));
  table.updateMapping(second, 602);
  ASSERT_EQ(table.size(), static_cast<size_t>(2));

  table.clear();

  int next = 0;
  ASSERT_TRUE(table.getNewVirtualId(&next));
  ASSERT_EQ(next, 201);
}

} // namespace

extern const dmtcp_test::TestCase virtualIdTableTests[] = {
  {"VirtualIdTable allocates and resolves ids",
   virtualIdTableAllocatesAndResolvesIds},
  {"VirtualIdTable reuses ids after clear", virtualIdTableReusesIdsAfterClear},
};

extern const size_t virtualIdTableTestCount =
  sizeof(virtualIdTableTests) / sizeof(virtualIdTableTests[0]);
