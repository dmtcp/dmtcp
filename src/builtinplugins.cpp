#include "builtinplugins.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "constants.h"
#include "jassert.h"

namespace dmtcp
{
struct BuiltinPluginState {
  const char *name;
  const char *envName;
  bool enabled;
};

static BuiltinPluginState builtinPlugins[BUILTIN_PLUGIN_COUNT] = {
  { "alloc", ENV_VAR_ALLOC_PLUGIN, true },
  { "dl", ENV_VAR_DL_PLUGIN, true },
  { "ipc", ENV_VAR_IPC_PLUGIN, true },
  { "svipc", ENV_VAR_SVIPC_PLUGIN, true },
  { "timer", ENV_VAR_TIMER_PLUGIN, true },
  { "pid", ENV_VAR_PID_PLUGIN, true }
};

static bool disableAllPlugins = false;
static pthread_once_t initializeBuiltinPluginStateOnceControl =
  PTHREAD_ONCE_INIT;

static bool
readBooleanEnv(const char *envName, bool defaultValue)
{
  const char *value = getenv(envName);
  if (value == NULL) {
    return defaultValue;
  }

  if (strcmp(value, "1") == 0) {
    return true;
  }

  if (strcmp(value, "0") == 0) {
    return false;
  }

  JASSERT(false) (envName) (value)
  .Text("Invalid value for the environment variable.");
  return defaultValue;
}

static void
assertValidBuiltinPluginId(BuiltinPluginId id)
{
  JASSERT(id >= 0 && id < BUILTIN_PLUGIN_COUNT) (id)
  .Text("Invalid built-in plugin id.");
}

static void
initializeBuiltinPluginStateOnce()
{
  disableAllPlugins = readBooleanEnv(ENV_VAR_DISABLE_ALL_PLUGINS, false);

  for (int i = 0; i < BUILTIN_PLUGIN_COUNT; i++) {
    BuiltinPluginId id = static_cast<BuiltinPluginId>(i);
    builtinPlugins[i].enabled =
      readBooleanEnv(builtinPluginEnvName(id), true);
  }
}

void
initializeBuiltinPluginState()
{
  int rc = pthread_once(&initializeBuiltinPluginStateOnceControl,
                        initializeBuiltinPluginStateOnce);
  JASSERT(rc == 0) (rc)
  .Text("Failed to initialize built-in plugin state.");
}

bool
builtinPluginEnabled(BuiltinPluginId id)
{
  assertValidBuiltinPluginId(id);
  initializeBuiltinPluginState();

  return !disableAllPlugins && builtinPlugins[id].enabled;
}

const char *
builtinPluginName(BuiltinPluginId id)
{
  assertValidBuiltinPluginId(id);

  return builtinPlugins[id].name;
}

const char *
builtinPluginEnvName(BuiltinPluginId id)
{
  assertValidBuiltinPluginId(id);

  return builtinPlugins[id].envName;
}
}
