/***************************************************************************
 *   Copyright (C) 2006 by Jason Ansel                                     *
 *   jansel@ccs.neu.edu                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
#include "checkpointcoordinator.h"
#include "constants.h"
#include "dmtcpmessagetypes.h"
#include "syslogcheckpointer.h"
#include "signalmanager.h"

dmtcp::CheckpointCoordinator::CheckpointCoordinator(const ConnectionToFds& ctfd )
    : _conToFds(ctfd)
{}


void dmtcp::CheckpointCoordinator::preCheckpointLock()
{
    SignalManager::saveSignals();
    SyslogCheckpointer::stopService();
	
    // build fd table with stale connections
    _conToFds = ConnectionToFds(KernelDeviceToConnection::Instance());

    //lock each fd
    ConnectionList& connections = ConnectionList::Instance();
    for(ConnectionList::iterator i = connections.begin()
       ; i!= connections.end()
       ; ++i)
    {
	if(_conToFds[i->first].size() == 0) continue;
		
	(i->second)->saveOptions(_conToFds[i->first]);
	(i->second)->doLocking(_conToFds[i->first]);
    }
    
    
}
void dmtcp::CheckpointCoordinator::preCheckpointDrain()
{
    //initialize the drainer
    ConnectionList& connections = ConnectionList::Instance();
    for(ConnectionList::iterator i = connections.begin()
       ; i!= connections.end()
       ; ++i)
    {
        if(_conToFds[i->first].size() > 0)
	{
           (i->second)->preCheckpoint(_conToFds[i->first], _drain);
        }
    }
    
    //this will block until draining is complete
    _drain.monitorSockets( DRAINER_CHECK_FREQ );

    //build list of stale connections
    std::vector<ConnectionList::iterator> staleConnections;
    for(ConnectionList::iterator i = connections.begin()
       ; i!= connections.end()
       ; ++i)
    {
        if(_conToFds[i->first].size() == 0)
            staleConnections.push_back(i);
    }

    //delete all the stale connections
    for(size_t i=0; i<staleConnections.size(); ++i)
    {
        JTRACE("deleting stale connection")(staleConnections[i]->first);
        connections.erase( staleConnections[i] );
    }

    //re build fd table without stale connections
    _conToFds = ConnectionToFds(KernelDeviceToConnection::Instance());

    //write out the *.dmtcp file
    {    
         std::string serialFile = dmtcp::UniquePid::dmtcpCheckpointFilename();
         JTRACE("Writing *.dmtcp checkpoint file");
         jalib::JBinarySerializeWriter wr( serialFile );
         _conToFds.serialize( wr );
     }

}


void dmtcp::CheckpointCoordinator::postCheckpoint()
{
    _drain.refillAllSockets();
    
    ConnectionList& connections = ConnectionList::Instance();
    for(ConnectionList::iterator i= connections.begin()
       ; i!= connections.end()
       ; ++i)
    {
        JWARNING(_conToFds[i->first].size() > 0)(i->first.conId())
             .Text("stale connections should be gone by now");
        if (_conToFds[i->first].size() == 0) continue;

        (i->second)->postCheckpoint(_conToFds[i->first]);
    }
    
    SyslogCheckpointer::restoreService();
    SignalManager::restoreSignals();
}

void dmtcp::CheckpointCoordinator::postRestart()
{
    ConnectionList& connections = ConnectionList::Instance();
    for(ConnectionList::iterator i= connections.begin()
       ; i!= connections.end()
       ; ++i)
    {
        JASSERT(_conToFds[i->first].size() > 0).Text("stale connections should be gone by now");
        
        (i->second)->restoreOptions(_conToFds[i->first]);
    }
	
    KernelDeviceToConnection::Instance().dbgSpamFds();

    //fix our device table to match the new world order
    KernelDeviceToConnection::Instance() = KernelDeviceToConnection(_conToFds);
}

void dmtcp::CheckpointCoordinator::doReconnect(jalib::JSocket& coordinator, jalib::JSocket& restoreListen)
{
    _rewirer.addDataSocket(new jalib::JChunkReader(coordinator,sizeof(DmtcpMessage)));
    _rewirer.addListenSocket(restoreListen);
    _rewirer.setCoordinatorFd( coordinator.sockfd() );
    
    ConnectionList& connections = ConnectionList::Instance();
    for(ConnectionList::iterator i= connections.begin()
       ; i!= connections.end()
       ; ++i)
    {
        JASSERT(_conToFds[i->first].size() > 0).Text("stale connections should be gone by now");
        
        (i->second)->restore(_conToFds[i->first], _rewirer);
    }
    
    _rewirer.doReconnect();
}
