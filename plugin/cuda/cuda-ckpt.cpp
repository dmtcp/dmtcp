#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/eventfd.h>
#include <cuda.h>
#include <cuda_runtime_api.h>
#include <vector>
#include "config.h"
#include "constants.h"
#include "dmtcp.h"
#include "jassert.h"
#include "procselfmaps.h"
#include "procmapsarea.h"
#include "trampolines.h"
#include "plugin/ipc/event/eventwrappers.h"

// Maximum path length for /proc/self/fd/ entries and readlink buffer
#define PATH_MAX 4096 
// Max number of file descriptors we'll track (arbitrary limit)
#define MAX_PIPE_FDS 1024 

// Enum for pipe end type
typedef enum {
    PIPE_READ = O_RDONLY,
    PIPE_WRITE = O_WRONLY
} pipe_rw_t;

// Structure to hold pipe information
typedef struct {
    int fd;
    unsigned long pipe_inode;
    pipe_rw_t rw_type;
    int status_flags; // F_GETFL: access mode + O_NONBLOCK/O_DIRECT/...
    int fd_flags;     // F_GETFD: FD_CLOEXEC
} pipe_info_t;

// Structure to map old inode to new FDs (for conceptual recreation)
typedef struct {
    unsigned long old_inode;
    int new_read_fd;
    int new_write_fd;
    int created; // Flag to ensure we only create a new pipe pair once per inode
} pipe_map_t;

std::vector<ProcMapsArea> nvidia_device_files;
CUprocessState cuda_state = CU_PROCESS_STATE_RUNNING;
CUcheckpointRestoreArgs restore_args = {0};
int cuda_initialized = 0;
pipe_info_t pipe_list[MAX_PIPE_FDS] = {0};
int num_fds_found;

// The main inspection function
int inspect_pipes(pipe_info_t *pipe_fd_array, int max_pipe_fds) {
  // ... (content of inspect_pipes function from previous response) ...
  DIR *dir;
  struct dirent *entry;
  char fd_path[PATH_MAX];
  char link_target[PATH_MAX];
  int count = 0;
  assert((dir = opendir("/proc/self/fd")) != NULL);
  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_name[0] == '.') continue;
    if (strspn(entry->d_name, "0123456789") != strlen(entry->d_name)) continue;
    int fd = atoi(entry->d_name);
    if (fd == dirfd(dir)) continue;
    // fd 0 (stdin), 1 (stdout) or 2 (stderr) could be replacd by pipe,
    // but we don't want to save them.
    if (fd == 0 || fd == 1 || fd == 2) continue;
    snprintf(fd_path, PATH_MAX, "/proc/self/fd/%s", entry->d_name);
    ssize_t len = readlink(fd_path, link_target, PATH_MAX - 1);
    if (len == -1) continue; 
    link_target[len] = '\0'; 
    if (strncmp(link_target, "pipe:[", 6) == 0) {
      JASSERT(count < max_pipe_fds);
      unsigned long inode = strtoul(link_target + 6, NULL, 10);
      int flags = fcntl(fd, F_GETFL);
      JASSERT(flags != -1);
      pipe_rw_t rw_type = (pipe_rw_t)(flags & O_ACCMODE);
      JASSERT(rw_type == PIPE_READ || rw_type == PIPE_WRITE);
      // Capture both the file-status flags (F_GETFL: O_NONBLOCK, O_DIRECT, ...)
      // and the file-descriptor flags (F_GETFD: FD_CLOEXEC) so they can be
      // restored after the pipe is recreated with pipe()+dup2 on restart.
      int fd_flags = fcntl(fd, F_GETFD);
      JASSERT(fd_flags != -1);
      pipe_fd_array[count].fd = fd;
      pipe_fd_array[count].pipe_inode = inode;
      pipe_fd_array[count].rw_type = rw_type;
      pipe_fd_array[count].status_flags = flags;
      pipe_fd_array[count].fd_flags = fd_flags;
      count++;
    }
  }

  closedir(dir);
  return count;
}

