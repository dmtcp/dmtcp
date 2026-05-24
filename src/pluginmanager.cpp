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
#include "jtimer.h"
#include "plugininfo.h"
#include "util.h"

static dmtcp::PluginManager *pluginManager = NULL;
JTIMER_NOPRINT(ckptWriteTime);

extern "C" void
dmtcp_register_plugin(DmtcpPluginDescriptor_t descr)
{
  JASSERT(pluginManager != NULL);

  pluginManager->registerPlugin(descr);
}

namespace dmtcp
{
DmtcpPluginDescriptor_t dmtcp_Syslog_PluginDescr();
DmtcpPluginDescriptor_t dmtcp_Rlimit_Float_PluginDescr();
DmtcpPluginDescriptor_t dmtcp_Alarm_PluginDescr();
DmtcpPluginDescriptor_t dmtcp_Terminal_PluginDescr();
DmtcpPluginDescriptor_t dmtcp_ProcessInfo_PluginDescr();
DmtcpPluginDescriptor_t dmtcp_PathTranslator_PluginDescr();
DmtcpPluginDescriptor_t dmtcp_SshPlugin_PluginDescr();
DmtcpPluginDescriptor_t dmtcp_EventPlugin_PluginDescr();
DmtcpPluginDescriptor_t dmtcp_FilePlugin_PluginDescr();
DmtcpPluginDescriptor_t dmtcp_PtyPlugin_PluginDescr();
DmtcpPluginDescriptor_t dmtcp_SocketPlugin_PluginDescr();
DmtcpPluginDescriptor_t dmtcp_SysVIPC_PluginDescr();
DmtcpPluginDescriptor_t dmtcp_Timer_PluginDescr();
DmtcpPluginDescriptor_t dmtcp_PidPlugin_PluginDescr();
DmtcpPluginDescriptor_t dmtcp_AllocPlugin_PluginDescr();
DmtcpPluginDescriptor_t dmtcp_DlPlugin_PluginDescr();

typedef DmtcpPluginDescriptor_t (*BuiltinDescriptorFn)();
struct InternalPluginEntry {
  DmtcpInternalPluginId_t id;
  const char *name;
  BuiltinDescriptorFn descriptorFn;
  DmtcpPluginDescriptor_t descriptor;
  bool enabled;
};

static DmtcpPluginDescriptor_t allocPlugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "ALLOC",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "Allocation wrappers",
  NULL
};

DmtcpPluginDescriptor_t
dmtcp_AllocPlugin_PluginDescr()
{
  return allocPlugin;
}

static DmtcpPluginDescriptor_t dlPlugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "DL",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "Dynamic loader wrappers",
  NULL
};

DmtcpPluginDescriptor_t
dmtcp_DlPlugin_PluginDescr()
{
  return dlPlugin;
}

static InternalPluginEntry internalPlugins[] = {
  { INTERNAL_PLUGIN_PATHVIRT, "PATHVIRT", dmtcp_PathTranslator_PluginDescr },
  { INTERNAL_PLUGIN_SYSLOG, "SYSLOG", dmtcp_Syslog_PluginDescr },
  { INTERNAL_PLUGIN_RLIMIT_FLOAT, "RLIMIT_FLOAT",
    dmtcp_Rlimit_Float_PluginDescr },
  { INTERNAL_PLUGIN_ALARM, "ALARM", dmtcp_Alarm_PluginDescr },
  { INTERNAL_PLUGIN_TERMINAL, "TERMINAL", dmtcp_Terminal_PluginDescr },
  { INTERNAL_PLUGIN_COORDINATOR_API, "COORDINATOR_API",
    CoordinatorAPI::pluginDescr },
  { INTERNAL_PLUGIN_PROCESS_INFO, "PROCESS_INFO",
    dmtcp_ProcessInfo_PluginDescr },
  { INTERNAL_PLUGIN_UNIQUE_PID, "UNIQUE_PID", UniquePid::pluginDescr },
  { INTERNAL_PLUGIN_SSH, "SSH", dmtcp_SshPlugin_PluginDescr },
  { INTERNAL_PLUGIN_EVENT, "EVENT", dmtcp_EventPlugin_PluginDescr },
  { INTERNAL_PLUGIN_FILE, "FILE", dmtcp_FilePlugin_PluginDescr },
  { INTERNAL_PLUGIN_PTY, "PTY", dmtcp_PtyPlugin_PluginDescr },
  { INTERNAL_PLUGIN_SOCKET, "SOCKET", dmtcp_SocketPlugin_PluginDescr },
  { INTERNAL_PLUGIN_SVIPC, "SVIPC", dmtcp_SysVIPC_PluginDescr },
  { INTERNAL_PLUGIN_TIMER, "TIMER", dmtcp_Timer_PluginDescr },
  { INTERNAL_PLUGIN_PID, "PID", dmtcp_PidPlugin_PluginDescr },
  { INTERNAL_PLUGIN_ALLOC, "ALLOC", dmtcp_AllocPlugin_PluginDescr },
  { INTERNAL_PLUGIN_DL, "DL", dmtcp_DlPlugin_PluginDescr }
};

