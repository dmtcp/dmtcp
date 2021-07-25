#ifndef MPI_RECORD_REPLAY_H
#define MPI_RECORD_REPLAY_H

#include <mpi.h>

#include <algorithm>
#include <functional>
#include <mutex>
#include <typeinfo>

#include "jassert.h"
#include "jconvert.h"

#include "lower_half_api.h"

// Logs the MPI call to the global MPI calls log object (defined by the
// 'MpiRecordReplay' class). 'cb' specifies the callback that will be used
// to replay the MPI call while restoring the MPI state at restart time. 'fnc'
// represents the current MPI call.  'args ...' can be used to provide a
// variable-length list of arguments to be saved. The saved arguments are useful
// while replaying the call later.
#define LOG_CALL(cb, fnc, args...) \
  dmtcp_mpi::MpiRecordReplay::instance().record(cb, GENERATE_ENUM(fnc),  \
                                                GENERATE_FNC_PTR(fnc), args)

#define RESTORE_MPI_STATE() \
  dmtcp_mpi::MpiRecordReplay::instance().replay()

#define CLEAR_LOG() \
  dmtcp_mpi::MpiRecordReplay::instance().reset()

#define CLEAR_GROUP_LOGS(group) \
  dmtcp_mpi::MpiRecordReplay::instance().clearGroupLogs(group)

#define CLEAR_COMM_LOGS(comm) \
  dmtcp_mpi::MpiRecordReplay::instance().clearCommLogs(comm)

// Returns true if we are currently replaying the MPI calls from the saved MPI
// calls log; false, otherwise. Normally, this would be true while restoring
// the MPI state at restart time. All other times, this would return false.
#define LOGGING() \
  dmtcp_mpi::MpiRecordReplay::instance().isReplayOn()

// Calls the wrapper function corresponding to the given type 'type'. (The
// 'rec' object contains a pointer to the wrapper function.)
#define FNC_CALL(type, rec)                                                    \
  ({                                                                           \
    __typeof__(GENERATE_FNC_PTR(type))_real_MPI_## type =                      \
                                  rec.call(GENERATE_FNC_PTR(type));            \
    _real_MPI_ ## type;                                                        \
  })

#define CREATE_LOG_BUF(buf, len) dmtcp_mpi::FncArg(buf, len)


namespace dmtcp_mpi
{
  struct FncArg;
  class MpiRecord;
  class MpiRecordReplay;

  using mutex_t = std::mutex;
  using lock_t  = std::unique_lock<mutex_t>;
  using fcb_t   = std::function<int(const MpiRecord&)>;
  using mpi_record_vector_iterator_t = dmtcp::vector<MpiRecord*>::iterator;

  enum TYPE {
    TYPE_INT,
    TYPE_INT_PTR,
    TYPE_INT_ARRAY,
    TYPE_VOID_PTR,
    TYPE_MPI_USER_FNC,
  };

  // Struct for saving arbitrary function arguments
  struct FncArg
  {
    void *_data;
    enum TYPE _type;

    FncArg(const void *data, size_t len, dmtcp_mpi::TYPE type)
      : _data(JALLOC_HELPER_MALLOC(len))
    {
      _type = type;
      if (_data && data) {
        memcpy(_data, data, len);
      }
    }

    // This constructor is only used by CREATE_LOG_BUF
    FncArg(const void *data, size_t len)
      : _data(JALLOC_HELPER_MALLOC(len))
    {
      // Default _type set to TYPE_INT_ARRAY because this constructor is used
      // by CREATE_LOG_BUF in MPI_Cart functions.
      _type = dmtcp_mpi::TYPE_INT_ARRAY;
      if (_data && data) {
        memcpy(_data, data, len);
      }
    }

    ~FncArg()
    {
      if (!_data) {
        JALLOC_HELPER_FREE(_data);
      }
    }

    // On restart, we will use the restore family of functions in
    // record-replay.cpp. A typical line of code there is:
    //   int arg = rec.args(n);
    // rec.args() returns a 'const FncArg&'.
    // The copy constructor for arg will invokes one of these casts
    // from FncArg to the type for arg. So in this example, the code
    // is invoking the 'int cast operator', resulting in:
    //   int arg = *(int*)(((FncArg)rec.args(n))._data);