int recreate_pipes(pipe_info_t *pipe_fd_array, int num_fds) {
  static void* handle = dlopen("libc.so.6", RTLD_LAZY);
  JASSERT(handle != NULL)("Failed to open libc");
  int (*real_pipe)(int*);
  *(void**)(&real_pipe) = dlsym(handle, "pipe");
  JASSERT(dlerror() == NULL)("Failed to find symbol for pipe");

  pipe_map_t pipe_map[MAX_PIPE_FDS] = {0};
  int map_count = 0;

  // Placeholder fd used to reserve the original pipe fd sites.
  int fd_unique = open("/dev/zero", O_RDONLY);

  // inspect_pipes() records every pipe fd present at checkpoint, including
  // pipes owned by other subsystems (e.g. MPI/Hydra PMI pipes and DMTCP's own
  // pipes) that DMTCP re-creates on restart.  Those fds are already occupied
  // here; the CUDA driver's own pipes are the ones DMTCP does not restore, so
  // their fd numbers are still free.  Keep only those free, CUDA-owned pipes
  // and reserve their fd sites; new pipes will land elsewhere.
  int kept = 0;
  for (int i = 0; i < num_fds; i++) {
    int fd = pipe_fd_array[i].fd;
    if (fd != fd_unique && fcntl(fd, F_GETFD) != -1) {
      continue; // occupied by another subsystem's restored pipe; not ours
    }
    pipe_fd_array[kept++] = pipe_fd_array[i];
    dup2(fd_unique, fd); // Occupy fd; close it later.
  }
  num_fds = kept;

  // Create a new pipe_map with the original pipes
  for (int i = 0; i < num_fds; i++) {
    unsigned long old_inode = pipe_fd_array[i].pipe_inode;
    int found = 0;
    for (int j = 0; j < map_count; j++) {
      if (pipe_map[j].old_inode == old_inode) {
          found = 1;
          break;
      }
    }
    JASSERT(map_count < MAX_PIPE_FDS);
    if (!found && pipe_map[map_count].created == 0) {
      int new_fds[2];
      JASSERT((*real_pipe)(new_fds) != -1);
      pipe_map[map_count].old_inode = old_inode;
      pipe_map[map_count].new_read_fd = new_fds[0];
      pipe_map[map_count].new_write_fd = new_fds[1];
      pipe_map[map_count].created = 1;
      map_count++;
    }
  }

  // Close fds that reserved sites for original pipes
  for (int i = 0; i < num_fds; i++) {
    close(pipe_fd_array[i].fd);
  }
  close(fd_unique);

  // Duplicate the new pipe ends onto the original FD numbers
  for (int i = 0; i < num_fds; i++) {
    unsigned long old_inode = pipe_fd_array[i].pipe_inode;
    int old_fd = pipe_fd_array[i].fd;
    pipe_rw_t rw_type = pipe_fd_array[i].rw_type;
    int new_fd_to_dup = -1;
    // Find the newly created pipe FDs using the old pipe inode
    for (int j = 0; j < map_count; j++) {
      if (pipe_map[j].old_inode == old_inode) {
        if (rw_type == PIPE_READ) {
          new_fd_to_dup = pipe_map[j].new_read_fd;
        } else if (rw_type == PIPE_WRITE) {
          new_fd_to_dup = pipe_map[j].new_write_fd;
        }
        break;
      }
    }
    assert(new_fd_to_dup != -1);
    int ret = dup2(new_fd_to_dup, old_fd);
    assert(ret != -1);
    // Restore the original pipe flags. dup2 gives old_fd the source pipe end's
    // (default) flags, so reapply: file-status flags via F_SETFL (the access
    // mode and creation-flag bits are ignored by F_SETFL, so mask them out),
    // and FD_CLOEXEC via F_SETFD (dup2 always clears close-on-exec on old_fd).
    JASSERT(fcntl(old_fd, F_SETFL,
                  pipe_fd_array[i].status_flags & ~O_ACCMODE) != -1)
      (old_fd)(JASSERT_ERRNO);
    JASSERT(fcntl(old_fd, F_SETFD, pipe_fd_array[i].fd_flags) != -1)
      (old_fd)(JASSERT_ERRNO);
  }
  
  // Final Step: Close the *original* new FDs that were used for duplication.
  // These are guaranteed not to intersect with the original pipe fds.
  for (int j = 0; j < map_count; j++) {
    close(pipe_map[j].new_read_fd);
    close(pipe_map[j].new_write_fd);
  }

  return 0;
}

