#include "unit_test.h"

#include "dmtcp.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <pthread.h>
#include <type_traits>

namespace {

void ckptHeaderIsFixedSize()
{
  ASSERT_EQ(sizeof(DmtcpCkptHeader), static_cast<size_t>(4096));
  ASSERT_EQ(offsetof(DmtcpCkptHeader, ckptSignature), static_cast<size_t>(0));
}

void ckptHeaderKeepsRestartFieldsPlain()
{
  ASSERT_TRUE(std::is_standard_layout_v<DmtcpCkptHeader>);
  ASSERT_TRUE(std::is_trivial_v<DmtcpCkptHeader>);
  ASSERT_TRUE(std::is_standard_layout_v<MemRegion>);
  ASSERT_TRUE(std::is_trivial_v<MemRegion>);
}

void ckptSignatureFitsHeaderField()
{
  ASSERT_TRUE(sizeof(DMTCP_CKPT_SIGNATURE) <=
              sizeof(DmtcpCkptHeader{}.ckptSignature));
}

void pluginDescriptorKeepsPlainAbiShape()
{
  ASSERT_TRUE(std::is_standard_layout_v<DmtcpPluginDescriptor_t>);
  ASSERT_TRUE(std::is_trivial_v<DmtcpPluginDescriptor_t>);
  ASSERT_TRUE(std::strlen(DMTCP_PLUGIN_API_VERSION) > 0);
}

void pthreadEventDataCarriesThreadIds()
{
  DmtcpEventData_t data{};
  data.pthreadInfo.pthread = pthread_self();
  data.pthreadInfo.tid = 42;

  ASSERT_EQ(sizeof(data.pthreadInfo.pthread), sizeof(pthread_t));
  ASSERT_EQ(data.pthreadInfo.tid, static_cast<pid_t>(42));
}

} // namespace

extern const dmtcp_test::TestCase dmtcpHeaderTests[] = {
  {"DmtcpCkptHeader remains fixed size", ckptHeaderIsFixedSize},
  {"DmtcpCkptHeader restart fields stay plain", ckptHeaderKeepsRestartFieldsPlain},
  {"checkpoint signature fits header field", ckptSignatureFitsHeaderField},
  {"plugin descriptor keeps plain ABI shape", pluginDescriptorKeepsPlainAbiShape},
  {"pthread event data carries thread ids", pthreadEventDataCarriesThreadIds},
};

extern const size_t dmtcpHeaderTestCount =
  sizeof(dmtcpHeaderTests) / sizeof(dmtcpHeaderTests[0]);
