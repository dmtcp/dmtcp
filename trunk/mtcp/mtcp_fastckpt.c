#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <errno.h>
#include "mtcp_internal.h"

#ifdef FAST_CKPT_RST_VIA_MMAP
static mtcp_ckpt_image_header_t *ckpt_image_header = NULL;
static int curr_area_idx = 0;
static Area *area_array = NULL;

VA fastckpt_mmap_addr()
{
  return ckpt_image_header->start_addr;
}

void fastckpt_write_mem_region(int fd, Area *area)
{
  area->mem_region_offset = ckpt_image_header->hdr_offset_in_file
                            + ckpt_image_header->maps_offset
                            + ckpt_image_header->VmSize;
  memcpy(&area_array[curr_area_idx], area, sizeof(*area));
  curr_area_idx++;

  mtcp_sys_lseek(fd, area->mem_region_offset, SEEK_SET);
  mtcp_writefile(fd, area->addr, area->size);

  ckpt_image_header->VmSize += area->size;
  ckpt_image_header->num_memory_regions += 1;
}

void fastckpt_get_mem_region_info(size_t *VmSize, size_t *num_mem_regions)
{
  int fd = mtcp_sys_open2 ("/proc/self/maps", O_RDONLY);
  if (fd < 0) {
    MTCP_PRINTF("error opening /proc/self/maps: %d\n", mtcp_sys_errno);
    mtcp_abort ();
  }

  char ch;
  while ((ch = mtcp_readchar(fd)) != '\0') {
    if (ch == '\n')
      (*num_mem_regions)++;
  }
  mtcp_sys_close(fd);

  fd = mtcp_sys_open2("/proc/self/statm", O_RDONLY);
  if (fd < 0) {
    MTCP_PRINTF("error opening /proc/self/statm: %d\n", mtcp_sys_errno);
    mtcp_abort ();
  }

  size_t num_pages = 0;
  if (mtcp_readdec(fd, (VA *) &num_pages) != ' ' || num_pages == 0) {
    MTCP_PRINTF("error reading /proc/self/statm");
    mtcp_abort();
  }
  *VmSize = num_pages * MTCP_PAGE_SIZE;
  return;
}

off_t fastckpt_align_offset(int fd, int isRestart)
{
  off_t curr_offset = mtcp_sys_lseek(fd, 0, SEEK_CUR);
  if (curr_offset % MTCP_PAGE_SIZE != 0) {
    size_t padding = (MTCP_PAGE_SIZE
                      - (curr_offset % MTCP_PAGE_SIZE)) % MTCP_PAGE_SIZE;
    if (!isRestart) {
      mtcp_sys_ftruncate(fd, curr_offset + padding);
    }
    curr_offset = mtcp_sys_lseek(fd, padding, SEEK_CUR);
  }
  return curr_offset;
}

void fastckpt_prepare_for_ckpt(int fd, VA restore_start, VA finishrestore)
{
  size_t num_memory_regions = 0;
  size_t VmSize = 0;

  fastckpt_get_mem_region_info(&VmSize, &num_memory_regions);

  off_t curr_offset = fastckpt_align_offset(fd, 0);

  size_t maps_offset = (sizeof (mtcp_ckpt_image_header_t)
                        + (sizeof (Area) * num_memory_regions)
                        + (MTCP_PAGE_SIZE - 1)) & MTCP_PAGE_MASK;

  mtcp_sys_ftruncate(fd, curr_offset + maps_offset);
  VA hdr_addr = (VA) mtcp_sys_mmap(NULL, maps_offset, PROT_READ | PROT_WRITE,
                                   MAP_SHARED, fd, curr_offset);
  if (hdr_addr == MAP_FAILED) {
    MTCP_PRINTF("mmap(NULL, %x, ..., ..., %d, %x) failed with error :%d\n",
                maps_offset, fd, curr_offset, mtcp_sys_errno);
    mtcp_abort();
  }

  ckpt_image_header = (mtcp_ckpt_image_header_t*) hdr_addr;
  memset(ckpt_image_header, 0, sizeof(ckpt_image_header));

  // TODO : place this lseek at proper place
  mtcp_sys_lseek(fd, maps_offset, SEEK_CUR);

  ckpt_image_header->ckpt_image_version = MTCP_CKPT_IMAGE_VERSION;
  ckpt_image_header->start_addr = hdr_addr;
  ckpt_image_header->hdr_offset_in_file = curr_offset;
  ckpt_image_header->maps_offset = maps_offset;
  ckpt_image_header->num_memory_regions = 0;
  ckpt_image_header->VmSize = 0;

  ckpt_image_header->restore_start_fncptr = restore_start;
  ckpt_image_header->finish_retore_fncptr = finishrestore;

  mtcp_sys_getrlimit(RLIMIT_STACK, &ckpt_image_header->stack_rlimit);

  DPRINTF("saved stack resource limit: soft_lim:%p, hard_lim:%p\n",
          ckpt_image_header->stack_rlimit.rlim_cur,
          ckpt_image_header->stack_rlimit.rlim_max);

  area_array = (Area*) (hdr_addr + sizeof (mtcp_ckpt_image_header_t));
  curr_area_idx = 0;
}

