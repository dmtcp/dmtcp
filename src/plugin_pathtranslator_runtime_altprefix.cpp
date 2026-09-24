/****************************************************************************
 *   Copyright (C) 2006-2013 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 *  This file is part of DMTCP.                                             *
 *                                                                          *
 *  DMTCP is free software: you can redistribute it and/or                  *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP is distributed in the hope that it will be useful,                *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

// ============================================================================
// VARIANT: runtime-configured alternate prefix
//
// This variant of the auto path-mapping plugin reads the alternate filesystem
// prefix from the DMTCP_CHECKPOINT_DIR_ALT_PREFIX environment variable at
// runtime, so no recompilation is needed to change it.
//
// Usage:
//   export DMTCP_CHECKPOINT_DIR_ALT_PREFIX="/your/alternate/prefix/"
//
// If DMTCP_CHECKPOINT_DIR_ALT_PREFIX is not set, only the primary mapping
// (DMTCP_CHECKPOINT_DIR -> cwd) is registered.
//
// See plugin_pathtranslator.cpp for the compile-time-configured variant.
// ============================================================================

// Enable auto path-mapping mode.
// When defined, the plugin automatically derives path mappings from
// DMTCP_CHECKPOINT_DIR at restart time, and saves the current working
// directory to DMTCP_CHECKPOINT_DIR before each checkpoint, instead of
// relying on the user-supplied DMTCP_PATH_MAPPING environment variable.
#define DMTCP_AUTO_PATH_MAPPING

#include <limits.h>  // for PATH_MAX
#include <cstring>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <sstream>

#include "config.h"
#include "dmtcp.h"
#include "jassert.h"
#include "jserialize.h"
#include "util.h"

#ifdef DMTCP_AUTO_PATH_MAPPING
# include <unistd.h>
#endif

// Semicolon delimited list of path mappings of the form
// "/path/a:/path/a_new:/path/b:/path/b_new:/path/b/c:/path/c_new"
#define ENV_PATH_MAPPING   "DMTCP_PATH_MAPPING"
#define MAX_ENV_VAR_SIZE   (16*1024)

#ifdef DMTCP_AUTO_PATH_MAPPING
// Environment variable holding the working directory captured at checkpoint
// time.  Set automatically before each checkpoint; read back on restart to
// reconstruct path mappings.  Format: absolute directory path.
# define ENV_CHECKPOINT_DIR      "DMTCP_CHECKPOINT_DIR"
// Optional runtime env var: alternate filesystem prefix that mirrors the
// same directory tree, for sites with multiple mount points.
// Format: absolute path prefix ending with '/'.
# define ENV_CHECKPOINT_DIR_ALT_PREFIX  "DMTCP_CHECKPOINT_DIR_ALT_PREFIX"
#endif

namespace dmtcp {

static unordered_map<string, string> *pathMapping = nullptr;
alignas(unordered_map<string, string>)
static unsigned char pathMappingStorage[sizeof(unordered_map<string, string>)];

static void populatePathMapping(const char *pathMappingStr)
{
  pathMapping->clear();

  if (!pathMappingStr) {
    return;
  }

  stringstream ss(pathMappingStr);
  string token;

  // Expected format: "old1:new1;old2:new2;..."
  while (std::getline(ss, token, ';')) {
    if (token.empty()) {
      continue;
    }
    std::string::size_type colonIdx = token.find(':');
    JASSERT(colonIdx != std::string::npos)(token).Text("Bad mapping; expect old:new");
    string oldPath = token.substr(0, colonIdx);
    string newPath = token.substr(colonIdx + 1);
    JASSERT(!oldPath.empty());
    JASSERT(!newPath.empty());
    (*pathMapping)[oldPath] = newPath;
  }
}

static void pathTranslator_Init()
{
  if (pathMapping == nullptr) {
    pathMapping = new(pathMappingStorage) unordered_map<string, string>;
#ifndef DMTCP_AUTO_PATH_MAPPING
    populatePathMapping(getenv(ENV_PATH_MAPPING));
#endif
  }

#ifdef DMTCP_AUTO_PATH_MAPPING
  // Derive mappings from DMTCP_CHECKPOINT_DIR:
  //   <DMTCP_CHECKPOINT_DIR>                    -> <cwd>
  //   <alternate-prefix>/<suffix-after-"usr/"> -> <cwd>
  // where the alternate prefix is a secondary filesystem location that
  // mirrors the same tree (runtime-configured via DMTCP_CHECKPOINT_DIR_ALT_PREFIX).
  const char *oldpwd = getenv(ENV_CHECKPOINT_DIR);
  if (oldpwd != nullptr) {
    string oldpath(oldpwd);
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) != nullptr) {
      string newPathMapping = string(oldpwd) + ":" + string(cwd);
      // If an alternate prefix is configured, register that mapping too.
      const char *altPrefix = getenv(ENV_CHECKPOINT_DIR_ALT_PREFIX);
      if (altPrefix != nullptr) {
        auto usrPos = oldpath.find("usr/");
        string suffix = (usrPos != string::npos)
                        ? oldpath.substr(usrPos + 4)
                        : string();
        newPathMapping += ";" + string(altPrefix) + suffix + ":" + string(cwd);
      }
      populatePathMapping(newPathMapping.c_str());
    }
  }
#endif
}

static void pathTranslator_Restart()
{
  pathTranslator_Init();
#ifndef DMTCP_AUTO_PATH_MAPPING
  char *tmp = (char*) JALLOC_MALLOC(MAX_ENV_VAR_SIZE);
  DmtcpGetRestartEnvErr_t ret = dmtcp_get_restart_env(ENV_PATH_MAPPING, tmp, MAX_ENV_VAR_SIZE);
  if (ret == RESTART_ENV_SUCCESS) {
    populatePathMapping(tmp);
  }

  JALLOC_FREE(tmp);
#endif
}

static void
pathTranslator_PrepareForExec(DmtcpEventData_t *data)
{
#ifndef DMTCP_AUTO_PATH_MAPPING
  pathTranslator_Init();
  JASSERT(data != NULL);
  jalib::JBinarySerializeWriterRaw wr("", data->preExec.serializationFd);
  wr.serialize(*pathMapping);
#endif
}

static void
pathTranslator_PostExec(DmtcpEventData_t *data)
{
#ifndef DMTCP_AUTO_PATH_MAPPING
  pathTranslator_Init();
  JASSERT(data != NULL);
  jalib::JBinarySerializeReaderRaw rd("", data->postExec.serializationFd);
  rd.serialize(*pathMapping);
#endif
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
  // TODO (kapil): Investigate if we need to match the longest prefix instead of
  // the first one.
  for (const auto &mapping : *pathMapping) {
    const string &oldp = mapping.first;
    const string &newp = mapping.second;
    if (Util::strStartsWith(virtPath, oldp.c_str())) {
      // boundary check: next char must be '/' or end
      char next = virtPath[oldp.size()];
      if (next == '/' || next == '\0') {
        string suffix = &virtPath[oldp.size()];
        int n = snprintf(data->virtualToRealPath.path,
                         PATH_MAX,
                         "%s%s",
                         newp.c_str(),
                         suffix.c_str());
        JASSERT(n > 0 && n < PATH_MAX).Text("Translated path exceeds PATH_MAX");
      }
    }
  }
}

#ifdef DMTCP_AUTO_PATH_MAPPING
// Save the current working directory to DMTCP_CHECKPOINT_DIR so that
// pathTranslator_Init() can reconstruct the path mapping on restart.
// Also sets DMTCP_SKIP_TRUNCATE_FILE_AT_RESTART to preserve file contents
// across restart.
static void
pathTranslator_PreCheckpoint()
{
  char cwd[PATH_MAX];
  if (getcwd(cwd, sizeof(cwd)) != nullptr) {
    setenv(ENV_CHECKPOINT_DIR, cwd, 1);
  } else {
    JWARNING(false)(JASSERT_ERRNO).Text("Failed to get current working directory");
  }
  setenv("DMTCP_SKIP_TRUNCATE_FILE_AT_RESTART", "1", 1);
}
#endif

void
pathTranslator_EventHook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
  case DMTCP_EVENT_INIT:
    pathTranslator_Init();
    break;

#ifdef DMTCP_AUTO_PATH_MAPPING
  case DMTCP_EVENT_PRECHECKPOINT:
    pathTranslator_PreCheckpoint();
    break;
#endif

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