    // Returns an int corresponding to the saved argument
    operator int() const
    {
      return *(int*)_data;
    }

    // Returns an int pointer to saved argument
    // This also handles types like MPI_Datatype* in MPICH,
    // because MPI_Datatype is an integer in this implementation.
    // This doesn't handle MPI_Aint* because MPI_Aint
    // is 8 bytes (long int).
    operator int*() const
    {
      if (_type == dmtcp_mpi::TYPE_INT_ARRAY) {
        return (int*)_data;
      } else if (_type == dmtcp_mpi::TYPE_INT_PTR) {
        return *(int**)_data;
      } else {
        JASSERT(false).Text("Unsupported arg type");
        return NULL;
      }
    }

    // This is used to handle MPI_Aint*, because in MPICH
    // MPI_Aint is 8 bytes (long int).
    operator long*() const
    {
      return (long*)_data;
    }

    // Returns a void pointer to saved argument
    operator void*() const
    {
      return *(void**)_data;
    }

    operator MPI_User_function*() const
    {
      return *(MPI_User_function**)_data;
    }
  };

  template<typename T>
  FncArg FncArgTyped(T data)
  {
    int a;
    int *b;
    void *c;
    MPI_User_function *d;

    if (typeid(data) == typeid(a)) {
      return FncArg(&data, sizeof(data), TYPE_INT);
    }
    if (typeid(data) == typeid(b)) {
      return FncArg(&data, sizeof(data), TYPE_INT_PTR);
    }
    if (typeid(data) == typeid(c)) {
      return FncArg(&data, sizeof(data), TYPE_VOID_PTR);
    }
    if (typeid(data) == typeid(d)) {
      return FncArg(&data, sizeof(data), TYPE_MPI_USER_FNC);
    }
    JASSERT(false).Text("Unkown type for FncArg");
    return FncArgTyped(-1);
  }

  // Represent a single call record
  class MpiRecord
  {
    public:
#ifdef JALIB_ALLOCATOR
      static void* operator new(size_t nbytes, void* p) { return p; }
      static void* operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }
      static void  operator delete(void* p) { JALLOC_HELPER_DELETE(p); }
#endif
      MpiRecord(fcb_t cb, MPI_Fncs type, void *fnc)
        : _cb(cb), _type(type), _fnc(fnc)
      {
      }

      ~MpiRecord()
      {
        _args.clear();
      }

      // Base case to stop the recursion
      void addArgs()
      {
      }

      // Handle one complex argument
      void addArgs(const FncArg arg)
      {
        _args.push_back(arg);
      }

      // Handle one MPI_User_function argument
      void addArgs(MPI_User_function *arg)
      {
        _args.push_back(FncArgTyped((void*)arg));
      }

      // Handle one simple argument
      template<typename T>
      void addArgs(const T arg)
      {
        _args.push_back(FncArgTyped(arg));
      }

      // Handle list of arguments
      template<typename T, typename... Targs>
      void addArgs(const T car, const Targs... cdr)
      {
        addArgs(car);
        addArgs(cdr...);
      }

      // Execute the restore callback for this record
      int play() const
      {
        return _cb(*this);
      }

      // Returns a reference to the 'n'-th function argument object
      const FncArg& args(int n) const
      {
        return _args[n];
      }

      // Returns the enum MPI_Fncs type of the current MPI record object
      MPI_Fncs getType() const
      {
        return _type;
      }

      // Returns a pointer to the wrapper function corresponding to this MPI
      // record object
      template<typename T>
      T call(T fptr) const
      {
        return T(_fnc);
      }

    private:
      fcb_t _cb; // Callback to invoke to replay this MPI call
      MPI_Fncs _type; // enum MPI_Fncs type of this MPI call
      void *_fnc; // Pointer to the wrapper function of this MPI call
      dmtcp::vector<FncArg> _args; // List of argument objects for this MPI call
  };

  // Singleton class representing the entire log of MPI calls, useful for
  // saving and restoring the MPI state
  class MpiRecordReplay
  {
    public:
#ifdef JALIB_ALLOCATOR
      static void* operator new(size_t nbytes, void* p) { return p; }
      static void* operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }
      static void  operator delete(void* p) { JALLOC_HELPER_DELETE(p); }
