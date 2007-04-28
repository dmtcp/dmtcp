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
#ifndef DMTCPCHECKPOINTCOORDINATOR_H
#define DMTCPCHECKPOINTCOORDINATOR_H

#include "kernelbufferdrainer.h"
#include "connectionmanager.h"
#include "connectionrewirer.h"
#include "jsocket.h"

namespace dmtcp {

    class CheckpointCoordinator {
    public:
        CheckpointCoordinator(const ConnectionToFds& ctfd = ConnectionToFds());
        
        void preCheckpointLock();
        void preCheckpointDrain();
        void postCheckpoint();
        
        void postRestart();
        void doReconnect(jalib::JSocket& master, jalib::JSocket& restoreListen);
        
    private:
        KernelBufferDrainer _drain;
        ConnectionToFds     _conToFds;
        ConnectionRewirer   _rewirer;
    };
    
}

#endif
