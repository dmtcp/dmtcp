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

#ifdef RECORD_REPLAY

#define LOG_IS_UNIFIED_VALUE 1
#define LOG_IS_UNIFIED_TYPE char
#define LOG_IS_UNIFIED_SIZE sizeof(LOG_IS_UNIFIED_TYPE)

#define LOG_OFFSET_FROM_START 64

namespace dmtcp
{
  typedef struct LogMetadata {
    bool   isUnified;
    size_t size;
    size_t dataSize;
    size_t numEntries;
  } LogMetadata;

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
        , _startAddr (NULL)
        , _log (NULL)
        , _index (0)
        , _entryIndex (0)
        , _size (NULL)
        , _dataSize (NULL)
        , _numEntries (NULL)
        , _isUnified (NULL)
      {}

      ~SynchronizationLog() {}

      void initGlobalLog(const char* path, size_t size = MAX_LOG_LENGTH);
      void initOnThreadCreation(size_t size = MAX_LOG_LENGTH);
      void initForCloneId(clone_id_t clone_id, size_t size = MAX_LOG_LENGTH);

    private:
      void init2(clone_id_t clone_id, size_t size, bool mapWithNoReserveFlag);
      void init3(const char *path, size_t size, bool mapWithNoReserveFlag);

      void init_common(size_t size);

    public:
      void   destroy();
      void   unmap();
      void   map_in(const char *path, size_t size,
                        bool mapWithNoReserveFlag);
      void   map_in();
      size_t currentIndex() { return _index; }
      size_t currentEntryIndex() { return _entryIndex; }
      bool   empty() { return numEntries() == 0; }
      size_t dataSize() { return _dataSize == NULL ? 0 : *_dataSize; }
      size_t numEntries() { return _numEntries == NULL ? 0 : *_numEntries; }
      bool   isUnified() { return _isUnified == NULL ? false : *_isUnified; }
      bool   isMappedIn() { return _startAddr != NULL; }
      string getPath() { return _path; }
      void   mergeLogs(dmtcp::vector<clone_id_t> clone_ids);

      int    getNextEntry(log_entry_t& entry);
      int    appendEntry(const log_entry_t& entry);
      void   replaceEntryAtOffset(const log_entry_t& entry, size_t index);

    private:
      void   resetIndex() { _index = 0; _entryIndex = 0; }
      void   resetMarkers()
        { resetIndex(); *_dataSize = 0; *_numEntries = 0; *_isUnified = false; }

      void   writeEntryHeaderAtOffset(const log_entry_t& entry, size_t index);
      size_t getEntryHeaderAtOffset(log_entry_t& entry, size_t index);
      int    writeEntryAtOffset(const log_entry_t& entry, size_t index);
      int    getEntryAtOffset(log_entry_t& entry, size_t index);

    private:
      string  _path;
      clone_id_t _cloneId;
      char   *_startAddr;
      char   *_log;
      size_t  _index;
      size_t  _entryIndex;
      size_t *_size;
      size_t _savedSize; // Only used between checkpoints
      size_t *_dataSize;
      size_t *_numEntries;
      bool   *_isUnified;
  };

}
#endif

#endif
