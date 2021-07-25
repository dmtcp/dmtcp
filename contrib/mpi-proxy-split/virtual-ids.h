#pragma once
#ifndef MPI_VIRTUAL_IDS_H
#define MPI_VIRTUAL_IDS_H

#include <mpi.h>
#include <mutex>

#include "virtualidtable.h"
#include "jassert.h"
#include "jconvert.h"
#include "split_process.h"
#include "dmtcp.h"

// Convenience macros
#define MpiCommList  dmtcp_mpi::MpiVirtualization<MPI_Comm>
#define MpiGroupList dmtcp_mpi::MpiVirtualization<MPI_Group>
#define MpiTypeList  dmtcp_mpi::MpiVirtualization<MPI_Datatype>
#define MpiOpList    dmtcp_mpi::MpiVirtualization<MPI_Op>
#define MpiCommKeyvalList    dmtcp_mpi::MpiVirtualization<int>
#define MpiRequestList    dmtcp_mpi::MpiVirtualization<MPI_Request>
# define NEXT_FUNC(func)                                                       \
  ({                                                                           \
    static __typeof__(&MPI_##func)_real_MPI_## func =                          \
                                                (__typeof__(&MPI_##func)) - 1; \
    if (_real_MPI_ ## func == (__typeof__(&MPI_##func)) - 1) {                 \
      _real_MPI_ ## func = (__typeof__(&MPI_##func))pdlsym(MPI_Fnc_##func);    \
    }                                                                          \
    _real_MPI_ ## func;                                                        \
  })

#define REAL_TO_VIRTUAL_COMM(id) \
  MpiCommList::instance("MpiComm", MPI_COMM_NULL).realToVirtual(id)
#define VIRTUAL_TO_REAL_COMM(id) \
  MpiCommList::instance("MpiComm", MPI_COMM_NULL).virtualToReal(id)
#define ADD_NEW_COMM(id) \
  MpiCommList::instance("MpiComm", MPI_COMM_NULL).onCreate(id)
#define REMOVE_OLD_COMM(id) \
  MpiCommList::instance("MpiComm", MPI_COMM_NULL).onRemove(id)
#define UPDATE_COMM_MAP(v, r) \
  MpiCommList::instance("MpiComm", MPI_COMM_NULL).updateMapping(v, r)

#define REAL_TO_VIRTUAL_GROUP(id) \
  MpiGroupList::instance("MpiGroup", MPI_GROUP_NULL).realToVirtual(id)
#define VIRTUAL_TO_REAL_GROUP(id) \
  MpiGroupList::instance("MpiGroup", MPI_GROUP_NULL).virtualToReal(id)
#define ADD_NEW_GROUP(id) \
  MpiGroupList::instance("MpiGroup", MPI_GROUP_NULL).onCreate(id)
#define REMOVE_OLD_GROUP(id) \
  MpiGroupList::instance("MpiGroup", MPI_GROUP_NULL).onRemove(id)
#define UPDATE_GROUP_MAP(v, r) \
  MpiGroupList::instance("MpiGroup", MPI_GROUP_NULL).updateMapping(v, r)

#define REAL_TO_VIRTUAL_TYPE(id) \
  MpiTypeList::instance("MpiType", MPI_DATATYPE_NULL).realToVirtual(id)
#define VIRTUAL_TO_REAL_TYPE(id) \
  MpiTypeList::instance("MpiType", MPI_DATATYPE_NULL).virtualToReal(id)
#define ADD_NEW_TYPE(id) \
  MpiTypeList::instance("MpiType", MPI_DATATYPE_NULL).onCreate(id)
#define REMOVE_OLD_TYPE(id) \
  MpiTypeList::instance("MpiType", MPI_DATATYPE_NULL).onRemove(id)
#define UPDATE_TYPE_MAP(v, r) \
  MpiTypeList::instance("MpiType", MPI_DATATYPE_NULL).updateMapping(v, r)

#define REAL_TO_VIRTUAL_OP(id) \
  MpiOpList::instance("MpiOp", MPI_OP_NULL).realToVirtual(id)
#define VIRTUAL_TO_REAL_OP(id) \
  MpiOpList::instance("MpiOp", MPI_OP_NULL).virtualToReal(id)
#define ADD_NEW_OP(id) \
  MpiOpList::instance("MpiOp", MPI_OP_NULL).onCreate(id)
#define REMOVE_OLD_OP(id) \
  MpiOpList::instance("MpiOp", MPI_OP_NULL).onRemove(id)
#define UPDATE_OP_MAP(v, r) \
  MpiOpList::instance("MpiOp", MPI_OP_NULL).updateMapping(v, r)

#define REAL_TO_VIRTUAL_COMM_KEYVAL(id) \
  MpiOpList::instance("MpiCommKeyval", 0).realToVirtual(id)
#define VIRTUAL_TO_REAL_COMM_KEYVAL(id) \
  MpiOpList::instance("MpiCommKeyval", 0).virtualToReal(id)
#define ADD_NEW_COMM_KEYVAL(id) \
  MpiOpList::instance("MpiCommKeyval", 0).onCreate(id)
#define REMOVE_OLD_COMM_KEYVAL(id) \
  MpiOpList::instance("MpiCommKeyval", 0).onRemove(id)
#define UPDATE_COMM_KEYVAL_MAP(v, r) \
  MpiOpList::instance("MpiCommKeyval", 0).updateMapping(v, r)

#if 1
#define REAL_TO_VIRTUAL_REQUEST(id) \
  MpiRequestList::instance("MpiRequest", MPI_REQUEST_NULL).realToVirtual(id)
#define VIRTUAL_TO_REAL_REQUEST(id) \
  MpiRequestList::instance("MpiRequest", MPI_REQUEST_NULL).virtualToReal(id)
#define ADD_NEW_REQUEST(id) \
  MpiRequestList::instance("MpiRequest", MPI_REQUEST_NULL).onCreate(id)
#define REMOVE_OLD_REQUEST(id) \
  MpiRequestList::instance("MpiRequest", MPI_REQUEST_NULL).onRemove(id)
#define UPDATE_REQUEST_MAP(v, r) \
  MpiRequestList::instance("MpiRequest", MPI_REQUEST_NULL).updateMapping(v, r)
#else
#define VIRTUAL_TO_REAL_REQUEST(id) id
#define ADD_NEW_REQUEST(id) id
#define UPDATE_REQUEST_MAP(v, r) r
#endif

namespace dmtcp_mpi
{

  template<typename T>
  class MpiVirtualization
  {
    using mutex_t = std::mutex;
    using lock_t  = std::unique_lock<mutex_t>;

    public:
#ifdef JALIB_ALLOCATOR
      static void* operator new(size_t nbytes, void* p) { return p; }
      static void* operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }
      static void  operator delete(void* p) { JALLOC_HELPER_DELETE(p); }
#endif
      static MpiVirtualization& instance(const char *name, T nullId)
      {
	// FIXME:
	// dmtcp_mpi::MpiVirtualization::instance("MpiGroup", 1)
	//                                       ._vIdTable.printMaps(true)
	// to access _virTableMpiGroup in GDB.
	// We need a cleaner way to access it.
	if (strcmp(name, "MpiOp") == 0) {
	  static MpiVirtualization<T> _virTableMpiOp(name, nullId);
	  return _virTableMpiOp;
	} else if (strcmp(name, "MpiComm") == 0) {
	  static MpiVirtualization<T> _virTableMpiComm(name, nullId);
	  return _virTableMpiComm;
	} else if (strcmp(name, "MpiGroup") == 0) {
	  static MpiVirtualization<T> _virTableMpiGroup(name, nullId);
	  return _virTableMpiGroup;
	} else if (strcmp(name, "MpiType") == 0) {
	  static MpiVirtualization<T> _virTableMpiType(name, nullId);
	  return _virTableMpiType;
	} else if (strcmp(name, "MpiCommKeyval") == 0) {
	  static MpiVirtualization _virTableMpiCommKeyval(name, nullId);
	  return _virTableMpiCommKeyval;
	} else if (strcmp(name, "MpiRequest") == 0) {
	  static MpiVirtualization _virTableMpiRequest(name, nullId);
	  return _virTableMpiRequest;
	}
	JWARNING(false)(name)(nullId).Text("Unhandled type");
	static MpiVirtualization _virTableNoSuchObject(name, nullId);
	return _virTableNoSuchObject;
      }

      T virtualToReal(T virt)
      {
        // Don't need to virtualize the null id
        if (virt == _nullId) {
          return virt;
        }
        // FIXME: Use more fine-grained locking (RW lock; C++17 for shared_lock)
        lock_t lock(_mutex);
        return _vIdTable.virtualToReal(virt);
      }

      T realToVirtual(T real)
      {
        // Don't need to virtualize the null id
        if (real == _nullId) {
          return real;
        }
        // FIXME: Use more fine-grained locking (RW lock; C++17 for shared_lock)
        lock_t lock(_mutex);
        return _vIdTable.realToVirtual(real);
      }

      // Adds the given real id to the virtual id table and creates a new
      // corresponding virtual id.
      // Returns the new virtual id on success, null id otherwise
      T onCreate(T real)
      {
        T vId = _nullId;
        // Don't need to virtualize the null id
        if (real == _nullId) {
          return vId;
        }
        lock_t lock(_mutex);
        if (_vIdTable.realIdExists(real)) {
          // JWARNING(false)(real)(_vIdTable.getTypeStr())
          //         (_vIdTable.realToVirtual(real))
          //         .Text("Real id exists. Will overwrite existing mapping");
          vId = _vIdTable.realToVirtual(real);
        }
        if (!_vIdTable.getNewVirtualId(&vId)) {
          JWARNING(false)(real)(_vIdTable.getTypeStr())
                  .Text("Failed to create a new vId");
        } else {
          _vIdTable.updateMapping(vId, real);
        }
        return vId;
      }

      // Removes virtual id from table and returns the real id corresponding
      // to the virtual id; if the virtual id does not exist in the table,
      // returns null id.
      T onRemove(T virt)
      {
        T realId = _nullId;
        // Don't need to virtualize the null id
        if (virt == _nullId) {
          return realId;
        }
        lock_t lock(_mutex);
        if (_vIdTable.virtualIdExists(virt)) {
          realId = _vIdTable.virtualToReal(virt);
          _vIdTable.erase(virt);
        } else {
          JWARNING(false)(virt)(_vIdTable.getTypeStr())
                  .Text("Cannot delete non-existent virtual id");
        }
        return realId;
      }

      // Updates the mapping for the given virtual id to the given real id.
      // Returns virtual id on success, null-id otherwise
      T updateMapping(T virt, T real)
      {
        // If the virt is the null id, then return it directly.
        // Don't need to virtualize the null id
        if (virt == _nullId) {
          return _nullId;
        }
        lock_t lock(_mutex);
        if (!_vIdTable.virtualIdExists(virt)) {
          JWARNING(false)(virt)(real)(_vIdTable.getTypeStr())
                  (_vIdTable.realToVirtual(real))
                  .Text("Cannot update mapping for a non-existent virt. id");
          return _nullId;
        }
        _vIdTable.updateMapping(virt, real);
        return virt;
      }

    private:
      // Pvt. constructor
      MpiVirtualization(const char *name, T nullId)
        : _vIdTable(name, (T)0, (T)999999),
          _mutex(),
          _nullId(nullId)
      {
      }

      // Virtual Ids Table
      dmtcp::VirtualIdTable<T> _vIdTable;
      // Lock on list
      mutex_t _mutex;
      // Default "NULL" value for id
      T _nullId;
  }; // class MpiId

  // FIXME: The new name should be: GlobalIdOfSimiliarComm
  class VirtualGlobalCommId {
    public:
      unsigned int createGlobalId(MPI_Comm comm) {
        if (comm == MPI_COMM_NULL) {
          return comm;
        }
        unsigned int gid = 0;
        int worldRank, commSize;
        int realComm = VIRTUAL_TO_REAL_COMM(comm);
        MPI_Comm_rank(MPI_COMM_WORLD, &worldRank);
        MPI_Comm_size(comm, &commSize);
        int rbuf[commSize];
        // FIXME: Use MPI_Group_translate_ranks instead of Allgather.
        // MPI_Group_translate_ranks only execute localy, so we can avoid
        // the cost of collective communication
        // FIXME: cray cc complains "catastrophic error" that can't find
        // split-process.h
#if 1
        DMTCP_PLUGIN_DISABLE_CKPT();
        JUMP_TO_LOWER_HALF(lh_info.fsaddr);
        NEXT_FUNC(Allgather)(&worldRank, 1, MPI_INT,
                             rbuf, 1, MPI_INT, realComm);
        RETURN_TO_UPPER_HALF();
        DMTCP_PLUGIN_ENABLE_CKPT();
#else
        MPI_Allgather(&worldRank, 1, MPI_INT, rbuf, 1, MPI_INT, comm);
#endif
        for (int i = 0; i < commSize; i++) {
          gid ^= hash(rbuf[i]);
        }
        // FIXME: We assume the hash collision between communicators who
        // have different members is low.
        // FIXME: We want to prune virtual communicators to avoid long
        // restart time.
        // FIXME: In VASP we observed that for the same virtual communicator
        // (adding 1 to each new communicator with the same rank members),
        // the virtual group can change over time, using:
        // virtual Comm -> real Comm -> real Group -> virtual Group
        // We don't understand why since vasp does not seem to free groups.
#if 0
        // FIXME: Some code can create new communicators during execution,
        // and so hash conflict may occur later.
        // if the new gid already exists in the map, add one and test again
        while (1) {
          bool found = false;
          for (std::pair<MPI_Comm, unsigned int> idPair : globalIdTable) {
            if (idPair.second == gid) {
              found = true;
              break;
            }
          }
          if (found) {
            gid++;
          } else {
            break;
          }
        }
#endif
        globalIdTable[comm] = gid;
        return gid;
      }

      unsigned int getGlobalId(MPI_Comm comm) {
        std::map<MPI_Comm, unsigned int>::iterator it =
          globalIdTable.find(comm);
        JASSERT(it != globalIdTable.end())(comm)
          .Text("Can't find communicator in the global id table");
        return it->second;
      }

      static VirtualGlobalCommId& instance() {
        static VirtualGlobalCommId _vGlobalId;
        return _vGlobalId;
      }

    private:
      VirtualGlobalCommId()
      {
          globalIdTable[MPI_COMM_NULL] = MPI_COMM_NULL;
          globalIdTable[MPI_COMM_WORLD] = MPI_COMM_WORLD;
      }

      void printMap(bool flag = false) {
        for (std::pair<MPI_Comm, int> idPair : globalIdTable) {
          if (flag) {
            printf("virtual comm: %x, real comm: %x, global id: %x\n",
                   idPair.first, VIRTUAL_TO_REAL_COMM(idPair.first),
                   idPair.second);
            fflush(stdout);
          } else {
            JTRACE("Print global id mapping")((void*) (uint64_t) idPair.first)
                      ((void*) (uint64_t) VIRTUAL_TO_REAL_COMM(idPair.first))
                      ((void*) (uint64_t) idPair.second);
          }
        }
      }
      // from https://stackoverflow.com/questions/664014/
      // what-integer-hash-function-are-good-that-accepts-an-integer-hash-key
      int hash(int i) {
        return i * 2654435761 % ((unsigned long)1 << 32);
      }
      std::map<MPI_Comm, unsigned int> globalIdTable;
  };
};  // namespace dmtcp_mpi

#endif // ifndef MPI_VIRTUAL_IDS_H
