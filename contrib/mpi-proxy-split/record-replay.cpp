#include <mpi.h>

#include "jassert.h"
#include "jconvert.h"

#include "record-replay.h"
#include "virtual-ids.h"
#include "p2p_log_replay.h"

using namespace dmtcp_mpi;

static int restoreCommSplit(const MpiRecord& rec);
static int restoreCommSplitType(const MpiRecord& rec);
static int restoreCommDup(const MpiRecord& rec);
static int restoreCommCreate(const MpiRecord& rec);
static int restoreCommCreateGroup(const MpiRecord& rec);
static int restoreCommErrHandler(const MpiRecord& rec);
static int restoreCommFree(const MpiRecord& rec);
static int restoreAttrPut(const MpiRecord& rec);
static int restoreAttrDelete(const MpiRecord& rec);
static int restoreCommCreateKeyval(const MpiRecord& rec);
static int restoreCommFreeKeyval(const MpiRecord& rec);

static int restoreCommGroup(const MpiRecord& rec);
static int restoreGroupFree(const MpiRecord& rec);
static int restoreGroupIncl(const MpiRecord& rec);

static int restoreTypeContiguous(const MpiRecord& rec);
static int restoreTypeCommit(const MpiRecord& rec);
static int restoreTypeVector(const MpiRecord& rec);
static int restoreTypeIndexed(const MpiRecord& rec);
static int restoreTypeFree(const MpiRecord& rec);
static int restoreTypeCreateStruct(const MpiRecord& rec);

static int restoreCartCreate(const MpiRecord& rec);
static int restoreCartMap(const MpiRecord& rec);
static int restoreCartShift(const MpiRecord& rec);
static int restoreCartSub(const MpiRecord& rec);

static int restoreOpCreate(const MpiRecord& rec);
static int restoreOpFree(const MpiRecord& rec);

static int restoreIbcast(const MpiRecord& rec);
static int restoreIreduce(const MpiRecord& rec);
static int restoreIbarrier(const MpiRecord& rec);

void
restoreMpiLogState()
{
  JASSERT(RESTORE_MPI_STATE() == MPI_SUCCESS)
          .Text("Failed to restore MPI state");
}

int
dmtcp_mpi::restoreComms(const MpiRecord &rec)
{
  int rc = -1;
  JTRACE("Restoring MPI communicators");
  switch (rec.getType()) {
    case GENERATE_ENUM(Comm_split):
      JTRACE("restoreCommSplit");
      rc = restoreCommSplit(rec);
      break;
    case GENERATE_ENUM(Comm_split_type):
      JTRACE("restoreCommSplitType");
      rc = restoreCommSplitType(rec);
      break;
    case GENERATE_ENUM(Comm_dup):
      JTRACE("restoreCommDup");
      rc = restoreCommDup(rec);
      break;
    case GENERATE_ENUM(Comm_create):
      JTRACE("restoreCommCreate");
      rc = restoreCommCreate(rec);
      break;
    case GENERATE_ENUM(Comm_create_group):
      JTRACE("restoreCommCreateGroup");
      rc = restoreCommCreateGroup(rec);
      break;
    case GENERATE_ENUM(Comm_set_errhandler):
      JTRACE("restoreCommErrHandler");
      rc = restoreCommErrHandler(rec);
      break;
    case GENERATE_ENUM(Comm_free):
      JTRACE("restoreCommFree");
      rc = restoreCommFree(rec);
      break;
    case GENERATE_ENUM(Attr_put):
      JTRACE("restoreAtrrPut");
      rc = restoreAttrPut(rec);
      break;
    case GENERATE_ENUM(Attr_delete):
      JTRACE("restoreAtrrDelete");
      rc = restoreAttrDelete(rec);
      break;
    case GENERATE_ENUM(Comm_create_keyval):
      JTRACE("restoreCommCreateKeyval");
      rc = restoreCommCreateKeyval(rec);
      break;
    case GENERATE_ENUM(Comm_free_keyval):
      JTRACE("restoreCommFreeKeyval");
      rc = restoreCommFreeKeyval(rec);
      break;
    default:
      JWARNING(false)(rec.getType()).Text("Unknown call");
      break;
  }
  return rc;
}