// For debugging
void print_cuda_process_state() {
  int pid = getpid();
  switch (cuda_state) {
    case CU_PROCESS_STATE_RUNNING:
      printf("CUDA process %d is running\n", pid);
      break;
    case CU_PROCESS_STATE_LOCKED:
      printf("CUDA process %d is locked\n", pid);
      break;
    case CU_PROCESS_STATE_CHECKPOINTED:
      printf("CUDA process %d is checkpointed\n", pid);
      break;
    case CU_PROCESS_STATE_FAILED:
    default:
      printf("CUDA process %d is failed\n", pid);
      break;
  }
}

// checkpoint_gpu() needs to be called in DMTCP_EVENT_PRESUSPEND.
// CUDA's checkpoint APIs requires the user threads running for
// communication with GPU. 
static void checkpoint_gpu() {
  // Initialize CUDA driver API
  if (!cuda_initialized) {
    cuInit(0);
    cuda_initialized = 1;
  }
  int pid = getpid();

  CUcheckpointLockArgs lock_args = {0};
  CUcheckpointCheckpointArgs checkpoint_args = {0};

  CUresult ret = cuCheckpointProcessLock(pid, &lock_args);
  JASSERT(ret == CUDA_SUCCESS)(ret);

  // List all nvidia related device files. These device
  // files will be released by the following checkpoint
  // API. In future version of the API will reserve
  // these addresses by itself, but for now, we need to
  // help it to reserve these addresses so that DMTCP
  // and other codes won't use them and causing conflict
  // at the time of restart.
  dmtcp::ProcSelfMaps proc_maps;
  Area area;
  while (proc_maps.getNextArea(&area)) {
    if (strstr(area.name, "/dev/nvidia")) {
      nvidia_device_files.push_back(area);
    }
  }

  ret = cuCheckpointProcessCheckpoint(pid, &checkpoint_args);
  JASSERT(ret == CUDA_SUCCESS)(ret);
  ret = cuCheckpointProcessGetState(pid, &cuda_state);
  JASSERT(ret == CUDA_SUCCESS)(ret);
  JASSERT(cuda_state == CU_PROCESS_STATE_CHECKPOINTED);
  // Reserve segments for GPU device files that are released by CUDA checkpoint
  // function.
  // NOTE: NVIDIA already fixed the problem that device files are released after
  // checkpoint. The CUDA checkpoint APIs will reser those areas by itself.
  // Therefore, we don't need to reserve these segments after the fix is
  // available. We will check the CUDA version to decide whether to reserve these
  // segments or not.
  for (Area device_area : nvidia_device_files) {
    void *ret = mmap(device_area.addr, device_area.size, PROT_NONE,
                     MAP_FIXED | MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    JASSERT(ret == device_area.addr)(ret)(device_area.addr)(device_area.name);
  }
}

static void restore_gpu() {
  int pid = getpid();
  // Release reserved memories for NVIDIA device files.
  for (Area device_area : nvidia_device_files) {
    munmap(device_area.addr, device_area.size);
  }
  CUcheckpointUnlockArgs unlock_args = {0};
  CUresult ret = cuCheckpointProcessRestore(pid, &restore_args);
  JASSERT(ret == CUDA_SUCCESS)(ret);
  ret = cuCheckpointProcessUnlock(pid, &unlock_args);
  JASSERT(ret == CUDA_SUCCESS)(ret);
  ret = cuCheckpointProcessGetState(pid, &cuda_state);
  JASSERT(ret == CUDA_SUCCESS)(ret);
  JASSERT(cuda_state == CU_PROCESS_STATE_RUNNING)(cuda_state);
  nvidia_device_files.clear();
}

static trampoline_info_t eventfd_trampoline_info;
static trampoline_info_t pipe2_trampoline_info;

static int eventfd_wrapper(unsigned int initval, int flags) {
  return eventfd(initval, flags);
}

static int eventfd_trampoline(unsigned int initval, int flags) {
  /* Unpatch eventfd. */
  UNINSTALL_TRAMPOLINE(eventfd_trampoline_info);
  int retval = eventfd_wrapper(initval, flags);

  /* Repatch eventfd. */
  INSTALL_TRAMPOLINE(eventfd_trampoline_info);
  return retval;
}

static void cuda_event_hook(DmtcpEvent_t event, DmtcpEventData_t *data) {
  switch (event) {
    case DMTCP_EVENT_INIT:
      // The CUDA checkpoint API matches the process by its name on restart, so
      // the "DMTCP:" program-name prefix must NOT be applied. DMTCP only adds
      // that prefix when it was built with prgname-prefix enabled AND the
      // DMTCP_DISABLE_PRGNAME_PREFIX env var is unset (see threadlist.cpp). If
      // both hold, restart of a CUDA process would fail, so refuse to launch.
#ifdef ENABLE_PRGNAME_PREFIX
      JASSERT(getenv(ENV_VAR_DISABLE_PRGNAME_PREFIX) != NULL)
        .Text("The CUDA plugin requires the DMTCP program-name prefix to be "
              "disabled; otherwise the CUDA checkpoint API fails after restart. "
              "Disable it using either approach:\n"
              "  1. Build DMTCP with ./configure --disable-prgname-prefix, or\n"
              "  2. Set the environment variable DMTCP_DISABLE_PRGNAME_PREFIX=1");
#endif
      dmtcp_setup_trampoline("eventfd", (void *)&eventfd_trampoline,
                             &eventfd_trampoline_info);
      break;

    case DMTCP_EVENT_RUNNING:
      /************************************************************************
       * NOTE: This event will be triggered whenever the user threads starts
       * running. It could be after initial launch, after resuming from
       * checkpoint, or after restart from disk.
       * Be aware that it's allowed to start a checkpoint at this stage.
       ***********************************************************************/
      DMTCP_PLUGIN_DISABLE_CKPT();    
      if (cuda_state == CU_PROCESS_STATE_RUNNING) {
        // In this case, the CUDA process is launched for the first time.
      } else if (cuda_state == CU_PROCESS_STATE_CHECKPOINTED) {
        INSTALL_TRAMPOLINE(eventfd_trampoline_info);
        // In this case, the CUDA process has been checkpointed, it can be
        // either restart or resume. We need to restore the GPU states and
        // memories.
        restore_gpu();
        nvidia_device_files.clear();
      }
      DMTCP_PLUGIN_ENABLE_CKPT();    
      break;

    case DMTCP_EVENT_PRESUSPEND:
      // We need to suspend and checkpoint the GPU before suspending user
      // threads on the CPU.
      checkpoint_gpu();
      break;

    case DMTCP_EVENT_PRECHECKPOINT:
      UNINSTALL_TRAMPOLINE(eventfd_trampoline_info);
      num_fds_found = inspect_pipes(pipe_list, MAX_PIPE_FDS);
      JASSERT(num_fds_found >= 0);
      break;

    case DMTCP_EVENT_RESUME:
      /************************************************************************
       * cuCheckpointProcessCheckpoint() sets the state of the GPU process
       * to CHECKPOINTED, and released all related resources.
       *
       * However, cuCheckpointProcessUnlock() requies the GPU process in the
       * LOCKED state. We need to restore the GPU even for resume to recover
       * GPU resources and set the GPU process state to the LOCKED state.
       *
       * To do so, we need to release user threads on the CPU first. Therefore,
       * we need to restore the GPU in the DMTCP_EVENT_RUNNING hook. Check
       * the comments and codes in the DMTCP_EVENT_RUNNING above.
       ************************************************************************/
      break;

    case DMTCP_EVENT_RESTART:
      // See comments in DMTCP_EVENT_RESUME above.
      JASSERT(recreate_pipes(pipe_list, num_fds_found) != -1);
      break;
    
    default:
      break;
  }
}

DmtcpPluginDescriptor_t cuda_plugin = {
  DMTCP_PLUGIN_API_VERSION,
  DMTCP_PACKAGE_VERSION,
  "CUDA-Plugin",
  "DMTCP",
  "xu.yao1@northeastern.edu",
  "CUDA Checkpoint API plugin",
  cuda_event_hook
};

DMTCP_DECL_PLUGIN(cuda_plugin);
