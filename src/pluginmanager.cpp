#include "pluginmanager.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants.h"
#include "coordinatorapi.h"
#include "config.h"
#include "dmtcp.h"
#include "dmtcpalloc.h"
#include "plugininfo.h"
#include "util.h"
#include "util_assert.h"

static dmtcp::PluginManager *pluginManager = NULL;

extern LIB_PRIVATE DmtcpPluginDescriptor_t UniqueCkptPlugin;
extern LIB_PRIVATE DmtcpPluginDescriptor_t sshPlugin;
extern LIB_PRIVATE DmtcpPluginDescriptor_t eventPlugin;
extern LIB_PRIVATE DmtcpPluginDescriptor_t filePlugin;
extern LIB_PRIVATE DmtcpPluginDescriptor_t ptyPlugin;
extern LIB_PRIVATE DmtcpPluginDescriptor_t socketPlugin;
extern LIB_PRIVATE DmtcpPluginDescriptor_t sysvipcPlugin;
extern LIB_PRIVATE DmtcpPluginDescriptor_t timerPlugin;
extern LIB_PRIVATE DmtcpPluginDescriptor_t pidPlugin;
extern LIB_PRIVATE DmtcpPluginDescriptor_t UniquePidPlugin;

extern "C" void
dmtcp_register_plugin(DmtcpPluginDescriptor_t descr)
{
  ASSERT_NOT_NULL(pluginManager);

  pluginManager->registerPlugin(descr);
}

namespace dmtcp
{
extern LIB_PRIVATE DmtcpPluginDescriptor_t syslogPlugin;
extern LIB_PRIVATE DmtcpPluginDescriptor_t rlimitFloatPlugin;
extern LIB_PRIVATE DmtcpPluginDescriptor_t alarmPlugin;
extern LIB_PRIVATE DmtcpPluginDescriptor_t terminalPlugin;
extern LIB_PRIVATE DmtcpPluginDescriptor_t processInfoPlugin;
extern LIB_PRIVATE DmtcpPluginDescriptor_t pathTranslator_plugin;

namespace CoordinatorAPI
{
extern LIB_PRIVATE DmtcpPluginDescriptor_t coordinatorAPIPlugin;
}

struct InternalPluginEntry {
  DmtcpPluginDescriptor_t *descriptor;
  bool enabled;
};

/*
 * Plugin descriptors stay in their owning modules.  PluginManager keeps only
 * registration order and cached enablement state for built-in plugins.
 */
static DmtcpPluginDescriptor_t allocPlugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "ALLOC",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "Allocation wrappers",
  NULL
};

static DmtcpPluginDescriptor_t dlPlugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "DL",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "Dynamic loader wrappers",
  NULL
};

static InternalPluginEntry internalPlugins[] = {
  // Keep UNIQUE_CKPT first.  Its PRECHECKPOINT hook updates the checkpoint
  // directory name, and later plugins should observe that final directory
  // when they serialize or reopen plugin-owned state.
  { &::UniqueCkptPlugin, false },
  { &pathTranslator_plugin, false },
  { &syslogPlugin, false },
  { &rlimitFloatPlugin, false },
  { &alarmPlugin, false },
  { &terminalPlugin, false },
  { &CoordinatorAPI::coordinatorAPIPlugin, false },
  { &processInfoPlugin, false },
  { &::UniquePidPlugin, false },
  { &::sshPlugin, false },
  { &::eventPlugin, false },
  { &::filePlugin, false },
  { &::ptyPlugin, false },
  { &::socketPlugin, false },
  { &::sysvipcPlugin, false },
  { &::timerPlugin, false },
  { &::pidPlugin, false },
  { &allocPlugin, false },
  { &dlPlugin, false }
};

static pthread_once_t internalPluginInitOnce = PTHREAD_ONCE_INIT;
static bool disableAllInternalPlugins = false;

static size_t
numInternalPlugins()
{
  return sizeof(internalPlugins) / sizeof(internalPlugins[0]);
}

static const char *
internalPluginEnvName(const char *pluginName,
                      char *envName,
                      size_t size)
{
  int len = snprintf(envName, size, "DMTCP_%s_PLUGIN", pluginName);
  ASSERT(len > 0 && (size_t)len < size,
         "internal plugin environment variable name is too long: "
         "plugin={} size={} required={}",
         pluginName, size, len);
  return envName;
}

static void
initializeInternalPluginStateOnce()
{
  disableAllInternalPlugins =
    Util::readBooleanEnv(ENV_VAR_DISABLE_ALL_PLUGINS, false);

  for (size_t i = 0; i < numInternalPlugins(); i++) {
    InternalPluginEntry *entry = &internalPlugins[i];
    ASSERT_NOT_NULL(entry->descriptor);
    ASSERT(entry->descriptor->pluginName != nullptr,
           "internal plugin descriptor is missing a plugin name: index={}", i);
    for (size_t j = 0; j < i; j++) {
      ASSERT(strcmp(internalPlugins[j].descriptor->pluginName,
                    entry->descriptor->pluginName) != 0,
             "duplicate internal plugin name: {}",
             entry->descriptor->pluginName);
    }

    char envName[64];
    entry->enabled =
      Util::readBooleanEnv(internalPluginEnvName(entry->descriptor->pluginName,
                                                 envName, sizeof(envName)),
                           true);
  }
}

static void
initializeInternalPluginState()
{
  ASSERT_EQ(0, pthread_once(&internalPluginInitOnce,
                            initializeInternalPluginStateOnce));
}

