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
#include "dmtcpmaster.h"
#include "constants.h"
#include "jconvert.h"
#include "dmtcpmessagetypes.h"
#include "dmtcpworker.h"
#include <stdio.h>
#include "jtimer.h"
#include <algorithm>
#undef min
#undef max

const int STDIN_FD = fileno( stdin );

JTIMER(checkpoint);
JTIMER(restart);

namespace
{
    static int theNextClientNumber = 1;
    
    class NamedChunkReader : public jalib::JChunkReader
    {
    public:
        NamedChunkReader(const jalib::JSocket& sock
                        ,const dmtcp::UniquePid& identity
                        ,dmtcp::WorkerState state
                        ,const struct sockaddr * remote
                        ,socklen_t len
                        ,int restorePort)
            : jalib::JChunkReader(sock,sizeof(dmtcp::DmtcpMessage))
            , _identity(identity)
            , _clientNumber(theNextClientNumber++)
            , _state(state)
            , _addrlen(len)
            , _restorePort(restorePort)
        {
            memset(&_addr, 0, sizeof _addr);
            memcpy(&_addr, remote, len);
        }
        const dmtcp::UniquePid& identity() const { return _identity;}
        int clientNumber() const { return _clientNumber; }
        dmtcp::WorkerState state() const { return _state; }
        const struct sockaddr_storage* addr() const { return &_addr; }
        socklen_t addrlen() const { return _addrlen; }
        int restorePort() const { return _restorePort; }
        void setState( dmtcp::WorkerState value ) { _state = value; }
    private:
        dmtcp::UniquePid _identity;
        int _clientNumber;
        dmtcp::WorkerState _state;
        struct sockaddr_storage _addr;
        socklen_t               _addrlen;
        int _restorePort;
    };
}