int
dmtcp_mpi::restoreGroups(const MpiRecord &rec)
{
  int rc = -1;
  JTRACE("Restoring MPI groups");
  switch (rec.getType()) {
    case GENERATE_ENUM(Comm_group):
      JTRACE("restoreCommGroup");
      rc = restoreCommGroup(rec);
      break;
    case GENERATE_ENUM(Group_free):
      JTRACE("restoreGroupFree");
      rc = restoreGroupFree(rec);
      break;
    case GENERATE_ENUM(Group_incl):
      JTRACE("restoreGroupIncl");
      rc = restoreGroupIncl(rec);
      break;
    default:
      JWARNING(false)(rec.getType()).Text("Unknown call");
      break;
  }
  return rc;
}

int
dmtcp_mpi::restoreTypes(const MpiRecord &rec)
{
  int rc = -1;
  JTRACE("Restoring MPI derived types");
  switch (rec.getType()) {
    case GENERATE_ENUM(Type_contiguous):
      JTRACE("restoreTypeContiguous");
      rc = restoreTypeContiguous(rec);
      break;
    case GENERATE_ENUM(Type_commit):
      JTRACE("restoreTypeCommit");
      rc = restoreTypeCommit(rec);
      break;
    case GENERATE_ENUM(Type_vector):
      JTRACE("restoreTypeVector");
      rc = restoreTypeVector(rec);
      break;
    case GENERATE_ENUM(Type_indexed):
      JTRACE("restoreTypeIndexed");
      rc = restoreTypeIndexed(rec);
      break;
    case GENERATE_ENUM(Type_free):
      JTRACE("restoreTypeFree");
      rc = restoreTypeFree(rec);
      break;
    case GENERATE_ENUM(Type_create_struct):
      JTRACE("restoreTypeCreateStruct");
      rc = restoreTypeCreateStruct(rec);
      break;
    default:
      JWARNING(false)(rec.getType()).Text("Unknown call");
      break;
  }
  return rc;
}

int
dmtcp_mpi::restoreCarts(const MpiRecord &rec)
{
  int rc = -1;
  JTRACE("Restoring MPI cartesian");
  switch (rec.getType()) {
    case GENERATE_ENUM(Cart_create):
      JTRACE("restoreCartCreate");
      rc = restoreCartCreate(rec);
      break;
    case GENERATE_ENUM(Cart_map):
      JTRACE("restoreCartMap");
      rc = restoreCartMap(rec);
      break;
    case GENERATE_ENUM(Cart_shift):
      JTRACE("restoreCartShift");
      rc = restoreCartShift(rec);
      break;
    case GENERATE_ENUM(Cart_sub):
      JTRACE("restoreCartSub");
      rc = restoreCartSub(rec);
      break;
    default:
      JWARNING(false)(rec.getType()).Text("Unknown call");
      break;
  }
  return rc;
}

int
dmtcp_mpi::restoreOps(const MpiRecord &rec)
{
  int rc = -1;
  JTRACE("Restoring MPI Ops");
  switch (rec.getType()) {
    case GENERATE_ENUM(Op_create):
      JTRACE("restoreOpCreate");
      rc = restoreOpCreate(rec);
      break;
    case GENERATE_ENUM(Op_free):
      JTRACE("restoreOpFree");
      rc = restoreOpFree(rec);
      break;
    default:
      JWARNING(false)(rec.getType()).Text("Unknown call");
      break;
  }
  return rc;
}

int
dmtcp_mpi::restoreRequests(const MpiRecord &rec)
{
  int rc = -1;
  JTRACE("Restoring MPI Requests");
  switch (rec.getType()) {
    case GENERATE_ENUM(Ibarrier):
      JTRACE("restoreIbarrier");
      rc = restoreIbarrier(rec);
      break;
    case GENERATE_ENUM(Ireduce):
      JTRACE("restoreIreduce");
      rc = restoreIreduce(rec);
      break;
    case GENERATE_ENUM(Ibcast):
      JTRACE("restoreIbcast");
      rc = restoreIbcast(rec);
      break;
    default:
      JWARNING(false)(rec.getType()).Text("Unknown call");
      break;
  }
  return rc;
}

static int
restoreCommSplit(const MpiRecord& rec)
{
  int retval;
  MPI_Comm comm = rec.args(0);
  int color = rec.args(1);
  int key = rec.args(2);
  MPI_Comm newcomm = MPI_COMM_NULL;
  retval = FNC_CALL(Comm_split, rec)(comm, color, key, &newcomm);
  if (retval == MPI_SUCCESS) {
    MPI_Comm virtComm = rec.args(3);
    UPDATE_COMM_MAP(virtComm, newcomm);
  }
  return retval;
}

