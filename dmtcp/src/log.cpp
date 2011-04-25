
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
                                               bool register_globally)
{
  size_t size = MAX_LOG_LENGTH;
  bool mapWithNoReserveFlag = SYNC_IS_REPLAY;
  init2(clone_id, size, mapWithNoReserveFlag);
  if (SYNC_IS_RECORD && register_globally) {
    register_in_global_log_list(clone_id);
  }

  JTRACE ("Initialized thread local synchronization log for preexisting threads." )
    (_path) (*_size) (mapWithNoReserveFlag);
}

void dmtcp::SynchronizationLog::init2(clone_id_t clone_id, size_t size,
                                      bool mapWithNoReserveFlag)
{
  char buf[RECORD_LOG_PATH_MAX];
#ifdef __x86_64__
  snprintf(buf, RECORD_LOG_PATH_MAX, "%s_%Ld", RECORD_LOG_PATH, clone_id);
#else
  snprintf(buf, RECORD_LOG_PATH_MAX, "%s_%ld", RECORD_LOG_PATH, clone_id);
#endif
  init3(buf, size, mapWithNoReserveFlag);
  _cloneId = clone_id;
}

void dmtcp::SynchronizationLog::init3(const char *path, size_t size,
                                      bool mapWithNoReserveFlag)
{
  JASSERT(_startAddr == NULL);
  JASSERT(_log == NULL);
  JASSERT(_index == 0);
  JASSERT(_size == NULL);
  JASSERT(_dataSize == NULL);
  JASSERT(_entryIndex == 0);
  JASSERT(_numEntries == NULL);
  JASSERT(_isUnified == NULL);
  /* map_in calls init_common if appropriate. */
  map_in(path, size, mapWithNoReserveFlag);
}

