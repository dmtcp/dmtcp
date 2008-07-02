#include <unistd.h>
#include <stdlib.h>
#include <string>
#include <stdio.h>
#include <fstream> 
#include <iostream>
#include "jassert.h"
#include "jfilesystem.h"
#include "connectionmanager.h"
#include "dmtcpworker.h"
#include "checkpointcoordinator.h"
#include "mtcpinterface.h"
#include "syscallwrappers.h"
#include "jtimer.h"


using namespace dmtcp;

namespace
{
  class InspectTarget
  {
    public:
      InspectTarget (const std::string& path)
        : _mtcpPath(path),
          _dmtcpPath (path + ".dmtcp")
      {
        JASSERT (jalib::Filesystem::FileExists(_mtcpPath)) (_mtcpPath).Text("missing file");
        JASSERT (jalib::Filesystem::FileExists(_dmtcpPath)) (_dmtcpPath).Text("missing file");
        jalib::JBinarySerializeReader rd(_dmtcpPath);
        _conToFd.serialize(rd);
      }

      std::string     _mtcpPath;
      std::string     _dmtcpPath;
      ConnectionToFds _conToFd;
  };

  class GProcess{
    public:
      GProcess(ConnectionToFds &conToFd){
        procname = conToFd.procname();
        hostname = conToFd.hostname();
        pid = conToFd.pid();
        hostid = conToFd.hostid();
        _index = _nextIndex();
      }

      int index() { return _index; }
      std::string procname;
      std::string hostname;
      pid_t pid;
      long hostid;
      void writeNode(std::ofstream &o){
        o << " \"" << _index << "\"" 
          << " [ label=\"" << procname << "[" << pid << "]\\n" << hostname << "\"," 
          << "shape=box ]\n";
      }

    private:
      int _nextIndex(){
        static int proc_index = 0;
        return proc_index ++;
      }
      int _index;
  };


	//---------------- Connection description class ---------------------------

  class GConnection{
    public:
      GConnection(TcpConnection &tcpCon); 
      void addProc(ConnectionIdentifier &id, int pindex);
      bool operator == (TcpConnection &tcpCon);
      bool operator == (ConnectionIdentifier &conId);
      ConnectionIdentifier srv() const { return _srv; }
      ConnectionIdentifier cli() const { return _cli; }
      void writeConnection(std::ofstream &o,int &conCnt);
    private:
      ConnectionIdentifier _srv,_cli;
      std::list<int> _sprocs,_cprocs;
  };

  GConnection::GConnection(TcpConnection &tcpCon)
  {
    // Consider only established connections
    switch( tcpCon.tcpType() ){
    case TcpConnection::TCP_ACCEPT:
      _srv = tcpCon.id();
      _cli = tcpCon.getRemoteId();
      break;
    case TcpConnection::TCP_CONNECT:
      _cli = tcpCon.id();
      _srv = tcpCon.getRemoteId();
      break;
    }
  }

  bool GConnection::operator == (TcpConnection &tcpCon)
  {
    switch( tcpCon.tcpType() ){
    case TcpConnection::TCP_ACCEPT:
      if( _srv == tcpCon.id() && _cli == tcpCon.getRemoteId() )
        return true;
      return false;
    case TcpConnection::TCP_CONNECT:
      if( _cli == tcpCon.id() && _srv == tcpCon.getRemoteId() )
        return true;
      return false;
    }
  }

  bool GConnection::operator == (ConnectionIdentifier &conId)
  {
    if( _srv == conId || _cli == conId )
      return true;
    return false;
  }

  void GConnection::addProc(ConnectionIdentifier &id,int pindex)
  {
    if( id == _srv ){
      _sprocs.push_back(pindex);
    }else{
      _cprocs.push_back(pindex);
    }
  }

  void GConnection::writeConnection(std::ofstream &o,int &conCnt)
  {
    // Write connection representation
    o << " \"" << conCnt << "\" [ shape=\"circle\", color=\"#00FF00\"]\n"; 
    // Write processes connected to server side
    std::list<int>::iterator lit;
    for(lit = _sprocs.begin(); lit != _sprocs.end(); lit++){
      o << " \"" << conCnt << "\" -> \"" << (*lit) << "\" [ color=\"#000000\" ]\n";
    }
    // Write processes connected to client side
    for(lit = _cprocs.begin(); lit != _cprocs.end(); lit++){
      o << " \"" << (*lit) << "\" -> \"" << conCnt << "\" [ color=\"#000000\" ]\n";
    }
    conCnt++;
  }