void dmtcp::DmtcpMaster::onData(jalib::JReaderInterface* sock)
{
    if(sock->socket().sockfd() == STDIN_FD)
    {
//        JTRACE("got request from user")(sock->buffer()[0]);
       
       #ifndef CHECKPOINT_TIMER
       switch(sock->buffer()[0])
       {
           case 'c': case 'C':
                startCheckpoint();
                break;
           case 'l': case 'L':
           case 't': case 'T':
                JASSERT_STDERR << "Listing clients... \n";
                for(std::vector<jalib::JReaderInterface*>::iterator i = _dataSockets.begin()
                    ;i!= _dataSockets.end()
                    ;++i)
                {
                    if((*i)->socket().sockfd() != STDIN_FD)
                    {
                        JASSERT_STDERR << "Client: clientNumber="<< ((NamedChunkReader*) (*i))->clientNumber()
                                << " fd="<<  (*i)->socket().sockfd()
                                << " " << ((NamedChunkReader*) (*i))->identity()
                                << '\n';
                    }
                }
               break;
//            case 't': case 'T':
//                 _table.dbgPrint();
//                 break;
          case 'f': case 'F':
            JNOTE("forcing restart...");
            broadcastMessage(DMT_FORCE_RESTART);
            break;
          case ' ': case '\t': case '\n': case '\r':
                break;
           default:
               JTRACE("unkown char on stdin")(sock->buffer()[0]);
       }
       #endif
       return; 
    }
    else
    {
        NamedChunkReader * client= (NamedChunkReader*) sock;
        DmtcpMessage& msg = *(DmtcpMessage*)sock->buffer();
        msg.assertValid();
        char * extraData = 0;
        
        if(msg.extraBytes > 0)
        {
          extraData = new char[msg.extraBytes];
          sock->socket().readAll(extraData, msg.extraBytes);
        }
        
        switch(msg.type)
        {
            case DMT_OK:
            {
                WorkerState oldState = client->state();
                client->setState(msg.state);
                WorkerState newState = minimumState();
                
                JTRACE("got DMT_OK message")
                        (msg.from)
                        (msg.state)
                        (oldState)
                        (newState);
                
                if(  oldState == WorkerState::RUNNING
                && newState == WorkerState::SUSPENDED)
                {
                    JNOTE("locking all nodes");
                    broadcastMessage(DMT_DO_LOCK_FDS);
                }
                if(  oldState == WorkerState::SUSPENDED
                && newState == WorkerState::LOCKED)
                {
                    JNOTE("draining all nodes");
                    broadcastMessage(DMT_DO_DRAIN);
                }
                if(  oldState == WorkerState::LOCKED
                && newState == WorkerState::DRAINED)
                {
                    JNOTE("checkpointing all nodes");
                    broadcastMessage(DMT_DO_CHECKPOINT);
                }
                if(  oldState == WorkerState::DRAINED
                && newState == WorkerState::CHECKPOINTED)
                {
                    JNOTE("refilling all nodes");
                    broadcastMessage(DMT_DO_REFILL);
                    writeRestartScript();
                }
                if(  oldState == WorkerState::RESTARTING
                && newState == WorkerState::CHECKPOINTED)
                {
                    JTRACE("resetting _restoreWaitingMessages")
                            (_restoreWaitingMessages.size());
                    _restoreWaitingMessages.clear();
                    
//                     sleep(5);
                    JTIMER_STOP(restart);
                    
                    JNOTE("refilling all nodes (after checkpoint)");
                    broadcastMessage(DMT_DO_REFILL);
                }
                if(  oldState == WorkerState::CHECKPOINTED
                && newState == WorkerState::REFILLED)
                {
                    JNOTE("restarting all nodes");
                    broadcastMessage(DMT_DO_RESUME);
                    
                    JTIMER_STOP(checkpoint);
                }
                break;
            }
            case DMT_RESTORE_WAITING:
            {
                DmtcpMessage restMsg = msg;
                restMsg.type = DMT_RESTORE_WAITING;
                memcpy(&restMsg.restoreAddr,client->addr(),client->addrlen());
                restMsg.restoreAddrlen = client->addrlen();
                restMsg.restorePort = client->restorePort();
                JASSERT(restMsg.restorePort > 0)(restMsg.restorePort)(client->identity());
                JASSERT(restMsg.restoreAddrlen > 0)(restMsg.restoreAddrlen)(client->identity());
                JASSERT(restMsg.restorePid != ConnectionIdentifier::Null())(client->identity());
                JTRACE("broadcasting RESTORE_WAITING")
                        (restMsg.restorePid)
                        (restMsg.restoreAddrlen)
                        (restMsg.restorePort);
                _restoreWaitingMessages.push_back(restMsg);
                broadcastMessage( restMsg );
                break;
            }
            
//             case DMT_RESTORE_SEARCHING:
//             {
//                 if(_table[msg.restorePid.id].state() != WorkerState::UNKOWN)
//                 {
//                     const WorkerNode& node = _table[msg.restorePid.id];
//                     JASSERT(node.addrlen() > 0)(node.addrlen());
//                     JASSERT(node.restorePort() > 0)(node.restorePort());
//                     DmtcpMessage msg;
//                     msg.type = DMT_RESTORE_WAITING;
//                     memcpy(&msg.restoreAddr,node.addr(),node.addrlen());
//                     msg.restoreAddrlen = node.addrlen();
//                     msg.restorePid.id = node.id();
//                     msg.restorePort = node.restorePort();
//                     addWrite( new jalib::JChunkWriter(sock->socket(), (char*)&msg, sizeof(DmtcpMessage)));
//                 }
//             }
//             break;

            case DMT_CKPT_FILENAME:
            {
              JASSERT(extraData!=0).Text("extra data expected with DMT_CKPT_FILENAME message");
              std::string ckptFilename;
              std::string hostname;
              ckptFilename = extraData;
              hostname = extraData + ckptFilename.length() + 1;
           
              JTRACE("recording restart info")(ckptFilename)(hostname);
              _restartFilenames[hostname].push_back(ckptFilename);
            }
            break;
            default:
                JASSERT(false)(msg.from)(msg.type).Text("unexpected message from worker");
        }
        
        delete[] extraData;
    }
}