void dmtcp::SynchronizationLog::init_common(size_t size)
{
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
    unmap();
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

void dmtcp::SynchronizationLog::unmap()
{
  if (_startAddr == NULL) {
    return;
  }
  // Save the size in case we want to remap after this unmap:
  _savedSize = *_size; 
  JASSERT(_real_munmap(_startAddr, *_size) == 0) (JASSERT_ERRNO) (*_size) (_startAddr);
}

void dmtcp::SynchronizationLog::map_in(const char *path, size_t size,
                                       bool mapWithNoReserveFlag)
{
#if 0
  bool created = false;
  struct stat buf;
  if (stat(path, &buf) == -1 && errno == ENOENT) {
    created = true;
    /* Make sure to clear old state, if this is not the first checkpoint.
       This case can happen (not first checkpoint, but create the log file)
       if log files have been moved or deleted. */
    _startAddr = NULL;
    destroy();
  }
#endif
  int fd = _real_open(path, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
  JASSERT(path==NULL || fd != -1);
  JASSERT(fd == -1 || _real_lseek(fd, size, SEEK_SET) == (off_t)size);
  if (fd != -1) Util::writeAll(fd, "", 1);
  // FIXME: Instead of MAP_NORESERVE, we may also choose to back it with
  // /dev/null which would also _not_ allocate pages until needed.
  int mmapProt = PROT_READ | PROT_WRITE;
  int mmapFlags;
  if (fd != -1) {
    mmapFlags = MAP_SHARED;
  } else {
    mmapFlags = MAP_PRIVATE | MAP_ANONYMOUS;
  }
  if (mapWithNoReserveFlag) {
    mmapFlags |= MAP_NORESERVE;
  }
  SET_IN_MMAP_WRAPPER();
  /* _startAddr may not be null if this is not the first checkpoint. */
  if (_startAddr == NULL) {
    _startAddr = (char*) _real_mmap(0, size, mmapProt, mmapFlags, fd, 0);
  } else {
    void *retval = (char*) _real_mmap(_startAddr, size, mmapProt,
                                      mmapFlags | MAP_FIXED, fd, 0);
    JASSERT ( retval == (void *)_startAddr );
  }
  UNSET_IN_MMAP_WRAPPER();
  JASSERT(_startAddr != MAP_FAILED) (JASSERT_ERRNO);
  _real_close(fd);
  _path = path == NULL ? "" : path;
#if 0
  if (created || _size == NULL) {
    /* We either had to create the file, or this is the first checkpoint. */
    init_common(size);
  }
#else
  init_common(size);
#endif
}

void dmtcp::SynchronizationLog::map_in()
{
  char path_copy[RECORD_LOG_PATH_MAX] = {'\0'};
  strncpy(path_copy, _path.c_str(), RECORD_LOG_PATH_MAX);
  /* We don't want to pass a pointer to _path, because that could be reset
     as part of a subsequent call to destroy(). */
  map_in(path_copy, _savedSize, false);
}

int dmtcp::SynchronizationLog::getNextEntry(log_entry_t& entry)
{
  int entrySize = getEntryAtOffset(entry, _index);
  if (entrySize != 0) {
    _index += entrySize;
    _entryIndex++;
  }
  return entrySize;
}

// Reads the entry from log and returns the length of entry
int dmtcp::SynchronizationLog::getEntryAtOffset(log_entry_t& entry, size_t index)
{
  if (index == *_dataSize || getEntryHeaderAtOffset(entry, index) == 0) {
    entry = EMPTY_LOG_ENTRY;
    return 0;
  }

  size_t event_size;
  GET_EVENT_SIZE(GET_COMMON(entry, event), event_size);

  JASSERT ((index + log_event_common_size + event_size) <= *_dataSize)
    (_index) (log_event_common_size) (event_size) (*_dataSize);

#if 1
  READ_ENTRY_FROM_LOG(&_log[index + log_event_common_size], entry);
#else
  void *ptr;
  GET_EVENT_DATA_PTR(entry, ptr);
  memcpy(ptr, &_log[index + log_event_common_size], event_size);
#endif

  return log_event_common_size + event_size;
}

int dmtcp::SynchronizationLog::appendEntry(const log_entry_t& entry)
{
  ssize_t entrySize = writeEntryAtOffset(entry, *_dataSize);
  *_dataSize += entrySize;
  *_numEntries += 1;
  return entrySize;
}

void dmtcp::SynchronizationLog::replaceEntryAtOffset(const log_entry_t& entry,
                                                    size_t index)
{
  // only allow it for pthread_create call
  JASSERT(GET_COMMON(entry, event) == pthread_create_event);

  log_entry_t old_entry = EMPTY_LOG_ENTRY;
  JASSERT(getEntryAtOffset(old_entry, index) != 0);

  JASSERT(GET_COMMON(entry, log_id) == GET_COMMON(old_entry, log_id) &&
          IS_EQUAL_FIELD(entry, old_entry, pthread_create, thread) &&
          IS_EQUAL_FIELD(entry, old_entry, pthread_create, thread) &&
          IS_EQUAL_FIELD(entry, old_entry, pthread_create, start_routine) &&
          IS_EQUAL_FIELD(entry, old_entry, pthread_create, attr) &&
          IS_EQUAL_FIELD(entry, old_entry, pthread_create, arg));

  writeEntryAtOffset(entry, index);
}

/* Move appropriate markers to the end, so that we enter "append" mode. */
void dmtcp::SynchronizationLog::moveMarkersToEnd()
{
  _index = *_dataSize;
  _entryIndex = *_numEntries;
}

int dmtcp::SynchronizationLog::writeEntryAtOffset(const log_entry_t& entry,
                                                 size_t index)
{
  if (__builtin_expect(_startAddr == 0, 0)) {
    JASSERT(false);
  }

  int event_size;
  GET_EVENT_SIZE(GET_COMMON(entry, event), event_size);

  JASSERT ((_index + log_event_common_size + event_size) < *_size)
    .Text ("Log size too large");

  writeEntryHeaderAtOffset(entry, index);

#if 1
  WRITE_ENTRY_TO_LOG(&_log[index + log_event_common_size], entry);
#else
  void *ptr;
  GET_EVENT_DATA_PTR(entry, ptr);
  memcpy(&_log[index + log_event_common_size], ptr, event_size);
#endif
  return log_event_common_size + event_size;
}

size_t dmtcp::SynchronizationLog::getEntryHeaderAtOffset(log_entry_t& entry,
                                                      size_t index)
{
  JASSERT ((index + log_event_common_size) <= *_dataSize) (index) (*_dataSize);

#ifdef NO_LOG_ENTRY_TO_BUFFER
  memcpy(&entry.header, &_log[index], log_event_common_size);
#else
  char* buffer = &_log[index];

  memcpy(&GET_COMMON(entry, event), buffer, sizeof(GET_COMMON(entry, event)));
  buffer += sizeof(GET_COMMON(entry, event));
  memcpy(&GET_COMMON(entry, isOptional), buffer, sizeof(GET_COMMON(entry, isOptional)));
  buffer += sizeof(GET_COMMON(entry, isOptional));
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

void dmtcp::SynchronizationLog::writeEntryHeaderAtOffset(const log_entry_t& entry,
                                                        size_t index)
{
#ifdef NO_LOG_ENTRY_TO_BUFFER
  memcpy(&_log[index], &entry.header, log_event_common_size);
#else
  char* buffer = &_log[index];

  memcpy(buffer, &GET_COMMON(entry, event), sizeof(GET_COMMON(entry, event)));
  buffer += sizeof(GET_COMMON(entry, event));
  memcpy(buffer, &GET_COMMON(entry, isOptional), sizeof(GET_COMMON(entry, isOptional)));
  buffer += sizeof(GET_COMMON(entry, isOptional));
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
  /* We can use dynamic allocation here AS LONG AS the net effect on the memory
     layout or usage is zero. In other words, free() what you malloc() and
     'delete' what you 'new' before returning from this function. */
  dmtcp::vector<dmtcp::SynchronizationLog *> sync_logs;
  dmtcp::vector<log_entry_t> curr_entries;
  log_entry_t entry;
  size_t num_entries = 0;
  for (size_t i = 0; i < clone_ids.size(); i++) {
    dmtcp::SynchronizationLog *slog = NULL;
    if (clone_id_to_log_table.find(clone_ids[i]) != clone_id_to_log_table.end()
        && clone_id_to_log_table[clone_ids[i]]->isMappedIn()) {
      // Don't map it in again if it's already present.
      slog = clone_id_to_log_table[clone_ids[i]];
    } else {
      slog = new dmtcp::SynchronizationLog();
      slog->initForCloneId(clone_ids[i], true);
    }
    JTRACE("Entries for this thread") (clone_ids[i]) (slog->numEntries());
    slog->getNextEntry(entry);
    num_entries += slog->numEntries();
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
        sync_logs[i]->getNextEntry(entry);
        curr_entries[i] = entry;
        entry_index++;
      }
    }
  }
  for (size_t i = 0; i < clone_ids.size(); i++) {
    // Only delete if we called new.
    if (clone_id_to_log_table.find(clone_ids[i]) == clone_id_to_log_table.end()
        || ! clone_id_to_log_table[clone_ids[i]]->isMappedIn()) {
      sync_logs[i]->destroy();
      delete sync_logs[i];
    }
  }
  
  *_isUnified = true;
  resetIndex();
}

#endif
