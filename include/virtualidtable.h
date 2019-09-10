/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 *   This file is part of the dmtcp/src module of DMTCP (DMTCP:dmtcp/src).  *
 *                                                                          *
 *  DMTCP is free software: you can redistribute it and/or        *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP is distributed in the hope that it will be useful,      *
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

#include "../jalib/jalloc.h"
#include "../jalib/jassert.h"
#include "../jalib/jconvert.h"
#include "../jalib/jfilesystem.h"
#include "../jalib/jserialize.h"

#include "dmtcp.h"
#include "dmtcpalloc.h"
#include "util.h"

#define MAX_VIRTUAL_ID 999

namespace dmtcp
{
template<typename IdType>
class VirtualIdTable
{
  protected:
    void _do_lock_tbl()
    {
      JASSERT(DmtcpMutexLock(&tblLock) == 0) (JASSERT_ERRNO);
    }

    void _do_unlock_tbl()
    {
      JASSERT(DmtcpMutexUnlock(&tblLock) == 0) (JASSERT_ERRNO);
    }

  public:
#ifdef JALIB_ALLOCATOR
    static void *operator new(size_t nbytes, void *p) { return p; }

    static void *operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }

    static void operator delete(void *p) { JALLOC_HELPER_DELETE(p); }
#endif // ifdef JALIB_ALLOCATOR
    VirtualIdTable(string typeStr, IdType base, size_t max = MAX_VIRTUAL_ID)
    {
      DmtcpMutexInit(&tblLock, DMTCP_MUTEX_NORMAL);
      _do_lock_tbl();
      _idMapTable.clear();
      _do_unlock_tbl();
      _typeStr = typeStr;
      _base = base;
      _max = max;
      resetNextVirtualId();
    }

    void resetNextVirtualId()
    {
      _nextVirtualId = (IdType)((unsigned long)_base + 1);
    }

    IdType addOneToNextVirtualId()
    {
      IdType ret = _nextVirtualId;

      _nextVirtualId = (IdType)((unsigned long)_nextVirtualId + 1);
      if ((unsigned long)_nextVirtualId >= (unsigned long)_base + _max) {
        resetNextVirtualId();
      }
      return ret;
    }

    size_t size()
    {
      _do_lock_tbl();
      size_t size = _idMapTable.size();
      _do_unlock_tbl();
      return size;
    }

    void clear()
    {
      _do_lock_tbl();
      _idMapTable.clear();
      resetNextVirtualId();
      _do_unlock_tbl();
    }

    void postRestart()
    {
      _do_lock_tbl();
      _idMapTable.clear();
      resetNextVirtualId();
      _do_unlock_tbl();
    }

    void resetOnFork(IdType newBase)
    {
      _base = newBase;
      DmtcpMutexInit(&tblLock, DMTCP_MUTEX_NORMAL);
      resetNextVirtualId();
    }

    bool getNewVirtualId(IdType *id)
    {
      bool res = false;

      _do_lock_tbl();
      if (_idMapTable.size() < _max) {
        size_t count = 0;
        while (1) {
          IdType newId = addOneToNextVirtualId();
          id_iterator i = _idMapTable.find(newId);
          if (i == _idMapTable.end()) {
            *id = newId;
            res = true;
            break;
          }
          if (++count == _max) {
            break;
          }
        }
      }
      _do_unlock_tbl();
      return res;
    }

    bool isIdCreatedByCurrentProcess(IdType id)
    {
      return (size_t)id > (size_t)getpid() &&
             (size_t)id <= (size_t)getpid() + _max;
    }

    bool virtualIdExists(IdType id)
    {
      bool retVal = false;

      _do_lock_tbl();
      id_iterator j = _idMapTable.find(id);
      if (j != _idMapTable.end()) {
        retVal = true;
      }

      _do_unlock_tbl();
      return retVal;
    }