void dmtcp::DmtcpMaster::onDisconnect(jalib::JReaderInterface* sock)
{

    if(sock->socket().sockfd() == STDIN_FD)
    {
        JTRACE("stdin closed");
    }
    else
    {
        NamedChunkReader& client = *((NamedChunkReader*)sock);
        JNOTE("client disconnected")(client.identity());
        
        
//         int clientNumber = ((NamedChunkReader*)sock)->clientNumber();
//         JASSERT(clientNumber >= 0)(clientNumber);
//         _table.removeClient(clientNumber);
    }
}

void dmtcp::DmtcpMaster::onConnect( const jalib::JSocket& sock,  const struct sockaddr* remoteAddr,socklen_t remoteLen)
{
    if(_dataSockets.size() <= 1 /*&& _restoreWaitingMessages.size()>0*/)
    {
        if(_dataSockets.size() == 0 
          || _dataSockets[0]->socket().sockfd() == STDIN_FD)
        {
            JTRACE("resetting _restoreWaitingMessages")
                    (_restoreWaitingMessages.size());
            _restoreWaitingMessages.clear();
            
            JTIMER_START(restart);
        }
    }
        
    
    
    jalib::JSocket remote(sock);
    dmtcp::DmtcpMessage hello_local, hello_remote;
    hello_remote.poison();
    hello_local.type = dmtcp::DMT_HELLO_WORKER;
    remote << hello_local;
    remote >> hello_remote;
    hello_remote.assertValid();
    JASSERT(hello_remote.type == dmtcp::DMT_HELLO_MASTER);
    JNOTE("worker connected")
            (hello_remote.from);
//     _table[hello_remote.from.pid()].setState(hello_remote.state);
    
    NamedChunkReader * ds = new NamedChunkReader(
                                    sock
                                    ,hello_remote.from.pid()
                                    ,hello_remote.state
                                    ,remoteAddr
                                    ,remoteLen
                                    ,hello_remote.restorePort);
    addDataSocket( ds );
    
    if(hello_remote.state == WorkerState::RESTARTING
      &&  _restoreWaitingMessages.size()>0 )
    {
        JTRACE("updating missing broadcasts for new connection")
                (hello_remote.from.pid())
                ( _restoreWaitingMessages.size());
        for(size_t i=0; i<_restoreWaitingMessages.size(); ++i)
        {
            addWrite( 
                new jalib::JChunkWriter( sock
                                    , (char*)&_restoreWaitingMessages[i]
                                    , sizeof(DmtcpMessage) )
            );
        }
    }
    
//     WorkerNode& node = _table[hello_remote.from.pid()];
//     node.setClientNumer( ds->clientNumber() );
/*    
    if(hello_remote.state == WorkerState::RESTARTING)
    {
        node.setAddr(remoteAddr, remoteLen);
        node.setRestorePort(hello_remote.restorePort);
        
        JASSERT(node.addrlen() > 0)(node.addrlen());
        JASSERT(node.restorePort() > 0)(node.restorePort());
        DmtcpMessage msg;
        msg.type = DMT_RESTORE_WAITING;
        memcpy(&msg.restoreAddr,node.addr(),node.addrlen());
        msg.restoreAddrlen = node.addrlen();
        msg.restorePid.id = node.id();
        msg.restorePort = node.restorePort();
        broadcastMessage( msg );
    }*/
}

void dmtcp::DmtcpMaster::onTimeoutInterval()
{
#ifdef CHECKPOINT_TIMER
     startCheckpoint();
#endif
}


