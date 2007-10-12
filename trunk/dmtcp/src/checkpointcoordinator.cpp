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
    
// #ifdef TEST_CKTP_SERIALIZE
//     std::string filename = UniquePid::dmtcpTableFilename();
//     JTRACE("writing out file")(filename);
//     {
//         jalib::JBinarySerializeWriter wr(filename);
//         KernelDeviceToConnection::Instance().serialize( wr );
//     }       
//     
//     //reset state
//     ConnectionList::Instance() = ConnectionList();
//     KernelDeviceToConnection::Instance() = KernelDeviceToConnection();
//     
//     JTRACE("reading in file")(filename);
//     {
//         jalib::JBinarySerializeReader rd(filename);
//         KernelDeviceToConnection::Instance().serialize( rd );
//     }
// #endif
    
    //build fd table
    _conToFds = KernelDeviceToConnection::Instance();
    
    {
        std::string serialFile = dmtcp::UniquePid::dmtcpCheckpointFilename();
        JTRACE("Writing checkpoint file");
        jalib::JBinarySerializeWriter wr( serialFile );
        _conToFds.serialize( wr );
    }
    
    
    //lock each fd
    ConnectionList& connections = ConnectionList::Instance();
    for(ConnectionList::iterator i = connections.begin()
       ; i!= connections.end()
       ; ++i)
    {
        if(_conToFds[i->first].size() == 0)
        {
            //TODO: figure out why this segfaults us
//             connections.erase( i );
        }
        else
        {
            (i->second)->saveOptions(_conToFds[i->first]);
            (i->second)->doLocking(_conToFds[i->first]);
        }
    }
    
    
}

void dmtcp::CheckpointCoordinator::preCheckpointDrain()
{

    ConnectionList& connections = ConnectionList::Instance();
    for(ConnectionList::iterator i = connections.begin()
       ; i!= connections.end()
       ; ++i)
    {
        if(_conToFds[i->first].size() == 0) continue;
        
        (i->second)->preCheckpoint(_conToFds[i->first], _drain);
    }
    
    _drain.monitorSockets( DRAINER_CHECK_FREQ );
}


void dmtcp::CheckpointCoordinator::postCheckpoint()
{
    _drain.refillAllSockets();
    
    ConnectionList& connections = ConnectionList::Instance();
    for(ConnectionList::iterator i= connections.begin()
       ; i!= connections.end()
       ; ++i)
    {
        if(_conToFds[i->first].size() == 0) continue;
        
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
        if(_conToFds[i->first].size() == 0) continue;
        
        (i->second)->restoreOptions(_conToFds[i->first]);
    }
    
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
        if(_conToFds[i->first].size() == 0) continue;
        
        (i->second)->restore(_conToFds[i->first], _rewirer);
    }
    
    _rewirer.doReconnect();
}