void fastckpt_save_restore_image(int fd, VA restore_begin, size_t restore_size)
{
  Area area;

  ckpt_image_header->restore_begin = restore_begin;
  ckpt_image_header->restore_size = restore_size;


  area.addr = restore_begin;
  area.size = restore_size;
  area.prot = PROT_READ | PROT_WRITE | PROT_EXEC;
  area.flags = MAP_PRIVATE | MAP_ANONYMOUS;
  area.offset = 0;
  strcpy(area.name, "[libmtcp.so]");

  fastckpt_write_mem_region(fd, &area);
}

void fastckpt_finish_ckpt(int fd)
{
  mtcp_sys_munmap(ckpt_image_header->start_addr,
                  ckpt_image_header->maps_offset);
  ckpt_image_header = NULL;
  curr_area_idx = 0;
  area_array = NULL;
}

__attribute__ ((visibility ("hidden")))
void fastckpt_read_header(int fd, struct rlimit *stack_rlimit, Area *area,
                          VA *restore_start)
{
  off_t curr_offset = fastckpt_align_offset(fd, 1);

  VA addr = mtcp_sys_mmap(0, sizeof(mtcp_ckpt_image_header_t) + sizeof(Area),
                          PROT_READ, MAP_PRIVATE, fd, curr_offset);
  if (addr == MAP_FAILED) {
    MTCP_PRINTF("mmap failed with error: %d\n", mtcp_sys_errno);
    mtcp_abort();
  }
  mtcp_ckpt_image_header_t *hdr = (mtcp_ckpt_image_header_t*) addr;

  *stack_rlimit = hdr->stack_rlimit;
  *area = *(Area *)((VA)hdr + sizeof(*hdr));
  *restore_start = hdr->restore_start_fncptr;

  mtcp_sys_munmap(addr, sizeof(mtcp_ckpt_image_header_t));
}

__attribute__ ((visibility ("hidden")))
void fastckpt_prepare_for_restore(int fd)
{
  off_t curr_offset = fastckpt_align_offset(fd, 1);

  VA addr = mtcp_sys_mmap(0, sizeof(mtcp_ckpt_image_header_t), PROT_READ,
                          MAP_PRIVATE, fd, curr_offset);
  if (addr == MAP_FAILED) {
    MTCP_PRINTF("mmap failed with error: %d\n", mtcp_sys_errno);
    mtcp_abort();
  }
  mtcp_ckpt_image_header_t hdr = *(mtcp_ckpt_image_header_t*) addr;
  if (mtcp_sys_munmap(addr, sizeof(mtcp_ckpt_image_header_t)) == -1) {
    MTCP_PRINTF("munmap failed with error: %d\n", mtcp_sys_errno);
    mtcp_abort();
  }

  addr = mtcp_sys_mmap(hdr.start_addr, hdr.maps_offset, PROT_READ,
                       MAP_PRIVATE, fd, hdr.hdr_offset_in_file);
  if (addr == MAP_FAILED) {
    MTCP_PRINTF("mmap failed with error: %d\n", mtcp_sys_errno);
    mtcp_abort();
  }

  ckpt_image_header = (mtcp_ckpt_image_header_t*) addr;
  curr_area_idx = 1;
  area_array = (Area*) (ckpt_image_header->start_addr +
                        sizeof(mtcp_ckpt_image_header_t));
}

__attribute__ ((visibility ("hidden")))
VA fastckpt_get_finishrestore()
{
  if (ckpt_image_header == NULL) {
    MTCP_PRINTF("Did you call fastckpt_prepare_for_restore already?\n");
    mtcp_abort();
  }
  return ckpt_image_header->finish_retore_fncptr;
}

__attribute__ ((visibility ("hidden")))
void fastckpt_finish_restore()
{
  int res = mtcp_sys_munmap(ckpt_image_header, ckpt_image_header->maps_offset);
  if (res == -1) {
    MTCP_PRINTF("munmap failed with error: %d\n", mtcp_sys_errno);
    mtcp_abort();
  }

  ckpt_image_header = NULL;
  curr_area_idx = 0;
  area_array = NULL;
}