static int
restoreCommSplitType(const MpiRecord& rec)
{
  int retval;
  MPI_Comm comm = rec.args(0);
  int split_type = rec.args(1);
  int key = rec.args(2);
  MPI_Info inf = rec.args(3);
  MPI_Comm newcomm = MPI_COMM_NULL;
  retval = FNC_CALL(Comm_split_type, rec)(comm, split_type, key, inf, &newcomm);
  if (retval == MPI_SUCCESS) {
    MPI_Comm virtComm = rec.args(4);
    UPDATE_COMM_MAP(virtComm, newcomm);
  }
  return retval;
}

static int
restoreCommDup(const MpiRecord& rec)
{
  int retval;
  MPI_Comm comm = rec.args(0);
  MPI_Comm newcomm = MPI_COMM_NULL;
  retval = FNC_CALL(Comm_dup, rec)(comm, &newcomm);
  if (retval == MPI_SUCCESS) {
    MPI_Comm virtComm = rec.args(1);
    UPDATE_COMM_MAP(virtComm, newcomm);
  }
  return retval;
}

static int
restoreCommCreate(const MpiRecord& rec)
{
  int retval;
  MPI_Comm comm = rec.args(0);
  MPI_Group group = rec.args(1);
  MPI_Comm newcomm = MPI_COMM_NULL;
  retval = FNC_CALL(Comm_create, rec)(comm, group, &newcomm);
  if (retval == MPI_SUCCESS) {
    MPI_Comm oldcomm = rec.args(2);
    UPDATE_COMM_MAP(oldcomm, newcomm);
  }
  return retval;
}

static int
restoreCommCreateGroup(const MpiRecord& rec)
{
  int retval;
  MPI_Comm comm = rec.args(0);
  MPI_Group group = rec.args(1);
  int tag = rec.args(2);
  MPI_Comm newcomm = MPI_COMM_NULL;
  retval = FNC_CALL(Comm_create_group, rec)(comm, group, tag, &newcomm);
  if (retval == MPI_SUCCESS) {
    MPI_Comm oldcomm = rec.args(3);
    UPDATE_COMM_MAP(oldcomm, newcomm);
  }
  return retval;
}

static int
restoreCommErrHandler(const MpiRecord& rec)
{
  int retval;
  MPI_Comm comm = rec.args(0);
  MPI_Errhandler errhandler = rec.args(1);
  retval = FNC_CALL(Comm_set_errhandler, rec)(comm, errhandler);
  JWARNING(retval == MPI_SUCCESS)(comm).Text("Error restoring MPI errhandler");
  return retval;
}

static int
restoreCommFree(const MpiRecord& rec)
{
  int retval;
  MPI_Comm comm = rec.args(0);
  retval = FNC_CALL(Comm_free, rec)(&comm);
  JWARNING(retval == MPI_SUCCESS)(comm).Text("Error freeing MPI comm");
  if (retval == MPI_SUCCESS) {
    // See mpi_comm_wrappers.cpp:Comm_free
    // NOTE: We cannot remove the old comm from the map, since
    // we'll need to replay this call to reconstruct any other comms that
    // might have been created using this comm.
    //
    // MPI_Comm oldcomm = REMOVE_OLD_COMM(comm);
  }
  return retval;
}

static int
restoreAttrPut(const MpiRecord& rec)
{
  int retval;
  MPI_Comm comm = rec.args(0);
  int key = rec.args(1);
  void *val = rec.args(2);
  retval = FNC_CALL(Attr_put, rec)(comm, key, val);
  JWARNING(retval == MPI_SUCCESS)(comm)
          .Text("Error restoring MPI attribute-put");
  return retval;
}

static int
restoreAttrDelete(const MpiRecord& rec)
{
  int retval;
  MPI_Comm comm = rec.args(0);
  int key = rec.args(1);
  retval = FNC_CALL(Attr_delete, rec)(comm, key);
  JWARNING(retval == MPI_SUCCESS)(comm).Text("Error deleting MPI attribute");
  return retval;
}

