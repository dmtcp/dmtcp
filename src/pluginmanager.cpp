#include "pluginmanager.h"

#include "dmtcp.h"
#include "dmtcpalloc.h"
#include "plugininfo.h"
#include "util.h"

static dmtcp::PluginManager *pluginManager = NULL;

extern "C" void dmtcp_register_plugin(DmtcpPluginDescriptor_t descr)
{
  JASSERT(pluginManager != NULL);

  pluginManager->registerPlugin(descr);
}

namespace dmtcp
{

DmtcpPluginDescriptor_t dmtcp_CoordinatorAPI_PluginDescr();
DmtcpPluginDescriptor_t dmtcp_ProcessInfo_PluginDescr();
DmtcpPluginDescriptor_t dmtcp_Syslog_PluginDescr();
DmtcpPluginDescriptor_t dmtcp_Alarm_PluginDescr();
DmtcpPluginDescriptor_t dmtcp_Terminal_PluginDescr();
DmtcpPluginDescriptor_t dmtcp_CoordinatorAPI_PluginDescr();

void PluginManager::initialize()
{
  if (pluginManager == NULL) {
    pluginManager = new PluginManager();
  }

  // Now initialize plugins.
  // Call into other plugins to have them register with us.
  if (dmtcp_initialize_plugin != NULL) {
    dmtcp_initialize_plugin();
  }

  // Register plugin list with coordinator.
  registerBarriersWithCoordinator();
}

PluginManager::PluginManager()
{}

void PluginManager::registerPlugin(DmtcpPluginDescriptor_t descr)
{
  // TODO(kapil): Validate the incoming descriptor.
  PluginInfo *info = PluginInfo::create(descr);
  pluginInfos.push_back(info);
}

static DmtcpPluginDescriptor_t createPluginDescr(const char *name,
                                                 HookFunctionPtr_t hook)
{
  DmtcpPluginDescriptor_t descr = {
    DMTCP_PLUGIN_API_VERSION,
    PACKAGE_VERSION,
    name,
    "DMTCP",
    "dmtcp@ccs.neu.edu",
    "",
    DMTCP_NO_PLUGIN_BARRIERS,
    hook
  };
  return descr;
}

extern "C" void dmtcp_initialize_plugin()
{
  // Now register the "in-built" plugins.
  dmtcp_register_plugin(dmtcp_Syslog_PluginDescr());
  dmtcp_register_plugin(dmtcp_Alarm_PluginDescr());
  dmtcp_register_plugin(dmtcp_Terminal_PluginDescr());
  dmtcp_register_plugin(dmtcp_CoordinatorAPI_PluginDescr());
  dmtcp_register_plugin(dmtcp_ProcessInfo_PluginDescr());

  void (*fn)() = NEXT_FNC(dmtcp_initialize_plugin);
  if (fn != NULL) {
    (*fn)();
  }
}

void PluginManager::registerBarriersWithCoordinator()
{
  vector<string> preCkptBarriers;
  vector<string> resumeBarriers;
  vector<string> restartBarriers;

  for (size_t i = 0; i < pluginManager->pluginInfos.size(); i++) {
    const vector<BarrierInfo*> barriers =
      pluginManager->pluginInfos[i]->preCkptBarriers;
    for (size_t j = 0; j < barriers.size(); j++) {
      preCkptBarriers.push_back(barriers[j]->toString());
    }
  }

  for (int i = pluginManager->pluginInfos.size() - 1; i >= 0; i--) {
    const vector<BarrierInfo*> barriers =
      pluginManager->pluginInfos[i]->resumeBarriers;
    for (size_t j = 0; j < barriers.size(); j++) {
      resumeBarriers.push_back(barriers[j]->toString());
    }
  }

  for (int i = pluginManager->pluginInfos.size() - 1; i >= 0; i--) {
    const vector<BarrierInfo*> barriers =
      pluginManager->pluginInfos[i]->restartBarriers;
    for (size_t j = 0; j < barriers.size(); j++) {
      restartBarriers.push_back(barriers[j]->toString());
    }
  }

  // TODO(kapil): Have a generic way to avoid bugs.
  string barrierList =
    Util::joinStrings(preCkptBarriers, ",") + ";" +
    Util::joinStrings(resumeBarriers, ",") + ";" +
    Util::joinStrings(restartBarriers, ",");

  DmtcpMessage msg;
  msg.type = DMT_BARRIER_LIST;
  msg.state = WorkerState::currentState();
  msg.extraBytes = barrierList.length() + 1;
  CoordinatorAPI::instance().sendMsgToCoordinator(msg,
                                                  barrierList.c_str(),
                                                  msg.extraBytes);
}

void PluginManager::processCkptBarriers()
{
  for (int i = 0; i < pluginManager->pluginInfos.size(); i++) {
    pluginManager->pluginInfos[i]->processBarriers();
  }
}

void PluginManager::processResumeBarriers()
{
  for (int i = pluginManager->pluginInfos.size() - 1; i >= 0; i--) {
    pluginManager->pluginInfos[i]->processBarriers();
  }
}

void PluginManager::processRestartBarriers()
{
  PluginManager::registerBarriersWithCoordinator();

  for (int i = pluginManager->pluginInfos.size() - 1; i >= 0; i--) {
    pluginManager->pluginInfos[i]->processBarriers();
  }
}

void PluginManager::eventHook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
    // case DMTCP_EVENT_WRAPPER_INIT, // Future Work :-).
    case DMTCP_EVENT_INIT:

    case DMTCP_EVENT_PRE_EXEC:
    case DMTCP_EVENT_POST_EXEC:

    case DMTCP_EVENT_ATFORK_PARENT:
    case DMTCP_EVENT_ATFORK_CHILD:

    case DMTCP_EVENT_PTHREAD_START:
      for (size_t i = 0; i < pluginManager->pluginInfos.size(); i++) {
        pluginManager->pluginInfos[i]->eventHook(event, data);
      }
      break;

    case DMTCP_EVENT_EXIT:
    case DMTCP_EVENT_PTHREAD_EXIT:
    case DMTCP_EVENT_PTHREAD_RETURN:

    case DMTCP_EVENT_ATFORK_PREPARE:

      for (int i = pluginManager->pluginInfos.size() - 1; i >= 0; i--) {
        pluginManager->pluginInfos[i]->eventHook(event, data);
      }
      break;

    default:
      JASSERT(false) (event) .Text("Not Reachable");
  }
}

} // namespace dmtcp {
