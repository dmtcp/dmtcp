
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

/*
 * TODO:
 * 
 * 1. How is my_clone_id handled in case of exec()? How do we make sure that we
 *    still write to the same log assigned for this thread ?
 * 2. Get rid of all XXX_return events.
 *
 */

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "constants.h"
#include "log.h"
#include "synchronizationlogging.h"
#include "syscallwrappers.h"
#include "util.h"
#include "../jalib/jassert.h"

#ifdef RECORD_REPLAY
void dmtcp::SynchronizationLog::initGlobalLog(const char *path, size_t size)
{
  bool mapWithNoReserveFlag = SYNC_IS_RECORD;
  init3(path, size, mapWithNoReserveFlag);
  JTRACE ("Initialized global synchronization log path to" )
    (_path) (*_size) (mapWithNoReserveFlag);
}

void dmtcp::SynchronizationLog::initOnThreadCreation(size_t size)
{
  bool mapWithNoReserveFlag = SYNC_IS_REPLAY;
  init2(my_clone_id, size, mapWithNoReserveFlag);
  if (SYNC_IS_RECORD) {
    register_in_global_log_list(my_clone_id);
  }

  JTRACE ("Initialized thread local synchronization log path to" )
    (_path) (*_size) (mapWithNoReserveFlag);
}

void dmtcp::SynchronizationLog::initForCloneId(clone_id_t clone_id,
                                                         size_t size)
{
  bool mapWithNoReserveFlag = SYNC_IS_REPLAY;
  init2(clone_id, size, mapWithNoReserveFlag);
  if (SYNC_IS_RECORD) {
    register_in_global_log_list(clone_id);
  }

  JTRACE ("Initialized thread local synchronization log for preexisting threads." )
    (_path) (*_size) (mapWithNoReserveFlag);
}

void dmtcp::SynchronizationLog::init2(clone_id_t clone_id, size_t size,
                                      bool mapWithNoReserveFlag)
{
  char buf[RECORD_LOG_PATH_MAX];
  snprintf(buf, RECORD_LOG_PATH_MAX, "%s_%Ld", RECORD_LOG_PATH, clone_id);

  init3(buf, size, mapWithNoReserveFlag);
  _cloneId = clone_id;
}

