#ifndef MTCP_SPLIT_PROCESS_H
#define MTCP_SPLIT_PROCESS_H

#include <stdint.h>

#include "mtcp_sys.h"

// FIXME: Make it dynamic
#define getpagesize()  4096

#define ROUND_UP(addr) ((addr + getpagesize() - 1) & ~(getpagesize()-1))
#define ROUND_DOWN(addr) ((unsigned long)addr & ~(getpagesize()-1))

typedef void (*fnptr_t)();

// FIXME: Much of this is duplicated in ../lower-half/lower_half_api.h

typedef struct __MemRange
{
  void *start;
  void *end;
} MemRange_t;

typedef struct __MmapInfo
{
  void *addr;
  size_t len;
  int unmapped;
  int guard;
} MmapInfo_t;

// The transient proxy process introspects its memory layout and passes this
// information back to the main application process using this struct.
typedef struct LowerHalfInfo
{
  void *startText;
  void *endText;
  void *startData;
  void *endOfHeap;
  void *libc_start_main;
  void *main;
  void *libc_csu_init;
  void *libc_csu_fini;
  void *fsaddr;
  uint64_t lh_AT_PHNUM;
  uint64_t lh_AT_PHDR;
  void *g_appContext;
  void *lh_dlsym;
  void *getRankFptr;
  void *parentStackStart;
  void *updateEnvironFptr;
  void *getMmappedListFptr;
  void *resetMmappedListFptr;
  MemRange_t memRange;
} LowerHalfInfo_t;

extern LowerHalfInfo_t lh_info;

// Helper macro to be used whenever making a jump from the upper half to
// the lower half.
#define JUMP_TO_LOWER_HALF(lhFs)                                               \
  do {                                                                         \
  unsigned long upperHalfFs;                                                   \
  mtcp_inline_syscall(arch_prctl, 2, ARCH_GET_FS, &upperHalfFs);               \
  mtcp_inline_syscall(arch_prctl, 2, ARCH_SET_FS, lhFs);

// Helper macro to be used whenever making a returning from the lower half to
// the upper half.
#define RETURN_TO_UPPER_HALF()                                                 \
  mtcp_inline_syscall(arch_prctl, 2, ARCH_SET_FS, &upperHalfFs);               \
  } while (0)

#ifdef MAIN_AUXVEC_ARG
/* main gets passed a pointer to the auxiliary.  */
# define MAIN_AUXVEC_DECL , void *
# define MAIN_AUXVEC_PARAM , auxvec
#else
# define MAIN_AUXVEC_DECL
# define MAIN_AUXVEC_PARAM
#endif // ifdef MAIN_AUXVEC_ARG

extern int main(int argc, char *argv[], char *envp[]);
extern int __libc_csu_init (int argc, char **argv, char **envp);
extern void __libc_csu_fini (void);

typedef int (*mainFptr)(int argc, char *argv[], char *envp[]);
typedef void (*finiFptr) (void);

extern int __libc_start_main(int (*main)(int, char **, char **MAIN_AUXVEC_DECL),
                             int argc,
                             char **argv,
                             __typeof (main) init,
                             void (*fini) (void),
                             void (*rtld_fini) (void),
                             void *stack_end);

typedef int (*libcFptr_t) (int (*main) (int, char **, char ** MAIN_AUXVEC_DECL),
                           int ,
                           char **,
                           __typeof (main) ,
                           void (*fini) (void),
                           void (*rtld_fini) (void),
                           void *);

