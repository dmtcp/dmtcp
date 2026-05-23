#ifndef DMTCP_BUILTINPLUGINS_H
#define DMTCP_BUILTINPLUGINS_H

namespace dmtcp
{
enum BuiltinPluginId {
  BUILTIN_PLUGIN_ALLOC = 0,
  BUILTIN_PLUGIN_DL,
  BUILTIN_PLUGIN_IPC,
  BUILTIN_PLUGIN_SVIPC,
  BUILTIN_PLUGIN_TIMER,
  BUILTIN_PLUGIN_PID,
  BUILTIN_PLUGIN_COUNT
};

void initializeBuiltinPluginState();
bool builtinPluginEnabled(BuiltinPluginId id);
const char *builtinPluginName(BuiltinPluginId id);
const char *builtinPluginEnvName(BuiltinPluginId id);
}

#endif
