#include "processinfo.h"

#ifdef ASSERT_EQ
# undef ASSERT_EQ
#endif

#include "unit_test.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
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

void processInfoDoesNotInheritBootstrapHeader()
{
  ASSERT_TRUE((!std::is_base_of_v<DmtcpCkptHeader, dmtcp::ProcessInfo>));
}

void processInfoFillsCallerOwnedBootstrapHeader()
{
  using FillFn = void (dmtcp::ProcessInfo::*)(DmtcpCkptHeader *) const;

  ASSERT_TRUE((std::is_same_v<
               decltype(&dmtcp::ProcessInfo::fillCheckpointHeader),
               FillFn>));
}

void ckptSignatureFitsHeaderField()
{
  ASSERT_TRUE(sizeof(DMTCP_CKPT_SIGNATURE) <=
              sizeof(DmtcpCkptHeader{}.ckptSignature));
}

void ckptHeaderIsSelfDescribing()
{
  DmtcpCkptHeader header = {};
  dmtcp_init_ckpt_header_bootstrap(&header);

  ASSERT_EQ(DMTCP_CKPT_HEADER_FORMAT_VERSION, 1u);
  ASSERT_EQ(header.headerSize,
            static_cast<uint32_t>(sizeof(DmtcpCkptHeader)));
  ASSERT_EQ(header.headerVersion, DMTCP_CKPT_HEADER_FORMAT_VERSION);
  ASSERT_EQ(header.wordSize, static_cast<uint32_t>(sizeof(void *)));
  ASSERT_EQ(header.endianMarker, DMTCP_CKPT_ENDIAN_MARKER);
}

void ckptHeaderBootstrapValidation()
{
  DmtcpCkptHeader header = {};
  std::strcpy(header.ckptSignature, DMTCP_CKPT_SIGNATURE);
  dmtcp_init_ckpt_header_bootstrap(&header);

  ASSERT_TRUE(dmtcp_ckpt_header_has_valid_bootstrap(&header) != 0);
  ASSERT_TRUE(dmtcp_ckpt_header_has_valid_bootstrap(nullptr) == 0);

  header.ckptSignature[0] = 'X';
  ASSERT_TRUE(dmtcp_ckpt_header_has_valid_bootstrap(&header) == 0);

  std::strcpy(header.ckptSignature, DMTCP_CKPT_SIGNATURE);
  header.headerVersion += 1;
  ASSERT_TRUE(dmtcp_ckpt_header_has_valid_bootstrap(&header) == 0);
}

void ckptHeaderRejectsNonZeroReservedPadding()
{
  DmtcpCkptHeader header = {};
  std::strcpy(header.ckptSignature, DMTCP_CKPT_SIGNATURE);
  dmtcp_init_ckpt_header_bootstrap(&header);

  header.padding[0] = 1;

  ASSERT_TRUE(dmtcp_ckpt_header_has_valid_bootstrap(&header) == 0);
}

void ckptSignatureRejectsOldLayout()
{
  ASSERT_TRUE(std::strstr(DMTCP_CKPT_SIGNATURE, "v5.0") != nullptr);

  DmtcpCkptHeader header = {};
  std::strcpy(header.ckptSignature, "DMTCP_CHECKPOINT_IMAGE_v4.0\n");
  dmtcp_init_ckpt_header_bootstrap(&header);

  ASSERT_TRUE(dmtcp_ckpt_header_has_valid_bootstrap(&header) == 0);
}

void pluginDescriptorKeepsPlainAbiShape()
{
  ASSERT_TRUE(std::is_standard_layout_v<DmtcpPluginDescriptor_t>);
  ASSERT_TRUE(std::is_trivial_v<DmtcpPluginDescriptor_t>);
  ASSERT_TRUE(std::strlen(DMTCP_PLUGIN_API_VERSION) > 0);
}

} // namespace

extern const dmtcp_test::TestCase dmtcpHeaderTests[] = {
  {"DmtcpCkptHeader remains fixed size", ckptHeaderIsFixedSize},
  {"DmtcpCkptHeader restart fields stay plain", ckptHeaderKeepsRestartFieldsPlain},
  {"ProcessInfo does not inherit bootstrap header",
   processInfoDoesNotInheritBootstrapHeader},
  {"ProcessInfo fills caller-owned bootstrap header",
   processInfoFillsCallerOwnedBootstrapHeader},
  {"checkpoint signature fits header field", ckptSignatureFitsHeaderField},
  {"checkpoint header is self describing", ckptHeaderIsSelfDescribing},
  {"checkpoint header validates bootstrap fields", ckptHeaderBootstrapValidation},
  {"checkpoint header rejects non-zero reserved padding",
   ckptHeaderRejectsNonZeroReservedPadding},
  {"checkpoint signature rejects old layout", ckptSignatureRejectsOldLayout},
  {"plugin descriptor keeps plain ABI shape", pluginDescriptorKeepsPlainAbiShape},
};

extern const size_t dmtcpHeaderTestCount =
  sizeof(dmtcpHeaderTests) / sizeof(dmtcpHeaderTests[0]);
