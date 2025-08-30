
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>  // for PATH_MAX
#include <stdarg.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <cstring>
#include <cstdlib>

#include "config.h"
#include "dmtcp.h"
#include "jassert.h"
#include "jserialize.h"
#include "util.h"
#include "util.h"

// Semicolon delimited list of path mappings of the form
// "/path/a:/path/a_new:/path/b:/path/b_new:/path/b/c:/path/c_new"
#define ENV_PATH_MAPPING   "DMTCP_PATH_MAPPING"
#define MAX_ENV_VAR_SIZE   (16*1024)

namespace dmtcp {

static unordered_map<string, string> *pathMapping = nullptr;

static void populatePathMapping(const char *pathMappingStr)
{
  pathMapping->clear();

  if (!pathMappingStr) {
    return;
  }

  stringstream ss(pathMappingStr); // Create a stringstream from the string
  string token;

  // Tokenize using ; as a delimiter
  while (std::getline(ss, token, ';')) {
    int colonIdx = token.find(':');
    ASSERT_NE(colonIdx, string::npos);
    string path = token.substr(0, colonIdx);
    string newPath = token.substr(colonIdx + 1);
    JASSERT(!path.empty());
    JASSERT(!newPath.empty());
    pathMapping->insert(make_pair(path, newPath));
  }
}

static void pathTranslator_Init()
{
  if (pathMapping == nullptr) {
    pathMapping = new unordered_map<string, string>();
    populatePathMapping(getenv(ENV_PATH_MAPPING));
  }
}

static void pathTranslator_Restart()
{
  pathTranslator_Init();
  char *tmp = (char*) JALLOC_MALLOC(MAX_ENV_VAR_SIZE);
  DmtcpGetRestartEnvErr_t ret = dmtcp_get_restart_env(ENV_PATH_MAPPING, tmp, MAX_ENV_VAR_SIZE);
  if (ret == RESTART_ENV_SUCCESS) {
    populatePathMapping(tmp);
  }

  JALLOC_FREE(tmp);
}

static void
pathTranslator_PrepareForExec(DmtcpEventData_t *data)
{
  JASSERT(data != NULL);
  jalib::JBinarySerializeWriterRaw wr("", data->preExec.serializationFd);
  wr.serialize(pathMapping);
}

static void
pathTranslator_PostExec(DmtcpEventData_t *data)
{
  pathTranslator_Init();
  JASSERT(data != NULL);
  jalib::JBinarySerializeReaderRaw rd("", data->postExec.serializationFd);
  rd.serialize(pathMapping);
}

/*
 * virtual_to_physical_path - translator virtual to physical path
 *
 * Returns a string for the corresponding physical path to the given
 * virtual path. If no path translation occurred, the given virtual path
 * will simply be returned as a string.
 *
 * Conceptually, an original path prior to the first checkpoint is considered a
 * "virtual path".  After a restart, it will be substituted using the latest
 * list of registered paths.  Hence, a newly registered path to be substituted
 * is a "physical path".  Internally, DMTCP works with the original "virtual
 * path" as the canonical name.  But in any system calls, it must translator the
 * virtual path to the latest "physical path", which will correspond to the
 * current, post-restart filesystem.
 */

static void
pathTranslator_VirtualToReal(DmtcpEventData_t *data)
{
  pathTranslator_Init();
  char *virtPath = data->virtualToRealPath.path;

  // Check to see if prefix of virtPath is present in pathMappings.
  // This is O(n) in the number of mappings. We could make this faster by using
  // a trie or a hash table.
  for (auto &mapping : *pathMapping) {
    if (Util::strStartsWith(virtPath, mapping.first.c_str())) {
      strncpy(data->virtualToRealPath.path, mapping.second.c_str(), PATH_MAX);
      return;
    }
  }
}

void
pathTranslator_EventHook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
  case DMTCP_EVENT_INIT:
    pathTranslator_Init();
    break;

  case DMTCP_EVENT_RESTART:
    pathTranslator_Restart();
    break;

  case DMTCP_EVENT_PRE_EXEC:
    pathTranslator_PrepareForExec(data);
    break;

  case DMTCP_EVENT_POST_EXEC:
    pathTranslator_PostExec(data);
    break;

  case DMTCP_EVENT_VIRTUAL_TO_REAL_PATH:
    pathTranslator_VirtualToReal(data);
    break;

  default:
    break;
  }
}

DmtcpPluginDescriptor_t pathTranslator_plugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "pathTranslator",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "PathTranslator plugin",
  pathTranslator_EventHook
};

DmtcpPluginDescriptor_t
dmtcp_PathTranslator_PluginDescr()
{
  return pathTranslator_plugin;
}

};
