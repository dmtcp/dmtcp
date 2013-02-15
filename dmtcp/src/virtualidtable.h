/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#ifndef VIRTUAL_ID_TABLE_H
#define VIRTUAL_ID_TABLE_H

#include <sys/types.h>
#include "../jalib/jserialize.h"
#include "../jalib/jfilesystem.h"
#include "../jalib/jalloc.h"
#include "dmtcpalloc.h"
#include "util.h"

#define INITIAL_VIRTUAL_ID 1
#define MAX_VIRTUAL_ID 999

namespace dmtcp
{
  template <typename IdType>
    class VirtualIdTable
    {
      protected:
        void _do_lock_tbl() {
          JASSERT(pthread_mutex_lock(&tblLock) == 0) (JASSERT_ERRNO);
        }

        void _do_unlock_tbl() {
          JASSERT(pthread_mutex_unlock(&tblLock) == 0) (JASSERT_ERRNO);
        }

      public:
#ifdef JALIB_ALLOCATOR
        static void* operator new(size_t nbytes, void* p) { return p; }
        static void* operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }
        static void  operator delete(void* p) { JALLOC_HELPER_DELETE(p); }
#endif
        VirtualIdTable(dmtcp::string typeStr) {
          pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
          tblLock = lock;
          _do_lock_tbl();
          _idMapTable.clear();
          _nextVirtualId = INITIAL_VIRTUAL_ID;
          _do_unlock_tbl();
          _typeStr = typeStr;
        }

        size_t size() {
          _do_lock_tbl();
          size_t size = _idMapTable.size();
          _do_unlock_tbl();
          return size;
        }

        void clear() {
          _do_lock_tbl();
          _idMapTable.clear();
          _nextVirtualId = INITIAL_VIRTUAL_ID;
          _do_unlock_tbl();
        }

        void postRestart() {
          _do_lock_tbl();
          _idMapTable.clear();
          _nextVirtualId = INITIAL_VIRTUAL_ID;
          _do_unlock_tbl();
        }

        void resetOnFork() {
          pthread_mutex_t newlock = PTHREAD_MUTEX_INITIALIZER;
          tblLock = newlock;
          _nextVirtualId = INITIAL_VIRTUAL_ID;
        }

        pid_t getNewVirtualId() {
          pid_t tid = -1;

          _do_lock_tbl();
          if (_idMapTable.size() < MAX_VIRTUAL_ID) {

            int count = 0;
            while (1) {
              tid = getpid() + _nextVirtualId++;
              if (_nextVirtualId >= MAX_VIRTUAL_ID) {
                _nextVirtualId = INITIAL_VIRTUAL_ID;
              }
              id_iterator i = _idMapTable.find(tid);
              if (i == _idMapTable.end()) {
                break;
              }
              if (++count == MAX_VIRTUAL_ID) {
                break;
              }
            }
          }
          _do_unlock_tbl();
          return tid;
        }

        bool isIdCreatedByCurrentProcess(IdType id) {
          return id > getpid() && id <= getpid() + MAX_VIRTUAL_ID;
        }

        bool virtualIdExists(IdType id) {
          bool retVal = false;
          _do_lock_tbl();
          id_iterator j = _idMapTable.find ( id );
          if ( j != _idMapTable.end() )
            retVal = true;

          _do_unlock_tbl();
          return retVal;
        }

        bool realIdExists(IdType id) {
          bool retval = false;
          _do_lock_tbl();
          for (id_iterator i = _idMapTable.begin(); i != _idMapTable.end(); ++i) {
            if (i->second == id) {
              retval = true;
              break;
            }
          }
          _do_unlock_tbl();
          return retval;
        }

        void updateMapping(IdType virtualId, IdType realId) {
          _do_lock_tbl();
          _idMapTable[virtualId] = realId;
          _do_unlock_tbl();
        }

        void erase(IdType virtualId) {
          _do_lock_tbl();
          _idMapTable.erase( virtualId );
          _do_unlock_tbl();
        }