static int
restoreCommCreateKeyval(const MpiRecord& rec)
{
  int retval;
  void *cfn_tmp = rec.args(0);
  void *dfn_tmp = rec.args(1);
  MPI_Comm_copy_attr_function *cfn = (MPI_Comm_copy_attr_function*) cfn_tmp;
  MPI_Comm_delete_attr_function *dfn = (MPI_Comm_delete_attr_function*) dfn_tmp;
  int newkey = 0;
  void *extra_state = rec.args(3);
  retval = FNC_CALL(Comm_create_keyval, rec)(cfn, dfn, &newkey, extra_state);
  if (retval == MPI_SUCCESS) {
    int oldkey = rec.args(2);
    UPDATE_COMM_KEYVAL_MAP(oldkey, newkey);
  }
  return retval;
}

static int
restoreCommFreeKeyval(const MpiRecord& rec)
{
  int retval;
  int key = rec.args(0);
  retval = FNC_CALL(Comm_free_keyval, rec)(&key);
  JWARNING(retval == MPI_SUCCESS)(key).Text("Error deleting MPI Comm Keyval");
  // See mpi_comm_wrappers.cpp:Comm_free_keyval
  // We don't remove item from virtual-id tables
  return retval;
}

static int
restoreCommGroup(const MpiRecord& rec)
{
  int retval;
  MPI_Comm comm = rec.args(0);
  MPI_Group newgroup = MPI_GROUP_NULL;
  retval = FNC_CALL(Comm_group, rec)(comm, &newgroup);
  JWARNING(retval == MPI_SUCCESS)(comm).Text("Error restoring MPI comm group");
  if (retval == MPI_SUCCESS) {
    MPI_Group oldgroup = rec.args(1);
    UPDATE_GROUP_MAP(oldgroup, newgroup);
  }
  return retval;
}

static int
restoreGroupFree(const MpiRecord& rec)
{
  int retval;
  MPI_Group group = rec.args(0);
  retval = FNC_CALL(Group_free, rec)(&group);
  JWARNING(retval == MPI_SUCCESS)(group).Text("Error restoring MPI group free");
  if (retval == MPI_SUCCESS) {
    // See mpi_group_wrappers.cpp:Group_free
    // NOTE: We cannot remove the old group, since we'll need
    // to replay this call to reconstruct any comms that might
    // have been created using this group.
    //
    // REMOVE_OLD_GROUP(group);
  }
  return retval;
}


static int
restoreGroupIncl(const MpiRecord& rec)
{
  int retval;
  MPI_Group group = rec.args(0);
  int n = rec.args(1);
  int *ranks = rec.args(2);
  MPI_Group newgroup = MPI_GROUP_NULL;
  retval = FNC_CALL(Group_incl, rec)(group, n, ranks, &newgroup);
  JWARNING(retval == MPI_SUCCESS)(group).Text("Error restoring MPI group incl");
  if (retval == MPI_SUCCESS) {
    MPI_Group oldgroup = rec.args(3);
    UPDATE_GROUP_MAP(oldgroup, newgroup);
  }
  return retval;
}

static int
restoreTypeContiguous(const MpiRecord& rec)
{
  int retval;
  int count = rec.args(0);
  MPI_Datatype oldtype = rec.args(1);
  MPI_Datatype newtype;
  retval = FNC_CALL(Type_contiguous, rec)(count, oldtype, &newtype);
  if (retval == MPI_SUCCESS) {
    MPI_Datatype virtType = rec.args(2);
    UPDATE_TYPE_MAP(virtType, newtype);
  }
  return retval;
}

static int
restoreTypeCommit(const MpiRecord& rec)
{
  int retval;
  MPI_Datatype type = rec.args(0);
  retval = FNC_CALL(Type_commit, rec)(&type);
  JWARNING(retval == MPI_SUCCESS)(type).Text("Could not commit MPI datatype");
  return retval;
}

static int
restoreTypeVector(const MpiRecord& rec)
{
  int retval;
  int count = rec.args(0);
  int blocklength = rec.args(1);
  int stride = rec.args(2);
  MPI_Datatype oldtype = rec.args(3);
  MPI_Datatype newtype = MPI_DATATYPE_NULL;
  retval = FNC_CALL(Type_vector, rec)(count, blocklength,
                                      stride, oldtype, &newtype);
  JWARNING(retval == MPI_SUCCESS)(oldtype)
          .Text("Could not restore MPI vector datatype");
  if (retval == MPI_SUCCESS) {
    MPI_Datatype virtType = rec.args(4);
    UPDATE_TYPE_MAP(virtType, newtype);
  }
  return retval;
}