void dmtcp::DmtcpMaster::startCheckpoint()
{
    if(minimumState() == WorkerState::RUNNING)
    {
        JTIMER_START(checkpoint);
        _restartFilenames.clear();
        JNOTE("suspending all nodes");
        broadcastMessage(DMT_DO_SUSPEND);
    }
    else
    {
        JTRACE("delaying checkpoint, workers not ready")(minimumState().value());
    }
}
dmtcp::DmtcpWorker& dmtcp::DmtcpWorker::instance() 
{
   JASSERT(false).Text("This method is only available on workers"); 
   return *((DmtcpWorker*)0);
}
const dmtcp::UniquePid& dmtcp::DmtcpWorker::masterId() const
{
   JASSERT(false).Text("This method is only available on workers"); 
   return *((UniquePid*)0);
}
 


void dmtcp::DmtcpMaster::broadcastMessage(DmtcpMessageType type)
{
    DmtcpMessage msg;
    msg.type = type;
    broadcastMessage(msg);
}

void dmtcp::DmtcpMaster::broadcastMessage(const DmtcpMessage& msg)
{
    for(std::vector<jalib::JReaderInterface*>::iterator i = _dataSockets.begin()
        ;i!= _dataSockets.end()
        ;++i)
    {
        if((*i)->socket().sockfd() != STDIN_FD)
            addWrite( new jalib::JChunkWriter((*i)->socket(),(char*)&msg,sizeof(DmtcpMessage)) );
    }
}

dmtcp::WorkerState dmtcp::DmtcpMaster::minimumState() const
{
    int m = 0x0FFFFFF;
    for(const_iterator i = _dataSockets.begin()
                    ;i!= _dataSockets.end()
                    ;++i)
    {
        if((*i)->socket().sockfd() != STDIN_FD)
        {
            NamedChunkReader* client = (NamedChunkReader*) *i;
            if(client->state().value() < m) m = client->state().value();
        }
    }
    return m==0x0FFFFFF ? WorkerState::UNKOWN : (WorkerState::eWorkerState)m;
}

void dmtcp::DmtcpMaster::writeRestartScript()
{
  std::map< std::string, std::vector<std::string> >::const_iterator host;
  std::vector<std::string>::const_iterator file;
  std::string filename = RESTART_SCRIPT_NAME;
  JTRACE("writing restart script")(filename);
  FILE* fp = fopen(filename.c_str(),"w");
  JASSERT(fp!=0)(filename).Text("failed to open file");  
  fprintf(fp, "#!/bin/bash \nset -m # turn on job control\n\n#launch all the restarts in the background:\n");
  
  for(host=_restartFilenames.begin(); host!=_restartFilenames.end(); ++host)
  {
    fprintf(fp,"ssh %s " DMTCP_RESTART_CMD " ", host->first.c_str());
    for(file=host->second.begin(); file!=host->second.end(); ++file)
    {
      fprintf(fp," %s", file->c_str());
    }
    fprintf(fp," & \n");
  }
  fprintf(fp,"\n#wait for them all to finish\nwait");
  fclose(fp);
  _restartFilenames.clear();
}

int main( int argc, char** argv)
{
    dmtcp::DmtcpMessage::setDefaultMaster(dmtcp::UniquePid::ThisProcess());
    int port = DEFAULT_PORT;
    const char* portStr = getenv(ENV_VAR_NAME_PORT);
    if(portStr != NULL) port = jalib::StringToInt(portStr); 
    if(argc > 1) port = jalib::StringToInt(argv[1]);
    JTRACE("dmtcp_master starting...")(port);
    jalib::JServerSocket sock(jalib::JSockAddr::ANY,port);
    JASSERT(sock.isValid())(port).Text("Failed to create listen socket");
    dmtcp::DmtcpMaster prog;
    prog.addListenSocket(sock);
    prog.addDataSocket( new jalib::JChunkReader( STDIN_FD , 1) );
    prog.monitorSockets(DEFAULT_CHECKPOINT_INTERVAL);
    return 0;
}