        void printMaps() {
          ostringstream out;
          out << _typeStr << " Maps\n";
          out << "      Virtual" << "  ->  " << "Real" << "\n";
          for ( id_iterator i = _idMapTable.begin(); i != _idMapTable.end(); ++i ) {
            IdType virtualId = i->first;
            IdType realId  = i->second;
            out << "\t" << virtualId << "\t->   " << realId << "\n";
          }
          JTRACE("Virtual To Real Mappings:") (_idMapTable.size()) (out.str());
        }

        dmtcp::vector< IdType > getIdVector() {
          dmtcp::vector< IdType > idVec;
          _do_lock_tbl();
          for ( id_iterator i = _idMapTable.begin(); i != _idMapTable.end(); ++i )
            idVec.push_back ( i->first );
          _do_unlock_tbl();
          return idVec;
        }


        IdType virtualToReal(IdType virtualId) {
          IdType retVal = 0;

          if (virtualId == -1) {
            return virtualId;
          }

          /* This code is called from MTCP while the checkpoint thread is holding
             the JASSERT log lock. Therefore, don't call JTRACE/JASSERT/JINFO/etc. in
             this function. */
          _do_lock_tbl();
          id_iterator i = _idMapTable.find(virtualId < -1 ? abs(virtualId)
                                           : virtualId);
          if (i == _idMapTable.end()) {
            _do_unlock_tbl();
            return virtualId;
          }

          retVal = virtualId < -1 ? (-i->second) : i->second;
          _do_unlock_tbl();
          return retVal;
        }

        IdType realToVirtual(IdType realId) {
          if (realId == -1) {
            return realId;
          }

          /* This code is called from MTCP while the checkpoint thread is holding
             the JASSERT log lock. Therefore, don't call JTRACE/JASSERT/JINFO/etc. in
             this function. */
          _do_lock_tbl();
          for (id_iterator i = _idMapTable.begin(); i != _idMapTable.end(); ++i) {
            if ( realId == i->second ) {
              _do_unlock_tbl();
              return i->first;
            }
          }

          _do_unlock_tbl();
          return realId;
        }


        void serialize(jalib::JBinarySerializer& o) {
          JSERIALIZE_ASSERT_POINT ( "dmtcp::VirtualIdTable:" );
          o.serializeMap(_idMapTable);
          JSERIALIZE_ASSERT_POINT( "EOF" );
          printMaps();
        }

        void writeMapsToFile(int fd) {
          dmtcp::string file = "/proc/self/fd/" + jalib::XToString(fd);
          dmtcp::string mapFile = jalib::Filesystem::ResolveSymlink(file);

          JASSERT (mapFile.length() > 0) (mapFile);
          JTRACE ("Write Maps to file") (mapFile);

          // Lock fileset before any operations
          Util::lockFile(fd);
          _do_lock_tbl();
          JASSERT(lseek(fd, 0, SEEK_END) != -1);

          jalib::JBinarySerializeWriterRaw mapwr(mapFile, fd);
          mapwr.serializeMap(_idMapTable);

          _do_unlock_tbl();
          Util::unlockFile(fd);
        }

        void readMapsFromFile(int fd) {
          dmtcp::string file = "/proc/self/fd/" + jalib::XToString(fd);
          dmtcp::string mapFile = jalib::Filesystem::ResolveSymlink(file);

          JASSERT(mapFile.length() > 0) (mapFile);
          JTRACE("Read Maps from file") (mapFile);

          Util::lockFile(fd);
          _do_lock_tbl();

          jalib::JBinarySerializeReaderRaw maprd(mapFile, fd);
          maprd.rewind();

          while (!maprd.isEOF()) {
            maprd.serializeMap(_idMapTable);
          }

          _do_unlock_tbl();
          Util::unlockFile(fd);

          printMaps();
          close(fd);
          // FIXME:
          unlink(mapFile.c_str());
        }

      private:
        IdType _nextVirtualId;
        dmtcp::string _typeStr;
        pthread_mutex_t tblLock;
      protected:
        typedef typename dmtcp::map<IdType, IdType>::iterator id_iterator;
        dmtcp::map<IdType, IdType> _idMapTable;
    };
}

#endif
