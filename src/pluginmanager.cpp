#include "pluginmanager.h"

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
  // Now register the "in-built" plugins.
  dmtcp_register_plugin(dmtcp_Syslog_PluginDescr());
  dmtcp_register_plugin(dmtcp_Rlimit_Float_PluginDescr());
  dmtcp_register_plugin(dmtcp_Alarm_PluginDescr());
  dmtcp_register_plugin(dmtcp_Terminal_PluginDescr());
  dmtcp_register_plugin(CoordinatorAPI::pluginDescr());
  dmtcp_register_plugin(dmtcp_ProcessInfo_PluginDescr());
  dmtcp_register_plugin(UniquePid::pluginDescr());

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