#define FOREACH_FNC(MACRO) \
  MACRO(Init) \
  MACRO(Finalize) \
  MACRO(Send) \
  MACRO(Recv) \
  MACRO(Type_size) \
  MACRO(Iprobe) \
  MACRO(Get_count) \
  MACRO(Isend) \
  MACRO(Irecv) \
  MACRO(Wait) \
  MACRO(Test) \
  MACRO(Bcast) \
  MACRO(Abort) \
  MACRO(Barrier) \
  MACRO(Reduce) \
  MACRO(Allreduce) \
  MACRO(Alltoall) \
  MACRO(Alltoallv) \
  MACRO(Comm_split) \
  MACRO(Accumulate) \
  MACRO(Add_error_class) \
  MACRO(Add_error_code) \
  MACRO(Add_error_string) \
  MACRO(Address) \
  MACRO(Allgather) \
  MACRO(Iallgather) \
  MACRO(Allgatherv) \
  MACRO(Iallgatherv) \
  MACRO(Alloc_mem) \
  MACRO(Iallreduce) \
  MACRO(Ialltoall) \
  MACRO(Ialltoallv) \
  MACRO(Alltoallw) \
  MACRO(Ialltoallw) \
  MACRO(Attr_delete) \
  MACRO(Attr_get) \
  MACRO(Attr_put) \
  MACRO(Ibarrier) \
  MACRO(Bsend) \
  MACRO(Ibcast) \
  MACRO(Bsend_init) \
  MACRO(Buffer_attach) \
  MACRO(Buffer_detach) \
  MACRO(Cancel) \
  MACRO(Cart_coords) \
  MACRO(Cart_create) \
  MACRO(Cart_get) \
  MACRO(Cart_map) \
  MACRO(Cart_rank) \
  MACRO(Cart_shift) \
  MACRO(Cart_sub) \
  MACRO(Cartdim_get) \
  MACRO(Close_port) \
  MACRO(Comm_accept) \
  MACRO(Comm_call_errhandler) \
  MACRO(Comm_compare) \
  MACRO(Comm_connect) \
  MACRO(Comm_create_errhandler) \
  MACRO(Comm_create_keyval) \
  MACRO(Comm_create_group) \
  MACRO(Comm_create) \
  MACRO(Comm_delete_attr) \
  MACRO(Comm_disconnect) \
  MACRO(Comm_dup) \
  MACRO(Comm_idup) \
  MACRO(Comm_dup_with_info) \
  MACRO(Comm_free_keyval) \
  MACRO(Comm_free) \
  MACRO(Comm_get_attr) \
  MACRO(Dist_graph_create) \
  MACRO(Dist_graph_create_adjacent) \
  MACRO(Dist_graph_neighbors) \
  MACRO(Dist_graph_neighbors_count) \
  MACRO(Comm_get_errhandler) \
  MACRO(Comm_get_info) \
  MACRO(Comm_get_name) \
  MACRO(Comm_get_parent) \
  MACRO(Comm_group) \
  MACRO(Comm_join) \
  MACRO(Comm_rank) \
  MACRO(Comm_remote_group) \
  MACRO(Comm_remote_size) \
  MACRO(Comm_set_attr) \
  MACRO(Comm_set_errhandler) \
  MACRO(Comm_set_info) \
  MACRO(Comm_set_name) \
  MACRO(Comm_size) \
  MACRO(Comm_spawn) \
  MACRO(Comm_spawn_multiple) \
  MACRO(Comm_split_type) \
  MACRO(Comm_test_inter) \
  MACRO(Compare_and_swap) \
  MACRO(Dims_create) \
  MACRO(Errhandler_create) \
  MACRO(Errhandler_free) \
  MACRO(Errhandler_get) \
  MACRO(Errhandler_set) \
  MACRO(Error_class) \
  MACRO(Error_string) \
  MACRO(Exscan) \
  MACRO(Fetch_and_op) \
  MACRO(Iexscan) \
  MACRO(File_call_errhandler) \
  MACRO(File_create_errhandler) \
  MACRO(File_set_errhandler) \
  MACRO(File_get_errhandler) \
  MACRO(File_open) \
  MACRO(File_close) \
  MACRO(File_delete) \
  MACRO(File_set_size) \
  MACRO(File_preallocate) \
  MACRO(File_get_size) \
  MACRO(File_get_group) \
  MACRO(File_get_amode) \
  MACRO(File_set_info) \
  MACRO(File_get_info) \
  MACRO(File_set_view) \
  MACRO(File_get_view) \
  MACRO(File_read_at) \
  MACRO(File_read_at_all) \
  MACRO(File_write_at) \
  MACRO(File_write_at_all) \
  MACRO(File_iread_at) \
  MACRO(File_iwrite_at) \
  MACRO(File_iread_at_all) \
  MACRO(File_iwrite_at_all) \
  MACRO(File_read) \
  MACRO(File_read_all) \
  MACRO(File_write) \
  MACRO(File_write_all) \
  MACRO(File_iread) \
  MACRO(File_iwrite) \
  MACRO(File_iread_all) \
  MACRO(File_iwrite_all) \
  MACRO(File_seek) \
  MACRO(File_get_position) \
  MACRO(File_get_byte_offset) \
  MACRO(File_read_shared) \
  MACRO(File_write_shared) \
  MACRO(File_iread_shared) \
  MACRO(File_iwrite_shared) \
  MACRO(File_read_ordered) \
  MACRO(File_write_ordered) \
  MACRO(File_seek_shared) \
  MACRO(File_get_position_shared) \
  MACRO(File_read_at_all_begin) \
  MACRO(File_read_at_all_end) \
  MACRO(File_write_at_all_begin) \
  MACRO(File_write_at_all_end) \
  MACRO(File_read_all_begin) \
  MACRO(File_read_all_end) \
  MACRO(File_write_all_begin) \
  MACRO(File_write_all_end) \
  MACRO(File_read_ordered_begin) \
  MACRO(File_read_ordered_end) \
  MACRO(File_write_ordered_begin) \
  MACRO(File_write_ordered_end) \
  MACRO(File_get_type_extent) \
  MACRO(File_set_atomicity) \
  MACRO(File_get_atomicity) \
  MACRO(File_sync) \
  MACRO(Finalized) \
  MACRO(Free_mem) \
  MACRO(Gather) \
  MACRO(Igather) \
  MACRO(Gatherv) \
  MACRO(Igatherv) \
  MACRO(Get_address) \
  MACRO(Get_elements) \
  MACRO(Get_elements_x) \
  MACRO(Get) \
  MACRO(Get_accumulate) \
  MACRO(Get_library_version) \
  MACRO(Get_processor_name) \
  MACRO(Get_version) \
  MACRO(Graph_create) \
  MACRO(Graph_get) \
  MACRO(Graph_map) \
  MACRO(Graph_neighbors_count) \
  MACRO(Graph_neighbors) \
  MACRO(Graphdims_get) \
  MACRO(Grequest_complete) \
  MACRO(Group_compare) \
  MACRO(Group_difference) \
  MACRO(Group_excl) \
  MACRO(Group_free) \
  MACRO(Group_incl) \
  MACRO(Group_intersection) \
  MACRO(Group_range_excl) \
  MACRO(Group_range_incl) \
  MACRO(Group_rank) \
  MACRO(Group_size) \
  MACRO(Group_translate_ranks) \
  MACRO(Group_union) \
  MACRO(Ibsend) \
  MACRO(Improbe) \
  MACRO(Imrecv) \
  MACRO(Info_create) \
  MACRO(Info_delete) \
  MACRO(Info_dup) \
  MACRO(Info_free) \
  MACRO(Info_get) \
  MACRO(Info_get_nkeys) \
  MACRO(Info_get_nthkey) \
  MACRO(Info_get_valuelen) \
  MACRO(Info_set) \
  MACRO(Initialized) \
  MACRO(Init_thread) \
  MACRO(Intercomm_create) \
  MACRO(Intercomm_merge) \
  MACRO(Irsend) \
  MACRO(Issend) \
  MACRO(Is_thread_main) \
  MACRO(Keyval_create) \
  MACRO(Keyval_free) \
  MACRO(Lookup_name) \
  MACRO(Mprobe) \
  MACRO(Mrecv) \
  MACRO(Neighbor_allgather) \
  MACRO(Ineighbor_allgather) \
  MACRO(Neighbor_allgatherv) \
  MACRO(Ineighbor_allgatherv) \
  MACRO(Neighbor_alltoall) \
  MACRO(Ineighbor_alltoall) \
  MACRO(Neighbor_alltoallv) \
  MACRO(Ineighbor_alltoallv) \
  MACRO(Neighbor_alltoallw) \
  MACRO(Ineighbor_alltoallw) \
  MACRO(Op_commutative) \
  MACRO(Op_create) \
  MACRO(Open_port) \
  MACRO(Op_free) \
  MACRO(Pack_external) \
  MACRO(Pack_external_size) \
  MACRO(Pack) \
  MACRO(Pack_size) \
  MACRO(Pcontrol) \
  MACRO(Probe) \
  MACRO(Publish_name) \
  MACRO(Put) \
  MACRO(Query_thread) \
  MACRO(Raccumulate) \
  MACRO(Recv_init) \
  MACRO(Ireduce) \
  MACRO(Reduce_local) \
  MACRO(Reduce_scatter) \
  MACRO(Ireduce_scatter) \
  MACRO(Reduce_scatter_block) \
  MACRO(Ireduce_scatter_block) \
  MACRO(Register_datarep) \
  MACRO(Request_free) \
  MACRO(Request_get_status) \
  MACRO(Rget) \
  MACRO(Rget_accumulate) \
  MACRO(Rput) \
  MACRO(Rsend) \
  MACRO(Rsend_init) \
  MACRO(Scan) \
  MACRO(Iscan) \
  MACRO(Scatter) \
  MACRO(Iscatter) \
  MACRO(Scatterv) \
  MACRO(Iscatterv) \
  MACRO(Send_init) \
  MACRO(Sendrecv) \
  MACRO(Sendrecv_replace) \
  MACRO(Ssend_init) \
  MACRO(Ssend) \
  MACRO(Start) \
  MACRO(Startall) \
  MACRO(Status_set_cancelled) \
  MACRO(Status_set_elements) \
  MACRO(Status_set_elements_x) \
  MACRO(Testall) \
  MACRO(Testany) \
  MACRO(Test_cancelled) \
  MACRO(Testsome) \
  MACRO(Topo_test) \
  MACRO(Type_commit) \
  MACRO(Type_contiguous) \
  MACRO(Type_create_darray) \
  MACRO(Type_create_f90_complex) \
  MACRO(Type_create_f90_integer) \
  MACRO(Type_create_f90_real) \
  MACRO(Type_create_hindexed_block) \
  MACRO(Type_create_hindexed) \
  MACRO(Type_create_hvector) \
  MACRO(Type_create_keyval) \
  MACRO(Type_create_indexed_block) \
  MACRO(Type_create_struct) \
  MACRO(Type_create_subarray) \
  MACRO(Type_create_resized) \
  MACRO(Type_delete_attr) \
  MACRO(Type_dup) \
  MACRO(Type_extent) \
  MACRO(Type_free) \
  MACRO(Type_free_keyval) \
  MACRO(Type_get_attr) \
  MACRO(Type_get_contents) \
  MACRO(Type_get_envelope) \
  MACRO(Type_get_extent) \
  MACRO(Type_get_extent_x) \
  MACRO(Type_get_name) \
  MACRO(Type_get_true_extent) \
  MACRO(Type_get_true_extent_x) \
  MACRO(Type_hindexed) \
  MACRO(Type_hvector) \
  MACRO(Type_indexed) \
  MACRO(Type_lb) \
  MACRO(Type_match_size) \
  MACRO(Type_set_attr) \
  MACRO(Type_set_name) \
  MACRO(Type_size_x) \
  MACRO(Type_struct) \
  MACRO(Type_ub) \
  MACRO(Type_vector) \
  MACRO(Unpack) \
  MACRO(Unpublish_name) \
  MACRO(Unpack_external ) \
  MACRO(Waitall) \
  MACRO(Waitany) \
  MACRO(Waitsome) \
  MACRO(Win_allocate) \
  MACRO(Win_allocate_shared) \
  MACRO(Win_attach) \
  MACRO(Win_call_errhandler) \
  MACRO(Win_complete) \
  MACRO(Win_create) \
  MACRO(Win_create_dynamic) \
  MACRO(Win_create_errhandler) \
  MACRO(Win_create_keyval) \
  MACRO(Win_delete_attr) \
  MACRO(Win_detach) \
  MACRO(Win_fence) \
  MACRO(Win_flush) \
  MACRO(Win_flush_all) \
  MACRO(Win_flush_local) \
  MACRO(Win_flush_local_all) \
  MACRO(Win_free) \
  MACRO(Win_free_keyval) \
  MACRO(Win_get_attr) \
  MACRO(Win_get_errhandler) \
  MACRO(Win_get_group) \
  MACRO(Win_get_info) \
  MACRO(Win_get_name) \
  MACRO(Win_lock) \
  MACRO(Win_lock_all) \
  MACRO(Win_post) \
  MACRO(Win_set_attr) \
  MACRO(Win_set_errhandler) \
  MACRO(Win_set_info) \
  MACRO(Win_set_name) \
  MACRO(Win_shared_query) \
  MACRO(Win_start) \
  MACRO(Win_sync) \
  MACRO(Win_test) \
  MACRO(Win_unlock) \
  MACRO(Win_unlock_all) \
  MACRO(Win_wait) \
  MACRO(Wtick) \
  MACRO(Wtime)

#define GENERATE_ENUM(ENUM) MPI_Fnc_##ENUM,
#define GENERATE_FNC_PTR(FNC) &MPI_##FNC,

enum MPI_Fncs {
  MPI_Fnc_NULL,
  FOREACH_FNC(GENERATE_ENUM)
  MPI_Fnc_Invalid,
};

typedef void* (*proxyDlsym_t)(enum MPI_Fncs fnc);
typedef void* (*updateEnviron_t)(char **envp);
typedef void (*resetMmappedList_t)();
typedef MmapInfo_t* (*getMmappedList_t)(int *num);

extern int splitProcess(char *argv0, char **envp);
int getMappedArea(Area *area, char *name);

#endif // ifndef MTCP_SPLIT_PROCESS_H