void MpiRecordReplay::printRecords(bool print)
{
  JNOTE("Printing _records");
  for(MpiRecord* record : _records) {
    int fnc_idx = record->getType();
    if (print) {
      printf("%s\n", MPI_Fnc_strings[fnc_idx]);
    } else {
      JNOTE("") (MPI_Fnc_strings[fnc_idx]);
    }
  }
}

 static int
restoreTypeIndexed(const MpiRecord& rec)
{
  int retval;
  int count = rec.args(0);
  int *blocklengths = rec.args(1);
  int *displs = rec.args(2);
  MPI_Datatype oldtype = rec.args(3);
  MPI_Datatype newtype = MPI_DATATYPE_NULL;
  retval = FNC_CALL(Type_indexed, rec)(count, blocklengths,
                                       displs, oldtype, &newtype);
  JWARNING(retval == MPI_SUCCESS)(oldtype)
          .Text("Could not restore MPI indexed datatype");
  if (retval == MPI_SUCCESS) {
    MPI_Datatype virtType = rec.args(4);
    UPDATE_TYPE_MAP(virtType, newtype);
  }
  return retval;
}

static int
restoreTypeFree(const MpiRecord& rec)
{
  int retval;
  MPI_Datatype type = rec.args(0);
  retval = FNC_CALL(Type_free, rec)(&type);
  JWARNING(retval == MPI_SUCCESS)(type).Text("Could not free MPI datatype");
  if (retval == MPI_SUCCESS) {
    // See mpi_type_wrappers.cpp:Type_free
    // NOTE: We cannot remove the old type from the map, since
    // we'll need to replay this call to reconstruct any other type that
    // might have been created using this type.
    //
    // MPI_Datatype realType = REMOVE_OLD_TYPE(type);
  }
  return retval;
}

static int
restoreTypeCreateStruct(const MpiRecord& rec)
{
  int retval;
  int count = rec.args(0);
  int *blocklengths = rec.args(1);
  MPI_Aint *displs = rec.args(2);
  MPI_Datatype *types = rec.args(3);
  MPI_Datatype newtype = MPI_DATATYPE_NULL;
  retval = FNC_CALL(Type_create_struct, rec)(count, blocklengths,
                                       displs, types, &newtype);
  JWARNING(retval == MPI_SUCCESS)(types)
          .Text("Could not restore MPI struct datatype");
  if (retval == MPI_SUCCESS) {
    MPI_Datatype virtType = rec.args(4);
    UPDATE_TYPE_MAP(virtType, newtype);
  }
  return retval;
}

static int
restoreCartCreate(const MpiRecord& rec)
{
  int retval;
  MPI_Comm comm = rec.args(0);
  int ndims = rec.args(1);
  int *dims = rec.args(2);
  int *periods = rec.args(3);
  int reorder = rec.args(4);
  MPI_Comm newcomm = MPI_COMM_NULL;
  retval = FNC_CALL(Cart_create, rec)(comm, ndims, dims,
                                      periods, reorder, &newcomm);
  if (retval == MPI_SUCCESS) {
    MPI_Comm virtComm = rec.args(5);
    UPDATE_COMM_MAP(virtComm, newcomm);
  }
  return retval;
}

static int
restoreCartMap(const MpiRecord& rec)
{
  int retval;
  MPI_Comm comm = rec.args(0);
  int ndims = rec.args(1);
  int *dims = rec.args(2);
  int *periods = rec.args(3);
  int newrank = -1;
  retval = FNC_CALL(Cart_map, rec)(comm, ndims, dims, periods, &newrank);
  if (retval == MPI_SUCCESS) {
    // FIXME: Virtualize rank?
    int oldrank = rec.args(4);
    JASSERT(newrank == oldrank)(oldrank)(newrank).Text("Different ranks");
  }
  return retval;
}

static int
restoreCartShift(const MpiRecord& rec)
{
  int retval;
  MPI_Comm comm = rec.args(0);
  int direction = rec.args(1);
  int disp = rec.args(2);
  int rank_source = -1;
  int rank_dest = -1;
  retval = FNC_CALL(Cart_shift, rec)(comm, direction,
                                     disp, &rank_source, &rank_dest);
  if (retval == MPI_SUCCESS) {
    // FIXME: Virtualize rank?
    int oldsrc = rec.args(3);
    int olddest = rec.args(4);
    JASSERT(oldsrc == rank_source && olddest == rank_dest)
           (oldsrc)(olddest)(rank_source)(rank_dest).Text("Different ranks");
  }
  return retval;
}

