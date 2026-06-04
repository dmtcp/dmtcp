#ifndef DMTCP_ASSERT_BUFFER_H
#define DMTCP_ASSERT_BUFFER_H

#include <cstddef>

#define DMTCP_ASSERT_BUFFER_SIZE 4096

namespace dmtcp {
inline constexpr size_t kAssertBufferSize = DMTCP_ASSERT_BUFFER_SIZE;
}

#endif // DMTCP_ASSERT_BUFFER_H