	//---------------- Connections fraph class ---------------------------

  class ConnectionGraph{
    public:
      ConnectionGraph(ConnectionList &list);
      bool importProcess(ConnectionToFds &conToFd);
      bool exportGraph(std::string ofile);
      std::list<GConnection>::iterator find(TcpConnection &tcpCon);
      void writeGraph(std::string o);
    private:
      std::list<GConnection> _connections;
      std::list<GProcess> _processes;
  };

  ConnectionGraph::ConnectionGraph(ConnectionList &list)
  {
    ConnectionList::iterator it;

    for(it = list.begin(); it != list.end(); it++){
      Connection &con = *(it->second);
      if(con.conType() != Connection::TCP)
        continue;
      TcpConnection& tcpCon = con.asTcp();
      if( tcpCon.tcpType() == TcpConnection::TCP_ACCEPT ||
          tcpCon.tcpType() == TcpConnection::TCP_CONNECT ){
        // if it is new connection  (when running full inspection 
        // each connection appears twice - on each node)
        if( find(tcpCon) == _connections.end() )
          _connections.push_back(GConnection(tcpCon));
      }
    }
  }

  std::list<GConnection>::iterator
  ConnectionGraph::find(TcpConnection &tcpCon){
    std::list<GConnection>::iterator it = _connections.begin();
    for(; it != _connections.end(); it++){
      if( (*it) == tcpCon ){
        return it;
      }
    }
    return _connections.end();
  } 

  bool ConnectionGraph::importProcess(ConnectionToFds &conToFd)
  {
    ConnectionToFds::const_iterator cit;
    std::list<GProcess>::iterator pit;

    std::cout << "\nimportProcess:\n";

    // Add process to _processes table
    _processes.push_front(GProcess(conToFd));
    pit = _processes.begin();

    // Run through all connections of the process
    for(cit = conToFd.begin(); cit!=conToFd.end(); cit++){
      ConnectionIdentifier conId = cit->first;
      Connection &con = ConnectionList::Instance()[cit->first];

      // Process only network connections
      if( con.conType() != Connection::TCP )
        continue;
      TcpConnection &tcpCon = con.asTcp();

      // If this is not ESTABLISHED connection
      if( tcpCon.tcpType() != TcpConnection::TCP_ACCEPT &&
          tcpCon.tcpType() != TcpConnection::TCP_CONNECT ){
        continue;
      }

      // Map process to connection
      std::list<GConnection>::iterator gcit = find(tcpCon);
      if( gcit != _connections.end() ){ 
        gcit->addProc(conId,pit->index());
      }
    }
  }

  void ConnectionGraph::writeGraph(std::string o)
  {
    std::ofstream out(o.c_str());
    std::list<GConnection>::iterator cit;
    std::list<GProcess>::iterator pit;
    int max_pindex = 0;

    // Head of dot-file 
    out << "digraph { \n";

    // Create nodes for processes
    for(pit = _processes.begin(); pit != _processes.end(); pit++){
      pit->writeNode(out);
      if( pit->index() > max_pindex )
        max_pindex = pit->index();
    }

    
    int conCnt = max_pindex+1;
    for(cit = _connections.begin(); cit != _connections.end(); cit++){
      // Write connection to the file
      cit->writeConnection(out,conCnt);
    }

    out << "}\n"; 
    out.close();
  }
    
}

  


static const char* theUsage = 
										"USAGE: dmtcp_inspector <output.dot> <ckpt1.mtcp> [ckpt2.mtcp...]\n";


int main ( int argc, char** argv )
{
  if( argc < 3 || strcmp(argv[1],"--help")==0 || strcmp(argv[1],"-h")==0){
    fprintf(stderr, theUsage);
    return 1;
  }
  std::string out = argv[1];

  std::vector<InspectTarget> targets;

  for ( int i = argc-1; i>1; --i ){
    if ( targets.size() >0 && targets.back()._dmtcpPath == argv[i] )
      continue;
    
    targets.push_back ( InspectTarget ( argv[i] ) );
  }


  ConnectionGraph conGr(ConnectionList::Instance());
  for(int i =0; i < targets.size(); i++){
    conGr.importProcess(targets[i]._conToFd);
  }

  conGr.writeGraph(out);
  return 0;
}