__attribute__ ((visibility ("hidden")))
void fastckpt_load_restore_image(int fd, Area *area)
{
  fastckpt_restore_mem_region(fd, area);
}

__attribute__ ((visibility ("hidden")))
int fastckpt_get_next_area_dscr(Area *area)
{
  if (curr_area_idx == ckpt_image_header->num_memory_regions) {
    DPRINTF("Next memory region not found in CKPT Image\n"
            "  num_memory_regions: %d", ckpt_image_header->num_memory_regions);
    return 0;
  }

  *area = area_array[curr_area_idx];
  curr_area_idx ++;
  return 1;
}

__attribute__ ((visibility ("hidden")))
static int fastckpt_restore_without_mmap(int fd, const Area *area)
{
  int imagefd = mtcp_sys_open (area->name, O_RDONLY, 0);
  if (imagefd < 0) {
    MTCP_PRINTF("Error %d open()ing %s. Continuing anyway\n",
                mtcp_sys_errno, area->name);
    return 0;
  }

  /* POSIX says mmap would unmap old memory.  Munmap never fails if args
   * are valid.  Can we unmap vdso and vsyscall in Linux?  Used to use
   * mtcp_safemmap here to check for address conflicts.
   */
  VA mmappedat = (VA) mtcp_sys_mmap (area->addr, area->size,
                                     area->prot | PROT_WRITE,
                                     MAP_PRIVATE, imagefd, area->offset);
  if (mmappedat != area->addr) {
    MTCP_PRINTF("error %d mapping %p bytes at %p. return value: %p\n",
                mtcp_sys_errno, area->size, area->addr, mmappedat);
    mtcp_abort();
  }


  /* Set the proper offset in ckpt image and call readfile */
  off_t curr_offset = mtcp_sys_lseek(fd, 0, SEEK_CUR);
  if (mtcp_sys_lseek(fd, area->mem_region_offset, SEEK_SET) == (off_t) -1) {
    MTCP_PRINTF("error %d performing lseek(%d, %d, SEEK_CUR).\n",
                mtcp_sys_errno, fd, area->mem_region_offset);
    mtcp_abort();
  }
  mtcp_readfile (fd, area->addr, area->size);
  if (mtcp_sys_lseek(fd, curr_offset, SEEK_SET) == (off_t) -1) {
    MTCP_PRINTF("error %d performing lseek(%d, %d, SEEK_CUR).\n",
                mtcp_sys_errno, fd, area->mem_region_offset);
    mtcp_abort();
  }
  return 1;
}

__attribute__ ((visibility ("hidden")))
void fastckpt_restore_mem_region(int fd, const Area *area)
{
  if (mtcp_strstartswith(area->name, "/") &&
      mtcp_strendswith(area->name, ".so") &&
      mtcp_strstr(area->name, "libc") &&
      fastckpt_restore_without_mmap(fd, area)) {
    return;
  }

  VA addr = (VA) mtcp_sys_mmap (area->addr, area->size, area->prot,
                                MAP_PRIVATE | MAP_FIXED, fd,
                                area->mem_region_offset);
  if (addr == MAP_FAILED) {
    DPRINTF("error %d mapping %p bytes at %p.\n",
            mtcp_sys_errno, area->size, area->addr);
    mtcp_abort ();
  }
  if (addr != area->addr) {
    MTCP_PRINTF("area at %p got mmapped to %p\n", area->addr, addr);
    mtcp_abort ();
  }
}

__attribute__ ((visibility ("hidden")))
void fastckpt_populate_shared_file_from_ckpt_image(int fd, int imagefd,
                                                   Area* area)
{
  VA tmp_addr = mtcp_sys_mmap(0, area->size, PROT_READ, MAP_PRIVATE,
                              fd, area->mem_region_offset);
  if (tmp_addr == MAP_FAILED) {
    MTCP_PRINTF("mmap failed with error: %d\n", mtcp_sys_errno);
    mtcp_abort();
  }

  ssize_t ret = mtcp_write_all(imagefd, tmp_addr, area->size);
  if (ret != area->size) {
    MTCP_PRINTF("Error %d writing data to shared file.\n", mtcp_sys_errno);
    mtcp_abort();
  }

  if (mtcp_sys_munmap(tmp_addr, area->size) == -1) {
    MTCP_PRINTF("error %d unmapping temp memory at %p\n",
                mtcp_sys_errno, area->addr);
    mtcp_abort ();
  }
}

#endif
