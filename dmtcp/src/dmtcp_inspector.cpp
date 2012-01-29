/****************************************************************************
 *   Copyright (C) 2006-2010 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 *   This file is part of the dmtcp/src module of DMTCP (DMTCP:dmtcp/src).  *
 *                                                                          *
 *  DMTCP:dmtcp/src is free software: you can redistribute it and/or        *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP:dmtcp/src is distributed in the hope that it will be useful,      *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

// This file was originally contributed
// by Artem Y. Polyakov <artpol84@gmail.com>.

// System includes
#include <unistd.h>
#include <stdlib.h>
#include <string>
#include <stdio.h>
#include <getopt.h>
// C++ includes
#include <fstream>
#include <iostream>
#include <sstream>
// Local includes
#include "constants.h"
#include "syscallwrappers.h"
#include  "../jalib/jassert.h"
#include  "../jalib/jfilesystem.h"
#include "connectionmanager.h"
#include "connectionstate.h"
#include "mtcpinterface.h"
#include "processinfo.h"
#include "ckptserializer.h"
#include  "../jalib/jtimer.h"

#define BINARY_NAME "dmtcp_checkpoint"

enum output_t {
  TEXT, DOT
} output_type = TEXT;

enum proc_out_t { NUMBER, SHORT, DETAILED, OUT_MAX } proc_out = SHORT;
bool parent_child = false;
bool sockets = true;
bool show_all_conn = false;
bool show_part_conn = false;
dmtcp::string output_file="", output_tool="", output_format = "", gviz_params="";


using namespace dmtcp;

namespace {

  class InspectTarget {
  public:

    InspectTarget(const dmtcp::string& path)
    {
      JASSERT(jalib::Filesystem::FileExists(path))(path).Text("missing file");
      CkptSerializer::loadFromFile(path, &_conToFd, &_processInfo);
    }
    ConnectionToFds _conToFd;
    ProcessInfo     _processInfo;
  };

  class GProcess {
  public:

    GProcess(ConnectionToFds &conToFd, ProcessInfo &processInfo, bool finfo)
    {
      procname = processInfo.procname();
      hostname = processInfo.hostname();
      pid = processInfo.upid();
      ppid = processInfo.uppid();
      _index = _nextIndex();
      fullinfo = finfo;
    }

    int index()
    {
      return _index;
    }
    dmtcp::string procname;
    dmtcp::string hostname;
    UniquePid pid;
    UniquePid ppid;
void writeNode(dmtcp::ostringstream &o)
    {

      if (proc_out == NUMBER) {
        o << " \"" << _index << "\""
                << " [ label=\"" << _index << "\"]\n";
      } else {
        o << " \"" << _index << "\""
                << " [ label=\"" << procname;
        if (proc_out == DETAILED) {
          time_t tm = pid.time();
          char s[256];
          strftime(s, 256, "%H:%M.%F", localtime(&tm));
          o << "[" << pid.pid() << "]@" << hostname
                  << "\\n" << s;
        }
        o << "\" shape=box ]\n";
      }
    }

  private:

    int _nextIndex()
    {
      static int proc_index = 0;
      return proc_index++;
    }
    int _index;
    bool fullinfo;
  };

  vector<InspectTarget> targets;

  //---------------- Connection description class ---------------------------

  class GConnection {
  public:
    GConnection(TcpConnection &tcpCon);
    void addProc(ConnectionIdentifier &id, int pindex, dmtcp::string hname);
    bool operator ==(TcpConnection &tcpCon);
    bool operator ==(ConnectionIdentifier &conId);

    ConnectionIdentifier srv() const
    {
      return _srv;
    }

    ConnectionIdentifier cli() const
    {
      return _cli;
    }
    void writeConnection(dmtcp::ostringstream &o, int &conCnt);
    void writeConnectionShort(dmtcp::ostringstream &o, unsigned char *matrix,
                              int conCnt);

    bool is_loop()
    {
      return _loop;
    }

    dmtcp::string hostname()
    {
      return _hostname;
    }
  private:
    bool _loop;
    dmtcp::string _hostname;
    ConnectionIdentifier _srv, _cli;
    dmtcp::list<int> _sprocs, _cprocs;
  };

  GConnection::GConnection(TcpConnection &tcpCon)
  {
    // Consider only established connections
    switch (tcpCon.tcpType()) {
    case TcpConnection::TCP_ACCEPT:
      _srv = tcpCon.id();
      _cli = tcpCon.getRemoteId();
      break;
    case TcpConnection::TCP_CONNECT:
      _cli = tcpCon.id();
      _srv = tcpCon.getRemoteId();
      break;
    }
    // Check if it is loop connection or not
    if (_srv.pid().hostid() == _cli.pid().hostid()) {
      _loop = true;
    } else {
      _loop = false;
    }
    _hostname = "?";
    _sprocs.clear();
    _cprocs.clear();
  }

  bool GConnection::operator ==(TcpConnection &tcpCon)
  {
    switch (tcpCon.tcpType()) {
    case TcpConnection::TCP_ACCEPT:
      if (_srv == tcpCon.id() && _cli == tcpCon.getRemoteId())
        return true;
      return false;
    case TcpConnection::TCP_CONNECT:
      if (_cli == tcpCon.id() && _srv == tcpCon.getRemoteId())
        return true;
      return false;
    default:
      return false;
    }
  }

  bool GConnection::operator ==(ConnectionIdentifier &conId)
  {
    if (_srv == conId || _cli == conId)
      return true;
    return false;
  }

  void GConnection::addProc(ConnectionIdentifier &id, int pindex, dmtcp::string hostname)
  {
    // Double check of loop connection
    // in the case host_hash has collision
    if (_hostname == "?") {
      _hostname = hostname;
    } else if (hostname != _hostname) {
      _loop = false;
    }
    // Save process in connection
    if (id == _srv) {
      _sprocs.push_back(pindex);
    } else {
      _cprocs.push_back(pindex);
    }
  }

  void GConnection::writeConnection(dmtcp::ostringstream &o, int &conCnt)
  {

    // if connection have only one part (client or server)
    // show it only if requested
    if (!show_part_conn) {
      if (_sprocs.size() == 0 || _cprocs.size() == 0)
        return;
    }

    // If connection have no shared descriptors
    if (_sprocs.size() == _cprocs.size() && _sprocs.size() == 1) {
      o << " \"" << *_cprocs.begin() << "\" -> \"" << *_sprocs.begin()
              << "\" [ color=\"#000000\", arrowhead=\"none\",arrowtail=\"none\" ]\n";
      return;
    }
    // else: what if no srv or no cli connections?

    // Write _shared_ connection representation
    o << " \"" << conCnt <<
            "\" [ shape=\"circle\", color=\"#007700\", fontsize=10";
    if (conCnt < 100) {
      o << ", fixedsize=true, height=\"0.4\"";
    }
    o << " ]\n";

    // Write processes connected to server side
    dmtcp::list<int>::iterator lit;
    for (lit = _sprocs.begin(); lit != _sprocs.end(); lit++) {
      o << " \"" << (*lit) << "\" -> \"" << conCnt <<
              "\" [ color=\"#000000\", arrowhead=\"tee\" ]\n";
    }

    // Write processes connected to client side
    for (lit = _cprocs.begin(); lit != _cprocs.end(); lit++) {
      o << " \"" << (*lit) << "\" -> \"" << conCnt <<
              "\" [ color=\"#000000\", arrowhead=\"none\" ]\n";
    }
    conCnt++;
  }

  void GConnection::writeConnectionShort(dmtcp::ostringstream &o,
                                         unsigned char *matrix, int conCnt)
  {

    // if connection have only one part (client or server)
    // show it only if requested
    if (_sprocs.size() == 0 || _cprocs.size() == 0)
      return;

    // Write processes connected to server side
    dmtcp::list<int>::iterator lit1, lit2;
    for (lit1 = _sprocs.begin(); lit1 != _sprocs.end(); lit1++) {
      for (lit2 = _cprocs.begin(); lit2 != _cprocs.end(); lit2++) {
        int row = *lit1;
        int col = *lit2;
        // We deal with low-diagonal matrix (it will be symmetric)
        if (col > row) {
          int tmp = col;
          col = row;
          row = tmp;
        }
        if (*(matrix + row * conCnt + col) == 0 && row != col) {
          o << " \"" << col << "\" -> \"" << row <<
                  "\" [ color=\"#000000\", arrowhead=\"none\",arrowtail=\"none\" ]\n";
          *(matrix + row * conCnt + col) = 1;
        }
      }
    }
  }


  //---------------- Connections fraph class ---------------------------

  class ConnectionGraph {
  public:
    ConnectionGraph(ConnectionList &list);
    void importProcess(ConnectionToFds &conToFd, ProcessInfo &processInfo);
    bool exportGraph(dmtcp::string ofile);
    dmtcp::list<GConnection>::iterator find(TcpConnection &tcpCon);
    void writeGraph(dmtcp::ostringstream &o);
  private:
    dmtcp::list<GConnection> _connections;
    typedef dmtcp::map<dmtcp::string, dmtcp::list<GProcess> > ClusterProcesses;
    ClusterProcesses _processes;
    typedef dmtcp::map<dmtcp::UniquePid, GProcess*> DMTCP_process;
    DMTCP_process _row_processes;
  };

  ConnectionGraph::ConnectionGraph(ConnectionList &list)
  {
    ConnectionList::iterator it;

    for (it = list.begin(); it != list.end(); it++) {
      Connection &con = *(it->second);
      if (con.conType() != Connection::TCP)
        continue;
      TcpConnection& tcpCon = con.asTcp();
      if (tcpCon.tcpType() == TcpConnection::TCP_ACCEPT ||
              tcpCon.tcpType() == TcpConnection::TCP_CONNECT) {
        // if it is new connection  (when running full inspection
        // each connection appears twice - on each node)
        if (find(tcpCon) == _connections.end())
          _connections.push_back(GConnection(tcpCon));
      }
    }
  }

  dmtcp::list<GConnection>::iterator
  ConnectionGraph::find(TcpConnection &tcpCon)
  {
    dmtcp::list<GConnection>::iterator it = _connections.begin();
    for (; it != _connections.end(); it++) {
      if ((*it) == tcpCon) {
        return it;
      }
    }
    return _connections.end();
  }

  void ConnectionGraph::importProcess(ConnectionToFds &conToFd,
                                      ProcessInfo &processInfo)
  {
    ConnectionToFds::const_iterator cit;
    dmtcp::list<GProcess>::iterator pit;

    std::cerr << "\nimportProcess:\n";

    // Add process to _processes table
    _processes[processInfo.hostname()].push_front(GProcess(conToFd,
                                                           processInfo,
                                                           proc_out));
    pit = _processes[processInfo.hostname()].begin();

    // Add to _row_process Map table
    _row_processes[pit->pid] = &(*pit);

    std::cerr << "Add process: " << pit->pid
            << ". Result: " << _row_processes.find(pit->pid)->second->pid << "\n";

    // Run through all connections of the process
    for (cit = conToFd.begin(); cit != conToFd.end(); cit++) {
      ConnectionIdentifier conId = cit->first;
      Connection &con = ConnectionList::instance()[cit->first];

      // Process only network connections
      if (con.conType() != Connection::TCP)
        continue;
      TcpConnection &tcpCon = con.asTcp();

      // If this is not ESTABLISHED connection
      if (tcpCon.tcpType() != TcpConnection::TCP_ACCEPT &&
              tcpCon.tcpType() != TcpConnection::TCP_CONNECT) {
        continue;
      }

      // Map process to connection
      dmtcp::list<GConnection>::iterator gcit = find(tcpCon);
      if (gcit != _connections.end()) {
        gcit->addProc(conId, pit->index(), pit->hostname);
      }
    }
  }

  void ConnectionGraph::writeGraph(dmtcp::ostringstream &o)
  {
    dmtcp::list<GConnection>::iterator cit;
    ClusterProcesses::iterator cpit;
    dmtcp::list<GConnection*>::iterator gcit;
    dmtcp::map< dmtcp::string, dmtcp::list<GConnection *> > inhost_conn;
    dmtcp::list<GConnection*> interhost_conn;
    unsigned char *wmatrix = NULL;

    // Divide connections on two groups:
    // 1. All communicated processes are at one host
    // 2. communicated processes are at different hosts
    for (cit = _connections.begin(); cit != _connections.end(); cit++) {
      // If this is loopback connection - map it
      // for fast access
      if (cit->is_loop()) {
        inhost_conn[cit->hostname()].push_back((GConnection*)&(*cit));
      } else {
        interhost_conn.push_back((GConnection*)&(*cit));
      }
    }

    // Count max process index
    int conCnt = 0;
    for (cpit = _processes.begin(); cpit != _processes.end(); cpit++) {
      dmtcp::list<GProcess>::iterator pit = cpit->second.begin();
      for (; pit != cpit->second.end(); pit++) {
        if (pit->index() > conCnt)
          conCnt = pit->index();
      }
    }
    conCnt++;
    if( !show_all_conn ){
      wmatrix = new unsigned char[conCnt*conCnt];
      memset(wmatrix,0,conCnt*conCnt);
    }
    // Head of dot-file
    o << "digraph { \n";

    // Create nodes for processes
    int cnt;
    for (cnt = 0, cpit = _processes.begin(); cpit != _processes.end(); cpit++, cnt++) {
      dmtcp::list<GProcess>::iterator pit = cpit->second.begin();
      dmtcp::string cur_hostname = pit->hostname;
      o << "subgraph cluster" << cnt << " {\n";
      o << " label=\"" << cur_hostname << "\";\n";
      o << " color=blue;\n";
      // write all processes
      for (; pit != cpit->second.end(); pit++) {
        pit->writeNode(o);
      }
      // write all inhost connections
      if (sockets) {
        if (inhost_conn.find(cur_hostname) != inhost_conn.end()) {
          for (gcit = inhost_conn[cur_hostname].begin();
                  gcit != inhost_conn[cur_hostname].end();
                  gcit++) {
            if( show_all_conn )
              (*gcit)->writeConnection(o, conCnt);
            else
              (*gcit)->writeConnectionShort(o, wmatrix, conCnt);
          }
        }
      }

      o << "}\n";
    }

    // write all interhost connections
    if (sockets) {
      for (gcit = interhost_conn.begin(); gcit != interhost_conn.end(); gcit++) {
        // Write connection to the file
        if (show_all_conn)
          (*gcit)->writeConnection(o, conCnt);
        else
          (*gcit)->writeConnectionShort(o, wmatrix, conCnt);
      }
    }

    // write Parent - Child relationships
    if (parent_child) {
      DMTCP_process::iterator dit, dit1;
      for (dit = _row_processes.begin(); dit != _row_processes.end(); dit++) {
        std::cerr << "Inspect process: " << dit->second->procname
                << "[" << dit->second->pid << ","
                << dit->second->ppid << "]:\n";
        dit1 = _row_processes.find(dit->second->ppid);
        if (dit1 != _row_processes.end()) {
          std::cerr << "find " << dit1->second->procname
                  << "[" << dit1->second->pid << "]\n";
          o << " \"" << dit1->second->index() << "\" -> \"" << dit->second->index()
                  << "\" [ color=\"#FF0000\", style=\"bold\" ]\n";
        }
      }
    }
    o << "}\n";
    delete[] wmatrix;
  }
}

void process_input(int argc, char** argv);

int main(int argc, char** argv)
{
  initializeJalib();
  process_input(argc, argv);

  ConnectionGraph conGr(ConnectionList::instance());
  for (size_t i = 0; i < targets.size(); i++) {
    conGr.importProcess(targets[i]._conToFd, targets[i]._processInfo);
  }


  dmtcp::string out_string;
  dmtcp::ostringstream buf(out_string);
  conGr.writeGraph(buf);

  // Write output
  switch (output_type) {
  case TEXT:{
    std::ostream *o;
    dmtcp::ofstream *file;
    file = NULL;
    if( output_file.size() != 0 ){
      file = new dmtcp::ofstream(output_file.c_str());
      if( !file->is_open() ){
        std::cerr << "Cannot open output file: " + output_file + "\n";
        exit(0);
      }
      o = (std::ostream*)file;
    }else
      o = &std::cout;
    (*o) << buf.str();

    if( file ){
      (*file).close();
      o = NULL;
      file = NULL;
    }
    break;
  }
  case DOT:
    // Create pipe to dot
    if( output_format.size() == 0 ){
      output_format = "pdf";
    }
    dmtcp::string popen_str = output_tool + " -T" + output_format +
           " -o " + output_file;

    std::cerr << "Popen arg: " << popen_str
            << "\nInput len=" << buf.str().length() << "\n";

    output_format = "", gviz_params="";

    FILE *fp = popen(popen_str.c_str(), "w");
    if (!fp) {
      std::cerr << "Error in popen(\"" << output_file.c_str() << "\",\"w\")\n";
      return 0;
    }
    fprintf(fp, "%s", buf.str().c_str());
    pclose(fp);
  }
  return 0;
}

void process_input(int argc, char** argv)
{
  static const char* theUsage =
    "USAGE: dmtcp_inspector [-o <file>] [-t <tool>] [-cdaznh] <ckpt1.dmtcp> [ckpt2.dmtcp...]\n"
    "  -o, --out <file> - Write output to <file>\n"
    "  -t, --tool       - Graphviz tool to use. By default output is in dot-like format.\n"
    "  -c, --cred       - Add  information about parent-child relations\n"
    "  -d, --no-sock    - Remove information about socket connections\n"
    "  -a, --sock-all   - Add verbose information about socket connections\n"
    "  -z, --sock-half  - Also represent half-connections (when some *.mtcp files are missed)\n"
    "  -n, -node        - Verbose node names indication\n"
    "  -h, --help       - Display this help\n"
    "  -v, --version    - Display this help\n"
    "\n"
    "See " PACKAGE_URL " for more information.\n"
          ;
  int c;

  output_file.clear();
  output_tool = "dot";
  // No arguments => help
  if (argc == 1) {
    std::cerr << theUsage;
    exit(0);
  }

  // Process arguments
  while (1) {
    //    int this_option_optind = optind ? optind : 1;
    int option_index = 0;
    static struct option long_options[] = {
      {"out", 1, 0, 'o'},
      {"help", 0, 0, 'h'},
      {"version", 0, 0, 'v'},
      {"cred", 0, 0, 'c'},
      {"no-sock", 0, 0, 'd'},
      {"sock-all", 0, 0, 'a'},
      {"sock-half", 0, 0, 'z'},
      {"node", 1, 0, 'n'},
      {"tool", 1, 0, 't'},
      {"format",1,0, 'f'},
      {"param-gv",1,0, 'p'},
      {0, 0, 0, 0}
    };

    c = getopt_long(argc, argv, "o:hcdan:zt:", long_options, &option_index);
    if (c == -1)
      break;

    switch (c) {
    case 'c':
      parent_child = true;
      break;
    case 'd':
      sockets = false;
      break;
    case 'a':
      show_all_conn = true;
      break;
    case 'z':
      show_part_conn = true;
      break;
    case 'o':
      output_file = optarg;
      break;
    case 't':
      output_tool = optarg;
      output_type = DOT;
      break;
    case 'n':
      proc_out = (proc_out_t)atoi(optarg);
      break;
    case 'h':
      std::cerr << theUsage;
      exit(0);
    case 'v':
      std::cerr << DMTCP_VERSION_AND_COPYRIGHT_INFO;
      exit(0);
    case 'f':
      output_format = optarg;
      break;
    case 'p':
      gviz_params = optarg;
      break;
    default:
      printf("ERROR: getopt returned character code 0%o ??\n", c);
      exit(1);
    }
  }

  if (optind < argc) {
    std::cerr << "Loading checkpoint files:\n";
    for (int i = optind; i < argc; i++) {
      if (i < argc - 1)
        std::cerr << argv[i] << ", ";
      else
        std::cerr << argv[i] << "\n";
      targets.push_back(InspectTarget(argv[i]));
    }
  } else {
    std::cerr << theUsage;
    exit(0);
  }

  // Process input data

  if( proc_out >= OUT_MAX )
    proc_out = SHORT;

  if (!sockets) {
    show_all_conn = false;
  }
  if( show_part_conn ){
    show_all_conn = true;
  }

  if (parent_child)
    std::cerr << "Display parent-child relations\n";
  else
    std::cerr << "Don't display parent-child relations\n";

  if (sockets)
    std::cerr << "Display socket connections\n";
  else
    std::cerr << "Don't display socket connections\n";

  if (show_all_conn)
    std::cerr << "Verbose connection information\n";

  if (output_file.size()) {
    if (output_type == TEXT)
      std::cerr << "Output to: " << output_file << " in DOT format\n";
    else
      std::cerr << "Output to: " << output_file << " in PDF format using DOT command\n";
  } else
    std::cerr << "Output to: stdout\n";
}