static InternalPluginEntry *internalPluginById[INTERNAL_PLUGIN_COUNT];

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
  JASSERT(len > 0 && (size_t)len < size) (pluginName) (size)
  .Text("Internal plugin environment variable name is too long.");
  return envName;
}

static void
initializeInternalPluginStateOnce()
{
  JASSERT(numInternalPlugins() == INTERNAL_PLUGIN_COUNT)
  .Text("Internal plugin metadata table is out of sync.");

  disableAllInternalPlugins =
    Util::readBooleanEnv(ENV_VAR_DISABLE_ALL_PLUGINS, false);

  for (size_t i = 0; i < numInternalPlugins(); i++) {
    InternalPluginEntry *entry = &internalPlugins[i];
    JASSERT(entry->id >= 0 && entry->id < INTERNAL_PLUGIN_COUNT)
      (entry->name) (entry->id)
    .Text("Internal plugin table has an invalid id.");
    JASSERT(internalPluginById[entry->id] == NULL) (entry->name) (entry->id)
    .Text("Duplicate internal plugin id.");

    entry->descriptor = entry->descriptorFn();
    JASSERT(strcmp(entry->descriptor.pluginName, entry->name) == 0)
      (entry->descriptor.pluginName) (entry->name)
    .Text("Internal plugin descriptor name does not match its plugin id.");
    char envName[64];
    entry->enabled =
      Util::readBooleanEnv(internalPluginEnvName(entry->name, envName,
                                                 sizeof(envName)), true);
    internalPluginById[entry->id] = entry;
  }
}

static void
initializeInternalPluginState()
{
  int rc = pthread_once(&internalPluginInitOnce,
                        initializeInternalPluginStateOnce);
  JASSERT(rc == 0) (rc)
  .Text("Failed to initialize internal plugin state.");
}

static InternalPluginEntry *
findInternalPluginEntry(DmtcpInternalPluginId_t id)
{
  JASSERT(id >= 0 && id < INTERNAL_PLUGIN_COUNT) (id)
  .Text("Invalid internal plugin id.");
  InternalPluginEntry *entry = internalPluginById[id];
  JASSERT(entry != NULL) (id).Text("Unknown internal plugin id.");
  return entry;
}

bool
internalPluginEnabled(DmtcpInternalPluginId_t id)
{
  initializeInternalPluginState();
  InternalPluginEntry *entry = findInternalPluginEntry(id);

  return !disableAllInternalPlugins && entry->enabled;
}

bool
internalPluginEnabledByName(const char *name)
{
  JASSERT(name != NULL).Text("Invalid internal plugin name.");
  initializeInternalPluginState();

  for (size_t i = 0; i < numInternalPlugins(); i++) {
    InternalPluginEntry *entry = &internalPlugins[i];
    if (strcmp(entry->name, name) == 0) {
      return !disableAllInternalPlugins && entry->enabled;
    }
  }

  return false;
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
  JASSERT(descr.pluginApiVersion != NULL &&
          strcmp(descr.pluginApiVersion, DMTCP_PLUGIN_API_VERSION) == 0)
    (descr.pluginApiVersion) (DMTCP_PLUGIN_API_VERSION)
    .Text("Incompatible DMTCP plugin API version");
  PluginInfo *info = new PluginInfo(descr);

  pluginInfos.push_back(info);
}

extern "C" void
dmtcp_initialize_plugin()
{
  initializeInternalPluginState();
  for (size_t i = 0; i < numInternalPlugins(); i++) {
    InternalPluginEntry *entry = &internalPlugins[i];
    if (entry->descriptor.event_hook != NULL &&
        internalPluginEnabled(entry->id)) {
      dmtcp_register_plugin(entry->descriptor);
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
    JASSERT(false) (event).Text("Not Reachable");
  }
}
} // namespace dmtcp {
