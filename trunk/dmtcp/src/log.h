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

#ifndef LOG_H
#define LOG_H

#include "dmtcpalloc.h"
#include "../jalib/jassert.h"
#include "synchronizationlogging.h"

#define LOG_IS_PATCHED_VALUE 1
#define LOG_IS_PATCHED_TYPE char
#define LOG_IS_PATCHED_SIZE sizeof(LOG_IS_PATCHED_TYPE)

#define LOG_IS_MERGED_VALUE 1
#define LOG_IS_MERGED_TYPE char
#define LOG_IS_MERGED_SIZE sizeof(LOG_IS_MERGED_TYPE)

#define LOG_OFFSET_FROM_START 32

namespace dmtcp
{
  class SynchronizationLog
  {
    public:
#ifdef JALIB_ALLOCATOR
      static void* operator new(size_t nbytes, void* p) { return p; }
      static void* operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }
      static void  operator delete(void* p) { JALLOC_HELPER_DELETE(p); }
#endif
      SynchronizationLog()
        : _path ("")
        , _cloneId(-1)
        , _size (-1)
        , _startAddr (NULL)
        , _log (NULL)
        , _index (0)
        , _dataSize (0)
        , _entryIndex (0)
        , _numEntries (NULL)
        , _isLoaded (false)
        , _isPatched (NULL)
        , _isMerged (NULL)
      {}

      SynchronizationLog(void *addr, size_t size)
        : _path ("")
        , _cloneId(-1)
        , _size (size)
        , _startAddr ((char*)addr)
        , _log (NULL)
        , _index (0)
        , _dataSize (0)
        , _entryIndex (0)
        , _numEntries (NULL)
        , _isLoaded (false)
        , _isPatched (NULL)
        , _isMerged (NULL)
      {}

      ~SynchronizationLog() {}

      void init (size_t size, bool readOnly = false, bool globalLog = false );
      void init2(long long int clone_id, size_t size = MAX_LOG_LENGTH, bool readOnly = false);
      void init3(const char *path, size_t size = MAX_LOG_LENGTH, bool readOnly = false);
      void init4(size_t size);
      void init_common(size_t size, bool readonly = false);
      void destroy();
      bool empty() { return numEntries() == 0; }
      bool isLoaded() { return _isLoaded; }
      bool isPatched() 
        { if (_isPatched == NULL ) return false; return *_isPatched; }
      bool isMerged()
        { if (_isMerged == NULL ) return false; return *_isMerged; }

      void resetIndex() { _index = 0; _entryIndex = 0; }
      void clearLog();

      void markAsPatched();
      void markAsMerged();
      
      bool isEndOfLog();

      size_t numEntries() { 
        if (_numEntries == NULL) return 0;
        return *_numEntries; 
      }
      void copyDataFrom(SynchronizationLog& other);
      void appendDataFrom(SynchronizationLog& other);

      log_entry_t getFirstEntryWithCloneID(int clone_id);
      int getNextEntry(log_entry_t& entry);
      int writeEntry(const log_entry_t& entry);
      int replaceEntry(const log_entry_t& entry);
      void getNextEntryHeader(log_entry_t& entry);
      void writeEntryHeader(const log_entry_t& entry);

    //private:
      char* logAddr() { return _log; }
      size_t dataSize() { return _index; }
      size_t currentIndex() { return _index; }
      void  setIndex(size_t index) { JASSERT(index < _size); _index = index; }
      size_t currentEntryIndex() { return _entryIndex; }
      void setEntryIndex(size_t index)
        { JASSERT(index <= *_numEntries); _entryIndex = index; }

    private:
      string  _path;
      long long int _cloneId;
      size_t  _size;
      char    *_startAddr;
      char    *_log;
      size_t  _index;
      size_t  _dataSize;
      size_t  _entryIndex;
      size_t *_numEntries;
      bool    _isLoaded;
      bool   *_isPatched;
      bool   *_isMerged;
  };

}

#endif
