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
DmtcpPluginDescriptor_t dmtcp_UniqueCkpt_PluginDescr();
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

static DmtcpPluginDescriptor_t allocPlugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "ALLOC",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "Allocation wrappers",
  NULL,
  1,
  INTERNAL_PLUGIN_ALLOC,
  1,
  1,
  1
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
  NULL,
  1,
  INTERNAL_PLUGIN_DL,
  1,
  1,
  1
};

DmtcpPluginDescriptor_t
dmtcp_DlPlugin_PluginDescr()
{
  return dlPlugin;
}

static BuiltinDescriptorFn internalPluginDescriptorFns[] = {
  // Keep UNIQUE_CKPT first.  Its PRECHECKPOINT hook updates the checkpoint
  // directory name, and later plugins should observe that final directory
  // when they serialize or reopen plugin-owned state.
  dmtcp_UniqueCkpt_PluginDescr,
  dmtcp_PathTranslator_PluginDescr,
  dmtcp_Syslog_PluginDescr,
  dmtcp_Rlimit_Float_PluginDescr,
  dmtcp_Alarm_PluginDescr,
  dmtcp_Terminal_PluginDescr,
  CoordinatorAPI::pluginDescr,
  dmtcp_ProcessInfo_PluginDescr,
  UniquePid::pluginDescr,
  dmtcp_SshPlugin_PluginDescr,
  dmtcp_EventPlugin_PluginDescr,
  dmtcp_FilePlugin_PluginDescr,
  dmtcp_PtyPlugin_PluginDescr,
  dmtcp_SocketPlugin_PluginDescr,
  dmtcp_SysVIPC_PluginDescr,
  dmtcp_Timer_PluginDescr,
  dmtcp_PidPlugin_PluginDescr,
  dmtcp_AllocPlugin_PluginDescr,
  dmtcp_DlPlugin_PluginDescr
};

static DmtcpPluginDescriptor_t internalPluginDescriptors[INTERNAL_PLUGIN_COUNT];
static DmtcpPluginDescriptor_t *internalPluginOrder[INTERNAL_PLUGIN_COUNT];
static DmtcpPluginDescriptor_t *internalPluginById[INTERNAL_PLUGIN_COUNT];

static pthread_once_t internalPluginInitOnce = PTHREAD_ONCE_INIT;
static bool disableAllInternalPlugins = false;

static size_t
numInternalPlugins()
{
  return sizeof(internalPluginDescriptorFns) /
         sizeof(internalPluginDescriptorFns[0]);
}

static const char *
internalPluginEnvName(const DmtcpPluginDescriptor_t *descr,
                      char *envName,
                      size_t size)
{
  if (!descr->internalPluginEnvControlled) {
    return NULL;
  }

  int len = snprintf(envName, size, "DMTCP_%s_PLUGIN", descr->pluginName);
  JASSERT(len > 0 && (size_t)len < size) (descr->pluginName) (size)
  .Text("Internal plugin environment variable name is too long.");
  return envName;
}

static bool
disabledByDisableAll(const DmtcpPluginDescriptor_t *descr)
{
  return disableAllInternalPlugins && descr->internalPluginEnvControlled;
}

static void
initializeInternalPluginStateOnce()
{
  JASSERT(numInternalPlugins() == INTERNAL_PLUGIN_COUNT)
  .Text("Internal plugin metadata table is out of sync.");

  disableAllInternalPlugins =
    Util::readBooleanEnv(ENV_VAR_DISABLE_ALL_PLUGINS, false);

  for (size_t i = 0; i < numInternalPlugins(); i++) {
    internalPluginDescriptors[i] = internalPluginDescriptorFns[i]();
    DmtcpPluginDescriptor_t *descr = &internalPluginDescriptors[i];
    JASSERT(descr->isInternalPlugin) (descr->pluginName)
    .Text("Built-in descriptor is missing internal plugin metadata.");
    JASSERT(descr->internalPluginId >= 0 &&
            descr->internalPluginId < INTERNAL_PLUGIN_COUNT)
      (descr->pluginName) (descr->internalPluginId)
    .Text("Internal plugin descriptor has an invalid id.");
    JASSERT(internalPluginById[descr->internalPluginId] == NULL)
      (descr->pluginName) (descr->internalPluginId)
    .Text("Duplicate internal plugin id.");

    internalPluginOrder[i] = descr;
    internalPluginById[descr->internalPluginId] = descr;
  }

  for (size_t i = 0; i < numInternalPlugins(); i++) {
    DmtcpPluginDescriptor_t *descr = internalPluginOrder[i];
    char envName[64];
    descr->internalPluginEnabled =
      Util::readBooleanEnv(internalPluginEnvName(descr, envName,
                                                 sizeof(envName)),
                           descr->internalPluginDefaultEnabled != 0);
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

static DmtcpPluginDescriptor_t *
findInternalPluginDescriptor(DmtcpInternalPluginId_t id)
{
  JASSERT(id >= 0 && id < INTERNAL_PLUGIN_COUNT) (id)
  .Text("Invalid internal plugin id.");
  DmtcpPluginDescriptor_t *descr = internalPluginById[id];
  JASSERT(descr != NULL) (id).Text("Unknown internal plugin id.");
  return descr;
}

bool
internalPluginEnabled(DmtcpInternalPluginId_t id)
{
  initializeInternalPluginState();
  DmtcpPluginDescriptor_t *descr = findInternalPluginDescriptor(id);

  return !disabledByDisableAll(descr) && descr->internalPluginEnabled;
}

bool
internalPluginEnabledByName(const char *name)
{
  JASSERT(name != NULL).Text("Invalid internal plugin name.");
  initializeInternalPluginState();

  for (size_t i = 0; i < numInternalPlugins(); i++) {
    DmtcpPluginDescriptor_t *descr = internalPluginOrder[i];
    if (strcmp(descr->pluginName, name) == 0) {
      return !disabledByDisableAll(descr) &&
             descr->internalPluginEnabled;
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
  // TODO(kapil): Validate the incoming descriptor.
  PluginInfo *info = new PluginInfo(descr);

  pluginInfos.push_back(info);
}

extern "C" void
dmtcp_initialize_plugin()
{
  initializeInternalPluginState();
  for (size_t i = 0; i < numInternalPlugins(); i++) {
    DmtcpPluginDescriptor_t *descr = internalPluginOrder[i];
    if (descr->event_hook != NULL &&
        internalPluginEnabled(descr->internalPluginId)) {
      dmtcp_register_plugin(*descr);
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