static InternalPluginEntry *
findInternalPluginEntry(const char *pluginName)
{
  ASSERT(pluginName != nullptr, "invalid internal plugin name");
  for (size_t i = 0; i < numInternalPlugins(); i++) {
    InternalPluginEntry *entry = &internalPlugins[i];
    if (entry->descriptor->pluginName == pluginName ||
        Util::strEquals(entry->descriptor->pluginName, pluginName)) {
      return entry;
    }
  }

  ASSERT(false, "unknown internal plugin name: {}", pluginName);
  return NULL;
}

bool
internalPluginEnabled(const char *pluginName)
{
  initializeInternalPluginState();
  InternalPluginEntry *entry = findInternalPluginEntry(pluginName);

  return !disableAllInternalPlugins && entry->enabled;
}

void
PluginManager::initialize()
{
  if (pluginManager == NULL) {
    pluginManager = new PluginManager();

    // Now initialize plugins.
    // Call into other plugins to have them register with us.
    if (dmtcp_initialize_plugin != NULL) {
      dmtcp_initialize_plugin();
    }
  }
}

PluginManager::PluginManager()
{}

void
PluginManager::registerPlugin(DmtcpPluginDescriptor_t descr)
{
  ASSERT(descr.pluginApiVersion != nullptr &&
         strcmp(descr.pluginApiVersion, DMTCP_PLUGIN_API_VERSION) == 0,
         "incompatible DMTCP plugin API version: plugin_api={} expected={}",
         descr.pluginApiVersion, DMTCP_PLUGIN_API_VERSION);
  PluginInfo *info = new PluginInfo(descr);

  pluginInfos.push_back(info);
}

extern "C" void
dmtcp_initialize_plugin()
{
  initializeInternalPluginState();
  for (size_t i = 0; i < numInternalPlugins(); i++) {
    InternalPluginEntry *entry = &internalPlugins[i];
    if (entry->descriptor->event_hook != NULL &&
        internalPluginEnabled(entry->descriptor->pluginName)) {
      dmtcp_register_plugin(*entry->descriptor);
    }
  }

  void (*fn)() = NEXT_FNC(dmtcp_initialize_plugin);
  if (fn != NULL) {
    (*fn)();
  }
}

void
PluginManager::eventHook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  PluginManager::initialize();

  switch (event) {
  // The following events are processed in the order of plugin registration.
  case DMTCP_EVENT_INIT:
  case DMTCP_EVENT_RUNNING:
  case DMTCP_EVENT_PRE_EXEC:
  case DMTCP_EVENT_POST_EXEC:
  case DMTCP_EVENT_ATFORK_PREPARE:
  case DMTCP_EVENT_VFORK_PREPARE:
  case DMTCP_EVENT_PTHREAD_START:
  case DMTCP_EVENT_OPEN_FD:
  case DMTCP_EVENT_REOPEN_FD:
  case DMTCP_EVENT_CLOSE_FD:
  case DMTCP_EVENT_DUP_FD:
  case DMTCP_EVENT_VIRTUAL_TO_REAL_PATH:
  case DMTCP_EVENT_PRESUSPEND:
  case DMTCP_EVENT_PRECHECKPOINT:

    // The plugins can be thought of as implementing a layered software
    // architecture.  All of the events here occur before writing the checkpoint
    // file.  The plugins are invoked for these events in the natural order.
    // For the resume/restart events below, the plugins are invoked
    // in _reverse_ order.  This is required to support layered software.
    // For an analogous case, see 'man pthread_atfork' with the handlers:
    // (i) prepare, (ii) parent, and (iii) child.
    // Those are analogous to our events for:
    // (i) pre-checkpoint, (ii) resume event, and (iii) restart; respectively.
    for (size_t i = 0; i < pluginManager->pluginInfos.size(); i++) {
      if (pluginManager->pluginInfos[i]->event_hook) {
        pluginManager->pluginInfos[i]->event_hook(event, data);
      }
    }
    break;

  // The following events are processed in reverse order.
  case DMTCP_EVENT_EXIT:
  case DMTCP_EVENT_PTHREAD_EXIT:
  case DMTCP_EVENT_PTHREAD_RETURN:
  case DMTCP_EVENT_ATFORK_PARENT:
  case DMTCP_EVENT_ATFORK_CHILD:
  case DMTCP_EVENT_ATFORK_FAILED:
  case DMTCP_EVENT_VFORK_PARENT:
  case DMTCP_EVENT_VFORK_CHILD:
  case DMTCP_EVENT_VFORK_FAILED:
  case DMTCP_EVENT_REAL_TO_VIRTUAL_PATH:
  case DMTCP_EVENT_RESUME:
  case DMTCP_EVENT_RESTART:
  case DMTCP_EVENT_THREAD_RESUME:

    if (event == DMTCP_EVENT_RESTART) {
      DMTCP_RESTART_PAUSE_WHILE(restartPauseLevel == 5);
    }

    // The plugins are invoked in _reverse_ order during resume/restart.  This
    // is required to support layered software.  See the related comment, above.
    for (int i = pluginManager->pluginInfos.size() - 1; i >= 0; i--) {
      if (pluginManager->pluginInfos[i]->event_hook) {
        pluginManager->pluginInfos[i]->event_hook(event, data);
      }
    }
    break;

    if (event == DMTCP_EVENT_RESTART) {
      DMTCP_RESTART_PAUSE_WHILE(restartPauseLevel == 6);
    }

  default:
    ASSERT(false, "unhandled plugin event: {}", static_cast<int>(event));
  }
}
} // namespace dmtcp {
