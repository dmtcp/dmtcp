#ifndef _MANA_COORDINATOR_
#define _MANA_COORDINATOR_

#include "dmtcp_coordinator.h"
#include "dmtcpmessagetypes.h"
#include "lookup_service.h"

void printNonReadyRanks();
dmtcp::string getClientState(dmtcp::CoordClient* client);
void printMpiDrainStatus(const dmtcp::LookupService& lookupService);
void processPreSuspendClientMsgHelper(dmtcp::DmtcpCoordinator *coord,
                                      dmtcp::CoordClient *client,
                                      int &workersAtCurrentBarrier,
                                      const dmtcp::DmtcpMessage& msg,
                                      const void *extraData);
void sendCkptIntentMsg(dmtcp::DmtcpCoordinator *coord);

#endif // ifndef _MANA_COORDINATOR_
