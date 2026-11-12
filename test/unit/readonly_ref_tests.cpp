#include <cstddef>
#include <cstdlib>
#include <optional>
#include <string>

#include "readonly_refs.h"
#include "unit_test.h"

namespace
{

using namespace dmtcp::ReadOnlyRefs;

class SavedEnvironment
{
  public:
  explicit SavedEnvironment(const char *name) : name_(name)
  {
    const char *value = getenv(name);
    if (value != NULL) {
      value_ = value;
    }
    unsetenv(name);
  }

  ~SavedEnvironment()
  {
    if (value_) {
      setenv(name_, value_->c_str(), 1);
    } else {
      unsetenv(name_);
    }
  }

  private:
  const char *name_;
  std::optional<std::string> value_;
};

void
globMatchesPathsAndClasses()
{
  ASSERT_TRUE(glob("/a/*.bin", "/a/nested/model.bin"));
  ASSERT_TRUE(glob("/a/file?.[a-z]", "/a/file1.x"));
  ASSERT_TRUE(!glob("/a/file?.[a-z]", "/a/file11.x"));
  ASSERT_TRUE(glob("/a/[!0-9]", "/a/x"));
  ASSERT_TRUE(!glob("/a/[!0-9]", "/a/3"));
  ASSERT_TRUE(glob("/a/[]]", "/a/]"));
  ASSERT_TRUE(glob("/a/[", "/a/["));
}

void
directoryMatchesRespectPathBoundaries()
{
  ASSERT_TRUE(matches("/assets/model.bin", "/unused\n/assets"));
  ASSERT_TRUE(!matches("/assets-old/model.bin", "/assets"));
  ASSERT_TRUE(matches("/assets/model.bin", "/assets/"));
  ASSERT_TRUE(matches("/a", "/"));
}

void
selectionRequiresOptInAndExcludesWin()
{
  SavedEnvironment enabled(ENV_VAR_EXTERNALIZE_RO);
  SavedEnvironment include(ENV_VAR_RO_INCLUDE);
  SavedEnvironment exclude(ENV_VAR_RO_EXCLUDE);

  ASSERT_TRUE(!selected("/a/lib.so.1"));
  setenv(ENV_VAR_EXTERNALIZE_RO, "1", 1);
  ASSERT_TRUE(selected("/a/lib.so.1"));
  ASSERT_TRUE(!selected("/a/model.bin"));
  setenv(ENV_VAR_RO_INCLUDE, "/a/*.bin\n/b", 1);
  ASSERT_TRUE(selected("/a/model.bin"));
  ASSERT_TRUE(selected("/b/nested/model.data"));
  setenv(ENV_VAR_RO_EXCLUDE, "/a/*.so*\n/a/model.bin", 1);
  ASSERT_TRUE(!selected("/a/lib.so.1"));
  ASSERT_TRUE(!selected("/a/model.bin"));
  ASSERT_TRUE(selected("/b/nested/model.data"));
  setenv(ENV_VAR_RO_EXCLUDE, "/", 1);
  ASSERT_TRUE(!selected("/b/nested/model.data"));
}

} // namespace

extern const dmtcp_test::TestCase readOnlyRefTests[] = {
  { "read-only reference globs match paths and classes",
    globMatchesPathsAndClasses },
  { "read-only reference directories respect boundaries",
    directoryMatchesRespectPathBoundaries },
  { "read-only references require opt-in and excludes win",
    selectionRequiresOptInAndExcludesWin },
};

extern const size_t readOnlyRefTestCount =
  sizeof(readOnlyRefTests) / sizeof(readOnlyRefTests[0]);