    bool realIdExists(IdType id)
    {
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

    void updateMapping(IdType virtualId, IdType realId)
    {
      _do_lock_tbl();
      _idMapTable[virtualId] = realId;
      _do_unlock_tbl();
    }

    void erase(IdType virtualId)
    {
      _do_lock_tbl();
      _idMapTable.erase(virtualId);
      _do_unlock_tbl();
    }

    void printMaps()
    {
      ostringstream out;

      out << _typeStr << " Maps\n";
      out << "      Virtual" << "  ->  " << "Real" << "\n";
      for (id_iterator i = _idMapTable.begin(); i != _idMapTable.end(); ++i) {
        IdType virtualId = i->first;
        IdType realId = i->second;
        out << "\t" << virtualId << "\t->   " << realId << "\n";
      }
      JTRACE("Virtual To Real Mappings:") (_idMapTable.size()) (out.str());
    }

    vector<IdType>getIdVector()
    {
      vector<IdType>idVec;
      _do_lock_tbl();
      for (id_iterator i = _idMapTable.begin(); i != _idMapTable.end(); ++i) {
        idVec.push_back(i->first);
      }
      _do_unlock_tbl();
      return idVec;
    }

    virtual IdType virtualToReal(IdType virtualId)
    {
      IdType retVal = 0;

      /* This code is called from MTCP while the checkpoint thread is holding
         the JASSERT log lock. Therefore, don't call JTRACE/JASSERT/JINFO/etc. in
         this function. */
      _do_lock_tbl();
      id_iterator i = _idMapTable.find(virtualId);
      if (i == _idMapTable.end()) {
        retVal = virtualId;
      } else {
        retVal = i->second;
      }
      _do_unlock_tbl();
      return retVal;
    }

    virtual IdType realToVirtual(IdType realId)
    {
      /* This code is called from MTCP while the checkpoint thread is holding
         the JASSERT log lock. Therefore, don't call JTRACE/JASSERT/JINFO/etc. in
         this function. */
      _do_lock_tbl();
      for (id_iterator i = _idMapTable.begin(); i != _idMapTable.end(); ++i) {
        if (realId == i->second) {
          _do_unlock_tbl();
          return i->first;
        }
      }

      _do_unlock_tbl();
      return realId;
    }

    void serialize(jalib::JBinarySerializer &o)
    {
      JSERIALIZE_ASSERT_POINT("VirtualIdTable:");
      o & _idMapTable;
      JSERIALIZE_ASSERT_POINT("EOF");
      printMaps();
    }

    void writeMapsToFile(int fd)
    {
      string file = "/proc/self/fd/" + jalib::XToString(fd);
      string mapFile = jalib::Filesystem::ResolveSymlink(file);

      JASSERT(mapFile.length() > 0) (mapFile);
      JTRACE("Write Maps to file") (mapFile);

      // Lock fileset before any operations
      Util::lockFile(fd);
      _do_lock_tbl();
      JASSERT(lseek(fd, 0, SEEK_END) != -1);

      jalib::JBinarySerializeWriterRaw mapwr(mapFile, fd);
      mapwr & _idMapTable;

      _do_unlock_tbl();
      Util::unlockFile(fd);
    }

    void readMapsFromFile(int fd)
    {
      string file = "/proc/self/fd/" + jalib::XToString(fd);
      string mapFile = jalib::Filesystem::ResolveSymlink(file);

      JASSERT(mapFile.length() > 0) (mapFile);
      JTRACE("Read Maps from file") (mapFile);

      // No need to lock the file as we are the only process using this fd.
      // Util::lockFile(fd);
      _do_lock_tbl();

      jalib::JBinarySerializeReaderRaw maprd(mapFile, fd);
      maprd.rewind();

      while (!maprd.isEOF()) {
        maprd & _idMapTable;
      }

      _do_unlock_tbl();

      // Util::unlockFile(fd);

      printMaps();
    }

  private:
    string _typeStr;
    DmtcpMutex tblLock;

  protected:
    typedef typename map<IdType, IdType>::iterator id_iterator;
    map<IdType, IdType>_idMapTable;
    IdType _base;
    size_t _max;
    IdType _nextVirtualId;
};
}
#endif // ifndef VIRTUAL_ID_TABLE_H