void dmtcp::SynchronizationLog::init3(const char *path, size_t size,
                                      bool mapWithNoReserveFlag)
{
  JASSERT(_startAddr == NULL);
  JASSERT(_log == NULL);
  JASSERT(_index == 0);
  JASSERT(_dataSize == NULL);
  JASSERT(_entryIndex == 0);
  JASSERT(_numEntries == NULL);
  JASSERT(_isUnified == NULL);

  //JASSERT(path != NULL);

  int fd = _real_open(path, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
  JASSERT(path==NULL || fd != -1);

  JASSERT(fd == -1 || _real_lseek(fd, size, SEEK_SET) == (off_t)size);
  if (fd != -1) Util::writeAll(fd, "", 1);

  // FIXME: Instead of MAP_NORESERVE, we may also choose to back it with
  // /dev/null which would also _not_ allocate pages until needed.
  int mmapProt = PROT_READ | PROT_WRITE;
  int mmapFlags;

  if (fd != -1)
    mmapFlags = MAP_SHARED;
  else
    mmapFlags = MAP_PRIVATE | MAP_ANONYMOUS;

  if (mapWithNoReserveFlag) {
    mmapFlags |= MAP_NORESERVE;
  }

  SET_IN_MMAP_WRAPPER();
  _startAddr = (char*) _real_mmap(0, size, mmapProt, mmapFlags, fd, 0);
  UNSET_IN_MMAP_WRAPPER();

  JASSERT(_startAddr != MAP_FAILED) (JASSERT_ERRNO);

  _real_close(fd);

  _path = path == NULL ? "" : path;
  init_common(size);
}

void dmtcp::SynchronizationLog::init_common(size_t size)
{
  if (SYNC_IS_RECORD) {
    memset(_startAddr, 0, 4096);
  }

  JASSERT(sizeof (LogMetadata) < LOG_OFFSET_FROM_START) (sizeof(LogMetadata));

  LogMetadata *metadata = (LogMetadata *) _startAddr;

  _isUnified  = &(metadata->isUnified);
  _numEntries = &(metadata->numEntries);
  _dataSize = &(metadata->dataSize);
  _size = &(metadata->size);

  *_size = size;
  _log = _startAddr + LOG_OFFSET_FROM_START;
}

void dmtcp::SynchronizationLog::destroy()
{
  if (_startAddr != NULL) {
    JASSERT(_real_munmap(_startAddr, *_size) == 0) (JASSERT_ERRNO) (*_size) (_startAddr);
  }
  _startAddr = _log = NULL;
  _path = "";
  _size = NULL;
  _index = 0;
  _entryIndex = 0;
  _dataSize = NULL;
  _numEntries = NULL;
  _isUnified = NULL;
}

void dmtcp::SynchronizationLog::clearLog()
{
  JASSERT(_startAddr != NULL);
  bzero(_startAddr, *_dataSize);
  resetIndex();
  _entryIndex = 0;
  *_dataSize = 0;
  *_numEntries = 0;
}

int dmtcp::SynchronizationLog::getNextEntry(log_entry_t& entry)
{
  int entrySize = getEntryAtIndex(entry, _index);
  if (entrySize != 0) {
    _index += entrySize;
    _entryIndex++;
  }
  return entrySize;
}

// Reads the entry from log and returns the length of entry
int dmtcp::SynchronizationLog::getEntryAtIndex(log_entry_t& entry, size_t index)
{
  if (index == *_dataSize || getEntryHeaderAtIndex(entry, index) == 0) {
    entry = EMPTY_LOG_ENTRY;
    return 0;
  }

  size_t event_size;
  GET_EVENT_SIZE(GET_COMMON(entry, event), event_size);

  JASSERT ((index + log_event_common_size + event_size) <= *_dataSize)
    (_index) (log_event_common_size) (event_size) (*_dataSize);

  READ_ENTRY_FROM_LOG(&_log[index + log_event_common_size], entry);

  return log_event_common_size + event_size;
}

int dmtcp::SynchronizationLog::appendEntry(const log_entry_t& entry)
{
  JASSERT(_index == 0 && _entryIndex == 0);
  ssize_t entrySize = writeEntryAtIndex(entry, *_dataSize);
  *_dataSize += entrySize;
  *_numEntries += 1;
  return entrySize;
}

int dmtcp::SynchronizationLog::writeEntryAtIndex(const log_entry_t& entry,
                                                 size_t index)
{
  if (__builtin_expect(_startAddr == 0, 0)) {
    JASSERT(false);
  }

  int event_size;
  GET_EVENT_SIZE(GET_COMMON(entry, event), event_size);

  JASSERT ((_index + log_event_common_size + event_size) < *_size)
    .Text ("Log size too large");

  writeEntryHeaderAtIndex(entry, index);

  WRITE_ENTRY_TO_LOG(&_log[index + log_event_common_size], entry);
  return log_event_common_size + event_size;
}

size_t dmtcp::SynchronizationLog::getEntryHeaderAtIndex(log_entry_t& entry,
                                                      size_t index)
{
  JASSERT ((index + log_event_common_size) <= *_dataSize) (index) (*_dataSize);

#ifdef NO_LOG_ENTRY_TO_BUFFER
  memcpy(&entry.header, &_log[index], log_event_common_size);
#else
  char* buffer = &_log[index];

  memcpy(&GET_COMMON(entry, event), buffer, sizeof(GET_COMMON(entry, event)));
  buffer += sizeof(GET_COMMON(entry, event));
  memcpy(&GET_COMMON(entry, log_id), buffer, sizeof(GET_COMMON(entry, log_id)));
  buffer += sizeof(GET_COMMON(entry, log_id));
  memcpy(&GET_COMMON(entry, clone_id), buffer, sizeof(GET_COMMON(entry, clone_id)));
  buffer += sizeof(GET_COMMON(entry, clone_id));
  memcpy(&GET_COMMON(entry, my_errno), buffer, sizeof(GET_COMMON(entry, my_errno)));
  buffer += sizeof(GET_COMMON(entry, my_errno));
  memcpy(&GET_COMMON(entry, retval), buffer, sizeof(GET_COMMON(entry, retval)));
  buffer += sizeof(GET_COMMON(entry, retval));

  JASSERT((buffer - &_log[index]) == log_event_common_size)
    (index) (log_event_common_size) (buffer);
#endif

  if (GET_COMMON(entry, clone_id) == 0) {
    return 0;
  }
  return log_event_common_size;
}

void dmtcp::SynchronizationLog::writeEntryHeaderAtIndex(const log_entry_t& entry,
                                                        size_t index)
{
#ifdef NO_LOG_ENTRY_TO_BUFFER
  memcpy(&_log[index], &entry.header, log_event_common_size);
#else
  char* buffer = &_log[index];

  memcpy(buffer, &GET_COMMON(entry, event), sizeof(GET_COMMON(entry, event)));
  buffer += sizeof(GET_COMMON(entry, event));
  memcpy(buffer, &GET_COMMON(entry, log_id), sizeof(GET_COMMON(entry, log_id)));
  buffer += sizeof(GET_COMMON(entry, log_id));
  memcpy(buffer, &GET_COMMON(entry, clone_id), sizeof(GET_COMMON(entry, clone_id)));
  buffer += sizeof(GET_COMMON(entry, clone_id));
  memcpy(buffer, &GET_COMMON(entry, my_errno), sizeof(GET_COMMON(entry, my_errno)));
  buffer += sizeof(GET_COMMON(entry, my_errno));
  memcpy(buffer, &GET_COMMON(entry, retval), sizeof(GET_COMMON(entry, retval)));
  buffer += sizeof(GET_COMMON(entry, retval));

  JASSERT((buffer - &_log[index]) == log_event_common_size)
    (index) (log_event_common_size) (buffer);
#endif
}

void dmtcp::SynchronizationLog::mergeLogs(dmtcp::vector<clone_id_t> clone_ids)
{
  dmtcp::vector<dmtcp::SynchronizationLog> sync_logs;
  dmtcp::vector<log_entry_t> curr_entries;
  log_entry_t entry;

  size_t num_entries = 0;
  for (size_t i = 0; i < clone_ids.size(); i++) {
    dmtcp::SynchronizationLog slog;
    slog.initForCloneId(clone_ids[i]);
    JTRACE("Entries for this thread") (clone_ids[i]) (slog.numEntries());
    slog.getNextEntry(entry);
    num_entries += slog.numEntries();
    sync_logs.push_back(slog);
    curr_entries.push_back(entry);
  }
  JTRACE("Total number of log entries") (num_entries);

  // Prepare this unified_log:
  resetMarkers();

  // TODO: In future we can keep and array of sizes of each entry so that we
  // don't have to go through the quadratic algorithm for merging entries from
  // different logs. Here is pseudo-code:
  //   1. for all i; entry_size[i] <- size of entry with log_id == i
  //   2. Now create a similar array, this time with the offset for this entry
  //   from the beginning of log.
  //   3. Now we iterate through one log at a time and insert the entries at
  //   the current offset in the unified log.
  size_t entry_index = 0;
  while(entry_index < num_entries) {
    for (size_t i = 0 ; i < clone_ids.size() ; i++)
    {
      if (GET_COMMON(curr_entries[i], log_id) < entry_index &&
          GET_COMMON(curr_entries[i], log_id) != 0) {
        JASSERT(false) (i) (GET_COMMON(curr_entries[i], log_id)) (entry_index)
            .Text("Unreachable");
      }
      if (GET_COMMON(curr_entries[i], log_id) == entry_index &&
          GET_COMMON(curr_entries[i], clone_id) != 0) {
        appendEntry(curr_entries[i]);
        sync_logs[i].getNextEntry(entry);
        curr_entries[i] = entry;
        entry_index++;
      }
    }
  }
  resetIndex();
}

void dmtcp::SynchronizationLog::annotate()
{
  /*
   * This function performs several tasks for the log:
     1) Annotates pthread_create events with stack information from their
        respective return events.
     2) Annotates getline events with is_realloc information from their
        respective return events.
   */
  
  JTRACE ( "Annotating log." );
  log_entry_t entry = EMPTY_LOG_ENTRY;

  resetIndex();
  size_t prev_index = currentIndex();
  while (getNextEntry(entry) != 0) {
    if (GET_COMMON(entry,event) == pthread_create_event) {
      updateEntryFromNextPthreadCreate(entry);
      JASSERT(0 != writeEntryAtIndex(entry, prev_index));

    } else if (GET_COMMON(entry,event) == getline_event) {
      updateEntryFromNextGetline(entry);
      JASSERT(0 != writeEntryAtIndex(entry, prev_index));
    }

    prev_index = currentIndex();
  }

  resetIndex();
  *_isUnified = LOG_IS_UNIFIED_VALUE;

  JTRACE ( "log annotation finished. Opening annotated log file." ) 
    ( _path );
}

void dmtcp::SynchronizationLog::updateEntryFromNextPthreadCreate(log_entry_t& entry)
{
  /* Finds the next pthread_create return event with the same thread,
   * start_routine, attr, and arg as given.
   * NOTE: IT SEARCHES FROM CURRENT INDEX ONWARDS
   */

  size_t index = currentIndex();
  log_entry_t e = EMPTY_LOG_ENTRY;
  while (index < dataSize()) {
    if (getNextEntry(e) == 0) {
      JASSERT(false)
        .Text("Looks like this pthread_create never returned on RECORD");
    }
    if (GET_COMMON(e, event) == pthread_create_event_return &&
        IS_EQUAL_FIELD(entry, e, pthread_create, thread) &&
        IS_EQUAL_FIELD(entry, e, pthread_create, start_routine) &&
        IS_EQUAL_FIELD(entry, e, pthread_create, attr) &&
        IS_EQUAL_FIELD(entry, e, pthread_create, arg)) {
      break;
    }
  }

  SET_FIELD_FROM(entry, pthread_create, stack_size, e);
  SET_FIELD_FROM(entry, pthread_create, stack_addr, e);
}

void dmtcp::SynchronizationLog::updateEntryFromNextGetline(log_entry_t& entry)
{
  /* Finds the next getline return event with the same lineptr, n and stream as
   * given.
   * NOTE: IT SEARCHES FROM CURRENT INDEX ONWARDS
   */
  size_t index = currentIndex();
  log_entry_t e = EMPTY_LOG_ENTRY;
  while (index < dataSize()) {
    if (getEntryAtIndex(e, index) == 0) {
      JASSERT(false)
        .Text("Looks like this getline never returned on RECORD");
    }
    if (GET_COMMON(e, event) == getline_event_return &&
        IS_EQUAL_FIELD(entry, e, getline, lineptr) &&
        IS_EQUAL_FIELD(entry, e, getline, stream)) {
      break;
    }
  }

  SET_FIELD_FROM(entry, getline, is_realloc, e);
}

void dmtcp::SynchronizationLog::copyDataFrom(SynchronizationLog& other)
{
  if (other.numEntries() == 0) return;

  JASSERT(_log != NULL && other.logAddr() != NULL) (_log);
  JASSERT(numEntries() == other.numEntries()) (numEntries()) (other.numEntries());
  JASSERT(dataSize() == other.dataSize()) (dataSize()) (other.dataSize());

  memcpy(_log, other.logAddr(), other.dataSize());
  resetIndex();
}

void dmtcp::SynchronizationLog::appendDataFrom(SynchronizationLog& other)
{
  if (other.numEntries() == 0) return;
  JASSERT(_log != NULL && other.logAddr() != NULL) (_log);

  JASSERT(_index == *_dataSize);
  JASSERT(dataSize() + other.dataSize() < *_size)
    (dataSize()) (other.dataSize()) (*_size);

  memcpy(&_log[dataSize()], other.logAddr(), other.dataSize());
  _index += other.dataSize();
  *_dataSize += other.dataSize();
  *_numEntries += other.numEntries();
}

/* Given a clone_id, this returns the entry in patch_list with the lowest index
   and same clone_id.  If clone_id is 0, return the oldest entry, regardless of
   clone_id. If clone_id != 0 and no entry found, returns EMPTY_LOG_ENTRY. */
log_entry_t dmtcp::SynchronizationLog::getNextEntryWithCloneID(int clone_id)
{
  log_entry_t entry = EMPTY_LOG_ENTRY;

  while (getNextEntry(entry) != 0) {
    if (GET_COMMON(entry, clone_id) == clone_id) {
      break;
    }
  }

  if (GET_COMMON(entry, clone_id) == 0) {
    return entry;
  }

  size_t event_size = 0;
  GET_EVENT_SIZE(GET_COMMON(entry, event), event_size);
  size_t entry_size = log_event_common_size + event_size;


  JASSERT(*_dataSize >= _index) (*_dataSize) (_index);

  memmove(&_log[_index - entry_size], &_log[_index], *_dataSize - _index);

  _index -= entry_size;
  _entryIndex -= 1;

  *_dataSize -= entry_size;
  *_numEntries -= 1;

  return entry;
}

void dmtcp::SynchronizationLog::patchLog()
{
  JTRACE ( "Begin log patching." );

  dmtcp::SynchronizationLog record_patched_log;
  dmtcp::SynchronizationLog patch_list;
  record_patched_log.initGlobalLog(RECORD_PATCHED_LOG_PATH, MAX_LOG_LENGTH * 10);
  patch_list.initGlobalLog(NULL, MAX_LOG_LENGTH * 10);

  log_entry_t entry = EMPTY_LOG_ENTRY;
  log_entry_t temp = EMPTY_LOG_ENTRY;
  log_entry_t entry_to_write = EMPTY_LOG_ENTRY;
  size_t write_ret = 0;
  // Read one log entry at a time from the file on disk, and write the patched
  // version out to record_patched_log_fd.
  
  resetIndex();
  while (getNextEntry(entry) != 0) {
    if (GET_COMMON(entry,event) == exec_barrier_event) {
      // Nothing may move past an exec barrier. Dump everything in patch_list
      // into the new log before the exec barrier.
      record_patched_log.appendDataFrom(patch_list);
      patch_list.clearLog();
      continue;
    }
    // XXX: shouldn't this be treated as exec?
    if (GET_COMMON(entry,event) == pthread_kill_event) {
      JTRACE ( "Found a pthread_kill in log. Not moving it." );
      write_ret = record_patched_log.appendEntry(entry);
      continue;
    }

    JASSERT (GET_COMMON(entry,clone_id) != 0)
      .Text("Encountered a clone_id of 0 in log.");
    
    if (isUnlock(entry)) {
      patch_list.resetIndex();
      temp = patch_list.getNextEntryWithCloneID(GET_COMMON(entry,clone_id));
      while (GET_COMMON(temp,clone_id) != 0) {
        write_ret = record_patched_log.appendEntry(temp);
        temp = patch_list.getNextEntryWithCloneID(GET_COMMON(entry,clone_id));
      }
      patch_list.resetIndex();
      write_ret = record_patched_log.appendEntry(entry);
    } else {
      patch_list.appendEntry(entry);
    }
  }
  // If the patch_list is not empty (patch_list_idx != 0), then there were
  // some leftover log entries that were not balanced. So we tack them on to
  // the very end of the log.
  if (patch_list.empty() == false) {
    JTRACE ( "Extra log entries. Tacking them onto end of log." )
      (patch_list.dataSize());
      record_patched_log.appendDataFrom(patch_list);
      patch_list.clearLog();
  }
  patch_list.destroy();

  copyDataFrom(record_patched_log);
  record_patched_log.destroy();
  resetIndex();
}
#endif
