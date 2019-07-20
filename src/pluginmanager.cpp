#include "pluginmanager.h"

#include "coordinatorapi.h"
#include "config.h"
#include "dmtcp.h"
#include "dmtcpalloc.h"
#include "jtimer.h"
#include "plugininfo.h"
#include "util.h"

static const char *firstRestartBarrier = "DMTCP::RESTART";

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

void
PluginManager::registerPlugin(DmtcpPluginDescriptor_t descr)
{
  // TODO(kapil): Validate the incoming descriptor.
  PluginInfo *info = PluginInfo::create(descr);

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

  void (*fn)() = NEXT_FNC(dmtcp_initialize_plugin);
  if (fn != NULL) {
    (*fn)();
  }
}

static string
joinStrings(vector<string>v, const string &delim)
{
  string result;

  if (v.size() > 0) {
    result = v[0];
    for (size_t i = 1; i < v.size(); i++) {
      result += delim + v[i];
    }
  }
  return result;
}

void
PluginManager::registerBarriersWithCoordinator()
{
  vector<string>ckptBarriers;
  vector<string>restartBarriers;

  for (size_t i = 0; i < pluginManager->pluginInfos.size(); i++) {
    const vector<BarrierInfo *>barriers =
      pluginManager->pluginInfos[i]->preSuspendBarriers;
    for (size_t j = 0; j < barriers.size(); j++) {
      if (barriers[j]->isGlobal()) {
        ckptBarriers.push_back(barriers[j]->toString());
      }
    }
  }

  for (size_t i = 0; i < pluginManager->pluginInfos.size(); i++) {
    const vector<BarrierInfo *>barriers =
      pluginManager->pluginInfos[i]->preCkptBarriers;
    for (size_t j = 0; j < barriers.size(); j++) {
      if (barriers[j]->isGlobal()) {
        ckptBarriers.push_back(barriers[j]->toString());
      }
    }
  }

  for (int i = pluginManager->pluginInfos.size() - 1; i >= 0; i--) {
    const vector<BarrierInfo *>barriers =
      pluginManager->pluginInfos[i]->resumeBarriers;
    for (size_t j = 0; j < barriers.size(); j++) {
      if (barriers[j]->isGlobal()) {
        ckptBarriers.push_back(barriers[j]->toString());
      }
    }
  }

  restartBarriers.push_back(firstRestartBarrier);
  for (int i = pluginManager->pluginInfos.size() - 1; i >= 0; i--) {
    const vector<BarrierInfo *>barriers =
      pluginManager->pluginInfos[i]->restartBarriers;
    for (size_t j = 0; j < barriers.size(); j++) {
      if (barriers[j]->isGlobal()) {
        restartBarriers.push_back(barriers[j]->toString());
      }
    }
  }

  // TODO(kapil): Have a generic way to avoid bugs.
  string barrierList =
    joinStrings(ckptBarriers, ",") + ";" +
    joinStrings(restartBarriers, ",");

  DmtcpMessage msg(DMT_BARRIER_LIST);
  CoordinatorAPI::sendMsgToCoordinator(msg, barrierList);
}

void
PluginManager::processPreSuspendBarriers()
{
  for (size_t i = 0; i < pluginManager->pluginInfos.size(); i++) {
    pluginManager->pluginInfos[i]->processBarriers();
  }
}

void
PluginManager::processCkptBarriers()
{
  for (size_t i = 0; i < pluginManager->pluginInfos.size(); i++) {
    pluginManager->pluginInfos[i]->processBarriers();
  }

  JTIMER_START(ckptWriteTime);
}

void
PluginManager::processResumeBarriers()
{
  JTIMER_STOP(ckptWriteTime);
  for (int i = pluginManager->pluginInfos.size() - 1; i >= 0; i--) {
    pluginManager->pluginInfos[i]->processBarriers();
  }
}

#ifdef TIMING
void
PluginManager::logCkptResumeBarrierOverhead()
{
  char logFilename[5000] = {0};
  snprintf(logFilename, sizeof(logFilename), "%s/timings.%s.csv",
           dmtcp_get_ckpt_dir(), dmtcp_get_uniquepid_str());
  std::ofstream lfile (logFilename, std::ios::out | std::ios::app);

  double writeTime = 0.0;
  JTIMER_GETDELTA(writeTime, ckptWriteTime);
  lfile << "Ckpt-write time," << writeTime << std::endl;

  for (int i = pluginManager->pluginInfos.size() - 1; i >= 0; i--) {
    for (int j = 0;
         j < pluginManager->pluginInfos[i]->preSuspendBarriers.size(); j++) {
      lfile << pluginManager->pluginInfos[i]->preSuspendBarriers[j]->toString()
            <<  ','
            << pluginManager->pluginInfos[i]->preSuspendBarriers[j]->execTime
            << ','
            << pluginManager->pluginInfos[i]->preSuspendBarriers[j]->cbExecTime
            << std::endl;
    }

    for (int j = 0;
         j < pluginManager->pluginInfos[i]->preCkptBarriers.size(); j++) {
      lfile << pluginManager->pluginInfos[i]->preCkptBarriers[j]->toString()
            <<  ','
            << pluginManager->pluginInfos[i]->preCkptBarriers[j]->execTime
            << ','
            << pluginManager->pluginInfos[i]->preCkptBarriers[j]->cbExecTime
            << std::endl;
    }

    for (int j = 0;
         j < pluginManager->pluginInfos[i]->resumeBarriers.size(); j++) {
      lfile << pluginManager->pluginInfos[i]->resumeBarriers[j]->toString()
            <<  ','
            << pluginManager->pluginInfos[i]->resumeBarriers[j]->execTime
            << ','
            << pluginManager->pluginInfos[i]->resumeBarriers[j]->cbExecTime
            << std::endl;
    }
  }
}

void
PluginManager::logRestartBarrierOverhead(double ckptReadTime)
{
  char logFilename[5000] = {0};
  snprintf(logFilename, sizeof(logFilename), "%s/timings.%s.csv",
           dmtcp_get_ckpt_dir(), dmtcp_get_uniquepid_str());
  std::ofstream lfile (logFilename, std::ios::out | std::ios::app);
  lfile << "Ckpt-read time," << ckptReadTime << std::endl;
  for (int i = pluginManager->pluginInfos.size() - 1; i >= 0; i--) {
    for (int j = 0;
         j < pluginManager->pluginInfos[i]->restartBarriers.size(); j++) {
      lfile << pluginManager->pluginInfos[i]->restartBarriers[j]->toString()
            <<  ','
            << pluginManager->pluginInfos[i]->restartBarriers[j]->execTime
            << ','
            << pluginManager->pluginInfos[i]->restartBarriers[j]->cbExecTime
            << std::endl;
    }
  }
}
#endif

void
PluginManager::processRestartBarriers()
{
  PluginManager::registerBarriersWithCoordinator();

  Util::allowGdbDebug(DEBUG_PLUGIN_MANAGER);

  CoordinatorAPI::waitForBarrier(firstRestartBarrier);

  for (int i = pluginManager->pluginInfos.size() - 1; i >= 0; i--) {
    pluginManager->pluginInfos[i]->processBarriers();
  }
}

void
PluginManager::eventHook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  JASSERT(pluginManager != NULL);

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
    JASSERT(false) (event).Text("Not Reachable");
  }
}
} // namespace dmtcp {