static int
restoreCartSub(const MpiRecord& rec)
{
  int retval;
  MPI_Comm comm = rec.args(0);
  // int ndims = rec.args(1);
  int *remain_dims = rec.args(2);
  MPI_Comm newcomm = MPI_COMM_NULL;
  // LOG_CALL(restoreCarts, Cart_sub, comm, ndims, rs, virtComm);
  retval = FNC_CALL(Cart_sub, rec)(comm, remain_dims, &newcomm);
  if (retval == MPI_SUCCESS) {
    MPI_Comm virtComm = rec.args(3);
    UPDATE_COMM_MAP(virtComm, newcomm);
  }
  return retval;
}

static int
restoreOpCreate(const MpiRecord& rec)
{
  int retval = -1;
  MPI_User_function *user_fn = rec.args(0);
  int commute = rec.args(1);
  MPI_Op newop = MPI_OP_NULL;
  retval = FNC_CALL(Op_create, rec)(user_fn, commute, &newop);
  if (retval == MPI_SUCCESS) {
    MPI_Op oldop = rec.args(2);
    UPDATE_OP_MAP(oldop, newop);
  }
  return retval;
}

static int
restoreOpFree(const MpiRecord& rec)
{
  int retval = -1;
  MPI_Op op = rec.args(0);
  MPI_Op realOp = VIRTUAL_TO_REAL_OP(op);
  retval = FNC_CALL(Op_free, rec)(&realOp);
  if (retval == MPI_SUCCESS) {
    // See mpi_op_wrappers.cpp:Op_free
    // NOTE: We cannot remove the old op from the map, since
    // we'll need to replay this call to reconstruct any other op that
    // might have been created using this op.
    //
    // realOp = REMOVE_OLD_OP(op);
  }
  return retval;
}

static int restoreIbcast(const MpiRecord& rec) {
  int retval = -1;
  void *buf = rec.args(0);
  int count = rec.args(1);
  MPI_Datatype datatype = rec.args(2);
  int root = rec.args(3);
  MPI_Comm comm = rec.args(4);
  MPI_Request newRealRequest = MPI_REQUEST_NULL;
  retval = FNC_CALL(Ibcast, rec)(buf, count, datatype, root, comm,
                                 &newRealRequest);
  if (retval == MPI_SUCCESS) {
    MPI_Request virtRequest = rec.args(5);
    UPDATE_REQUEST_MAP(virtRequest, newRealRequest);
#ifdef USE_REQUEST_LOG
    logRequestInfo(virtRequest, IBCAST_REQUEST);
#endif
  }
  return retval;
}

static int restoreIreduce(const MpiRecord& rec) {
  int retval = -1;
  void *sendbuf = rec.args(0);
  void *recvbuf = rec.args(1);
  int count = rec.args(2);
  MPI_Datatype datatype = rec.args(3);
  MPI_Op op = rec.args(4);
  int root = rec.args(5);
  MPI_Comm comm = rec.args(6);
  MPI_Request newRealRequest = MPI_REQUEST_NULL;
  retval = FNC_CALL(Ireduce, rec)(sendbuf, recvbuf, count,
                                  datatype, op, root, comm, &newRealRequest);
  if (retval == MPI_SUCCESS) {
    MPI_Request virtRequest = rec.args(7);
    UPDATE_REQUEST_MAP(virtRequest, newRealRequest);
#ifdef USE_REQUEST_LOG
    logRequestInfo(virtRequest, IREDUCE_REQUSET);
#endif
  }
  return retval;
}

static int restoreIbarrier(const MpiRecord& rec) {
  int retval = -1;
  MPI_Comm comm = rec.args(0);
  MPI_Request newRealRequest = MPI_REQUEST_NULL;
  retval = FNC_CALL(Ibarrier, rec)(comm, &newRealRequest);
  MPI_Request virtRequest;
  if (retval == MPI_SUCCESS) {
    virtRequest = rec.args(1);
    UPDATE_REQUEST_MAP(virtRequest, newRealRequest);
#ifdef USE_REQUEST_LOG
    logRequestInfo(virtRequest, IBARRIER_REQUEST);
#endif
  }
  // Verify the request is valid
  int flag;
  retval = MPI_Request_get_status(virtRequest, &flag, MPI_STATUS_IGNORE);
  JASSERT(retval == MPI_SUCCESS);
  return retval;
}