#endif

      // Returns the singleton instance of the MpiRecordReplay class
      static MpiRecordReplay& instance()
      {
        static MpiRecordReplay _records;
        return _records;
      }

      // Records an MPI call with its arguments in the MPI calls log. Returns
      // a pointer to the inserted MPI record object (containing the details
      // of the call).
      template<typename T, typename... Targs>
      MpiRecord* record(fcb_t cb, MPI_Fncs type,
                        const T fPtr, const Targs... args)
      {
        lock_t lock(_mutex);
        MpiRecord *rec = new MpiRecord(cb, type, (void*)fPtr);
        if (rec) {
          rec->addArgs(args...);
          _records.push_back(rec);
        }
        return rec;
      }

      // Replays the MPI calls from the log. Returns MPI_SUCCESS on success.
      int replay()
      {
        int rc = MPI_SUCCESS;
        lock_t lock(_mutex);
        _replayOn = true;
        for (MpiRecord* rec : _records) {
          rc = rec->play();
          if (rc != MPI_SUCCESS) {
            break;
          }
        }
        _replayOn = false;
        return rc;
      }

      void reset()
      {
        lock_t lock(_mutex);
        for (MpiRecord* i : _records) {
          delete i;
        }
        _replayOn = false;
        _records.clear();
      }

      void cleanComms(dmtcp::set<MPI_Comm> &staleComms)
      {
        bool setChanged = false;
        std::function<bool(const MpiRecord*)> isStaleComm =
          [&](const MpiRecord *rec) {
            switch (rec->getType()) {
              case GENERATE_ENUM(Comm_split):
              {
                MPI_Comm c = rec->args(0);
                MPI_Comm newcomm = rec->args(3);
                if (staleComms.count(c) > 0) {
                  staleComms.erase(c);
                  staleComms.insert(newcomm);
                  setChanged = true;
                  return true;
                }
                return false;
              }
              case GENERATE_ENUM(Comm_split_type):
              {
                MPI_Comm c = rec->args(0);
                MPI_Comm newcomm = rec->args(4);
                if (staleComms.count(c) > 0) {
                  staleComms.erase(c);
                  staleComms.insert(newcomm);
                  setChanged = true;
                  return true;
                }
                return false;
              }
              case GENERATE_ENUM(Comm_create):
              {
                MPI_Comm c = rec->args(0);
                MPI_Comm newcomm = rec->args(2);
                if (staleComms.count(c) > 0) {
                  staleComms.erase(c);
                  staleComms.insert(newcomm);
                  setChanged = true;
                  return true;
                }
                return false;
              }
              case GENERATE_ENUM(Comm_dup):
              {
                MPI_Comm c = rec->args(0);
                MPI_Comm newcomm = rec->args(1);
                if (staleComms.count(c) > 0) {
                  staleComms.erase(c);
                  staleComms.insert(newcomm);
                  setChanged = true;
                  return true;
                }
                return false;
              }
              case GENERATE_ENUM(Comm_set_errhandler):
              case GENERATE_ENUM(Attr_put):
              case GENERATE_ENUM(Attr_delete):
              {
                MPI_Comm c = rec->args(0);
                if (staleComms.count(c) > 0) {
                  staleComms.erase(c);
                  return true;
                }
                return false;
              }
              default:
                return false;
            } };
        do {
          mpi_record_vector_iterator_t it =
            remove_if(_records.begin(), _records.end(), isStaleComm);
          _records.erase(it, _records.end());
        } while (setChanged);
      }

      void clearGroupLogs(MPI_Group group)
      {
        lock_t lock(_mutex);
        dmtcp::set<MPI_Comm> staleComms;
        std::function<bool(const MpiRecord*)> isValidGroup =
          [group, &staleComms](const MpiRecord *rec) {
            switch (rec->getType()) {
              case GENERATE_ENUM(Group_incl):
              {
                MPI_Group g = rec->args(0);
                return group == g;
              }
              case GENERATE_ENUM(Comm_group):
              {
                // MPI_Comm comm = rec->args(0);
                MPI_Group g = rec->args(1);
                return group == g;
              }
              case GENERATE_ENUM(Comm_create):
              {
                // MPI_Comm comm = rec->args(0);
                MPI_Group g = rec->args(1);
                MPI_Comm oldcomm = rec->args(2);
                if (group == g) {
                  staleComms.insert(oldcomm); // save this
                  return true;
                }
                return false;
              }
              default:
                return false;
            } };
        mpi_record_vector_iterator_t it =
          remove_if(_records.begin(), _records.end(), isValidGroup);
        _records.erase(it, _records.end());
        cleanComms(staleComms);
      }

      void clearCommLogs(MPI_Comm comm)
      {
        lock_t lock(_mutex);
        dmtcp::set<MPI_Comm> staleComms;
        std::function<bool(const MpiRecord*)> isValidComm =
          [comm, &staleComms](const MpiRecord *rec) {
            switch (rec->getType()) {
              case GENERATE_ENUM(Comm_split):
              {
                MPI_Comm c = rec->args(0);
                MPI_Comm newcomm = rec->args(3);
                if (c == comm) {
                  staleComms.insert(newcomm);
                  return true;
                }
                return false;
              }
              case GENERATE_ENUM(Comm_split_type):
              {
                MPI_Comm c = rec->args(0);
                MPI_Comm newcomm = rec->args(4);
                if (c == comm) {
                  staleComms.insert(newcomm);
                  return true;
                }
                return false;
              }
              case GENERATE_ENUM(Comm_create):
              {
                MPI_Comm c = rec->args(0);
                MPI_Comm newcomm = rec->args(2);
                if (c == comm) {
                  staleComms.insert(newcomm);
                  return true;
                }
                return false;
              }
              case GENERATE_ENUM(Comm_dup):
              {
                MPI_Comm c = rec->args(0);
                MPI_Comm newcomm = rec->args(1);
                if (c == comm) {
                  staleComms.insert(newcomm);
                  return true;
                }
                return false;
              }
              case GENERATE_ENUM(Comm_set_errhandler):
              case GENERATE_ENUM(Attr_put):
              case GENERATE_ENUM(Attr_delete):
              {
                MPI_Comm c = rec->args(0);
                return staleComms.count(c) > 0;
              }
              default:
                return false;
            } };
        mpi_record_vector_iterator_t it =
          remove_if(_records.begin(), _records.end(), isValidComm);
        _records.erase(it, _records.end());
        cleanComms(staleComms);
      }

      // Returns true if we are currently replaying the MPI calls
      bool isReplayOn()
      {
        // FIXME: This needs locking. But we can't do this here, otherwise it'll
        // deadlock
        return !_replayOn;
      }

      void printRecords(bool print);
    private:
      // Pvt. constructor
      MpiRecordReplay()
        : _records(),
          _replayOn(false),
          _mutex()
      {
      }

      // Virtual Ids Table
      dmtcp::vector<MpiRecord*> _records;
      // True on restart, false otherwise
      bool _replayOn;
      // Lock on list
      mutex_t _mutex;
  }; // class MpiRecordReplay


  // Restores the MPI communicators and returns MPI_SUCCESS on success
  extern int restoreComms(const MpiRecord& );

  // Restores the MPI groups and returns MPI_SUCCESS on success
  extern int restoreGroups(const MpiRecord& );

  // Restores the MPI types and returns MPI_SUCCESS on success
  extern int restoreTypes(const MpiRecord& );

  // Restores the MPI cartesian communicators and returns MPI_SUCCESS on success
  extern int restoreCarts(const MpiRecord& );

  // Restores the MPI ops and returns MPI_SUCCESS on success
  extern int restoreOps(const MpiRecord& );

  // Restores the MPI requests and returns MPI_SUCCESS on success
  extern int restoreRequests(const MpiRecord& );

}; // namespace dmtcp_mpi

// Restores the MPI state by recreating the communicator, groups, types, etc.
// post restart
extern void restoreMpiLogState();

#endif // ifndef MPI_RECORD_REPLAY_H
