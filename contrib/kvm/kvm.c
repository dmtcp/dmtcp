#include <errno.h>
#include <linux/kvm.h> /* For all the kvm data structs */
#include <stdarg.h>    /* For va_arg(), etc. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>    /* For strcmp(), etc. */
#include <sys/ioctl.h> /* For ioctl() */
#include <sys/mman.h>  /* For mmap() */
#include <unistd.h>
#if 0
#include <sys/utsname.h> /* For uname */
#endif                   /* if 0 */

#include "config.h"
#include "dmtcp.h"

#define DEBUG_SIGNATURE "DEBUG [KVM Plugin]: "
#define MAX_MSR_ENTRIES 100
#define MAX_MEM_REGIONS 32
#define MAX_IRQ_ROUTES  65
#define SIGMASK_SIZE    (sizeof(struct kvm_signal_mask) + sizeof(sigset_t))

// #define KVM_PLUGIN_DEBUG

#ifdef KVM_PLUGIN_DEBUG
# define DPRINTF(fmt, ...) \
  do { fprintf(stderr, DEBUG_SIGNATURE fmt, ## __VA_ARGS__); } while (0)
#else /* ifdef KVM_PLUGIN_DEBUG */
# define DPRINTF(fmt, ...) \
  do {} while (0)
#endif /* ifdef KVM_PLUGIN_DEBUG */

/*============================================================================*/
/*============================= START GLOBAL DATA ============================*/
/*============================================================================*/

// Yuck starts
static int g_kvm_fd;
static int g_vcpu_fd; /* NOTE: We do not support SMP currently */
static int g_vm_fd;
static void *g_kvm_vm_arg;
static void *g_kvm_vcpu_arg;
static int g_long_mode_supported = 0; /* True for x86-64 */
static void *g_vcpu_mmap_addr;
static int g_vcpu_mmap_prot;
static size_t g_vcpu_mmap_length;
static int g_vcpu_mmap_flags;
static uint64_t g_kvm_id_map_addr;
static void *g_kvm_tss_addr;
static void *g_kvm_irqchip_arg;
static int g_num_of_memory_regions;

static struct kvm_regs g_kvm_regs;
static struct kvm_xsave g_kvm_xsave;
static struct kvm_xcrs g_kvm_xcrs;
static struct kvm_sregs g_kvm_sregs;
static struct {
  struct kvm_msrs info;
  struct kvm_msr_entry entries[MAX_MSR_ENTRIES];
} g_kvm_msrs;
static struct kvm_mp_state g_kvm_mp_state;
static struct kvm_vcpu_events g_kvm_vcpu_events;
static struct kvm_debugregs g_kvm_dbgregs;
static struct kvm_lapic_state g_kvm_lapic;
static struct kvm_irqchip g_kvm_irqchip_pic_master,
                          g_kvm_irqchip_pic_slave,
                          g_kvm_irqchip_ioapic;
static struct kvm_pit_config g_kvm_pit2_config;
static struct kvm_pit_state2 g_kvm_pit2;
static struct kvm_vapic_addr g_kvm_vapic_addr;
static struct kvm_userspace_memory_region g_kvm_mem_region[MAX_MEM_REGIONS];
static struct kvm_irq_routing *g_kvm_gsi_routing_table;
static uint8_t g_kvm_sigmask[SIGMASK_SIZE];

/* NOTE: We only support one zone for now. */
static struct kvm_coalesced_mmio_zone g_kvm_coalesced_mmio_zone;
static struct kvm_tpr_access_ctl g_kvm_tpr_access_ctl;
static struct kvm_irq_level g_kvm_irq_level;

// Yuck ends

/*============================================================================*/
/*============================= END GLOBAL DATA ==============================*/
/*============================================================================*/

/*============================================================================*/
/*========================= START PRIVATE FUNCTIONS ==========================*/
/*============================================================================*/

static char *g_mp_states[] = { "RUNNABLE", "UNINITIALIZED", "INIT_RECEIVED",
                               "HALTED", "SIPI_RECEIVED" };

static char *
get_mp_state_string(struct kvm_mp_state p_mp_state)
{
  if (p_mp_state.mp_state > 4 || p_mp_state.mp_state < 0) {
    DPRINTF("Unknown MP State: %d\n", p_mp_state.mp_state);
    return NULL;
  }
  return g_mp_states[p_mp_state.mp_state];
}

#if 0
static int
check_long_mode_support()
{
  struct utsname buf;
  static int long_mode_supported;

  uname(&buf);
  long_mode_supported = (strcmp(buf.machine, "x86_64") == 0);

  return long_mode_supported;
}
#endif /* if 0 */

static int
save_supported_msrs()
{
  int ret;

  /* IMPORTANT: See note for KVM_GET_MSRS below in the ioctl wrapper. */
#if 0
  int i, n;

  n = 0;
  g_kvm_msrs.entries[n++].index = MSR_IA32_SYSENTER_CS;
  g_kvm_msrs.entries[n++].index = MSR_IA32_SYSENTER_ESP;
  g_kvm_msrs.entries[n++].index = MSR_IA32_SYSENTER_EIP;
  g_kvm_msrs.entries[n++].index = MSR_PAT;
  if (has_msr_star) {
    g_kvm_msrs.entries[n++].index = MSR_STAR;
  }
  if (has_msr_hsave_pa) {
    g_kvm_msrs.entries[n++].index = MSR_VM_HSAVE_PA;
  }
  if (has_msr_tsc_deadline) {
    g_kvm_msrs.entries[n++].index = MSR_IA32_TSCDEADLINE;
  }
  if (has_msr_misc_enable) {
    g_kvm_msrs.entries[n++].index = MSR_IA32_MISC_ENABLE;
  }

  if (!env->tsc_valid) {
    g_kvm_msrs.entries[n++].index = MSR_IA32_TSC;
    env->tsc_valid = !runstate_is_running();
  }

  if (long_mode_supported) {
    g_kvm_msrs.entries[n++].index = MSR_CSTAR;
    g_kvm_msrs.entries[n++].index = MSR_KERNELGSBASE;
    g_kvm_msrs.entries[n++].index = MSR_FMASK;
    g_kvm_msrs.entries[n++].index = MSR_LSTAR;
  }
  g_kvm_msrs.entries[n++].index = MSR_KVM_SYSTEM_TIME;
  g_kvm_msrs.entries[n++].index = MSR_KVM_WALL_CLOCK;
  if (has_msr_async_pf_en) {
    g_kvm_msrs.entries[n++].index = MSR_KVM_ASYNC_PF_EN;
  }
  if (has_msr_pv_eoi_en) {
    g_kvm_msrs.entries[n++].index = MSR_KVM_PV_EOI_EN;
  }

  if (env->mcg_cap) {
    g_kvm_msrs.entries[n++].index = MSR_MCG_STATUS;
    g_kvm_msrs.entries[n++].index = MSR_MCG_CTL;
    for (i = 0; i < (env->mcg_cap & 0xff) * 4; i++) {
      g_kvm_msrs.entries[n++].index = MSR_MC0_CTL + i;
    }
  }

  g_kvm_msrs.info.nmsrs = n;
#endif /* if 0 */
  ret = NEXT_FNC(ioctl)(g_vcpu_fd, KVM_GET_MSRS, &g_kvm_msrs);
  if (ret < 0) {
    return ret;
  }

  return 0;
}

static int
save_irqchip()
{
  int ret;
  static int (*next_fnc)() = NULL; /* same type signature as ioctl */

  g_kvm_irqchip_pic_master.chip_id = KVM_IRQCHIP_PIC_MASTER;
  g_kvm_irqchip_pic_slave.chip_id = KVM_IRQCHIP_PIC_SLAVE;
  g_kvm_irqchip_ioapic.chip_id = KVM_IRQCHIP_IOAPIC;

  ret = NEXT_FNC(ioctl)(g_vm_fd, KVM_GET_IRQCHIP, &g_kvm_irqchip_pic_master);
  if (ret < 0) {
    return ret;
  }
  ret = NEXT_FNC(ioctl)(g_vm_fd, KVM_GET_IRQCHIP, &g_kvm_irqchip_pic_slave);
  if (ret < 0) {
    return ret;
  }
  ret = NEXT_FNC(ioctl)(g_vm_fd, KVM_GET_IRQCHIP, &g_kvm_irqchip_ioapic);
  if (ret < 0) {
    return ret;
  }

  return ret;
}

static int
save_pit2()
{
  int ret;
  static int (*next_fnc)() = NULL; /* same type signature as ioctl */

  ret = NEXT_FNC(ioctl)(g_vm_fd, KVM_GET_PIT2, &g_kvm_pit2);

  return ret;
}

static int
save_registers()
{
  int ret;
  static int (*next_fnc)() = NULL; /* Same type signature as ioctl */

  DPRINTF("Trying to get registers\n");

  ret = NEXT_FNC(ioctl)(g_vcpu_fd, KVM_GET_REGS, &g_kvm_regs);
  if (ret < 0) {
    return ret;
  }

  ret = NEXT_FNC(ioctl)(g_vcpu_fd, KVM_GET_XSAVE, &g_kvm_xsave);
  if (ret < 0) {
    return ret;
  }

  ret = NEXT_FNC(ioctl)(g_vcpu_fd, KVM_GET_XCRS, &g_kvm_xcrs);
  if (ret < 0) {
    return ret;
  }
  ret = NEXT_FNC(ioctl)(g_vcpu_fd, KVM_GET_SREGS, &g_kvm_sregs);
  if (ret < 0) {
    return ret;
  }
  ret = NEXT_FNC(ioctl)(g_vcpu_fd, KVM_GET_MSRS, &g_kvm_msrs);
  if (ret < 0) {
    return ret;
  }
  ret = NEXT_FNC(ioctl)(g_vcpu_fd, KVM_GET_MP_STATE, &g_kvm_mp_state);
  DPRINTF("************************** GOT MP STATE: %s\n",
          get_mp_state_string(g_kvm_mp_state));
  if (ret < 0) {
    return ret;
  }
  ret = NEXT_FNC(ioctl)(g_vcpu_fd, KVM_GET_LAPIC, &g_kvm_lapic);
  if (ret < 0) {
    return ret;
  }
#if 0
  ret = NEXT_FNC(ioctl)(g_vcpu_fd, KVM_GET_VCPU_EVENTS, &g_kvm_vcpu_events);
  if (ret < 0) {
    return ret;
  }
#endif /* if 0 */
  ret = NEXT_FNC(ioctl)(g_vcpu_fd, KVM_GET_DEBUGREGS, &g_kvm_dbgregs);
  if (ret < 0) {
    return ret;
  }

  DPRINTF("Get registers done\n");
  return 0;
}

static int
create_vm()
{
  int ret;
  static int (*next_fnc)() = NULL; /* same type signature as ioctl */

  DPRINTF("Creating new VMFD\n");
  ret = NEXT_FNC(ioctl)(g_kvm_fd, KVM_CREATE_VM, g_kvm_vm_arg);
  if (ret < 0) {
    return ret;
  }
  g_vm_fd = dup2(ret, g_vm_fd);
  if (g_vm_fd < 0) {
    perror("dup2(): Duplicating new VMFD");
  }
  return g_vm_fd;
}

static int
create_vcpu()
{
  int ret;
  static int (*next_fnc)() = NULL; /* same type signature as ioctl */

  ret = NEXT_FNC(ioctl)(g_vm_fd, KVM_CREATE_VCPU, g_kvm_vcpu_arg);
  if (ret < 0) {
    return ret;
  }

  g_vcpu_fd = dup2(ret, g_vcpu_fd);
  if (g_vcpu_fd < 0) {
    return g_vcpu_fd;
  }
  return 0;
}

static int
create_irqchip()
{
  int ret;
  static int (*next_fnc)() = NULL; /* same type signature as ioctl */

  DPRINTF("Creating IRQCHIP\n");
  ret = NEXT_FNC(ioctl)(g_vm_fd, KVM_CREATE_IRQCHIP, g_kvm_irqchip_arg);
  return ret;
}

static int
create_pit2()
{
  int ret;
  static int (*next_fnc)() = NULL; /* same type signature as ioctl */

  DPRINTF("Creating PIT2\n");
  ret = NEXT_FNC(ioctl)(g_vm_fd, KVM_CREATE_PIT2, &g_kvm_pit2_config);
  if (ret < 0) {
    perror("ioctl(KVM_CREATE_PIT2)");
  }
  return ret;
}

static int
restore_id_map_addr()
{
  int ret;
  static int (*next_fnc)() = NULL; /* same type signature as ioctl */

  DPRINTF("Setting identity map address.\n");
  ret = NEXT_FNC(ioctl)(g_vm_fd, KVM_SET_IDENTITY_MAP_ADDR, &g_kvm_id_map_addr);
  if (ret < 0) {
    DPRINTF("Could not set id-map\n");
    return ret;
  }

  return 0;
}

static int
restore_tss_addr()
{
  int ret;
  static int (*next_fnc)() = NULL; /* same type signature as ioctl */

  DPRINTF("Setting tss address.\n");
  ret = NEXT_FNC(ioctl)(g_vm_fd, KVM_SET_TSS_ADDR, g_kvm_tss_addr);
  if (ret < 0) {
    DPRINTF("Could not set tss address.\n");
    return ret;
  }

  return 0;
}

static int
restore_irqchip()
{
  int ret;
  static int (*next_fnc)() = NULL; /* same type signature as ioctl */

  ret = NEXT_FNC(ioctl)(g_vm_fd, KVM_SET_IRQCHIP, &g_kvm_irqchip_pic_master);
  if (ret < 0) {
    return ret;
  }
  ret = NEXT_FNC(ioctl)(g_vm_fd, KVM_SET_IRQCHIP, &g_kvm_irqchip_pic_slave);
  if (ret < 0) {
    return ret;
  }
  ret = NEXT_FNC(ioctl)(g_vm_fd, KVM_SET_IRQCHIP, &g_kvm_irqchip_ioapic);
  if (ret < 0) {
    return ret;
  }

  return ret;
}

static int
restore_pit2()
{
  int ret;
  static int (*next_fnc)() = NULL; /* same type signature as ioctl */

  ret = NEXT_FNC(ioctl)(g_vm_fd, KVM_SET_PIT2, &g_kvm_pit2);
  if (ret < 0) {
    perror("ioctl(KVM_SET_PIT2)");
  }

  return ret;
}

static int
inject_mce()
{
  /* Not required. */
  return 0;
}

static int
restore_registers()
{
  int ret;
  static int (*next_fnc)() = NULL; /* same type signature as ioctl */


  DPRINTF("Trying to put registers\n");

  ret = NEXT_FNC(ioctl)(g_vcpu_fd, KVM_SET_REGS, &g_kvm_regs);
  if (ret < 0) {
    perror("ioctl(KVM_SET_REGS)");
    return ret;
  }
  ret = NEXT_FNC(ioctl)(g_vcpu_fd, KVM_SET_XSAVE, &g_kvm_xsave);
  if (ret < 0) {
    perror("ioctl(KVM_SET_XSAVE)");
    return ret;
  }
  ret = NEXT_FNC(ioctl)(g_vcpu_fd, KVM_SET_XCRS, &g_kvm_xcrs);
  if (ret < 0) {
    perror("ioctl(KVM_SET_XCRS)");
    return ret;
  }
  ret = NEXT_FNC(ioctl)(g_vcpu_fd, KVM_SET_SREGS, &g_kvm_sregs);
  if (ret < 0) {
    perror("ioctl(KVM_SET_SREGS)");
    return ret;
  }

  /* must be before kvm_put_msrs */
  ret = inject_mce();
  if (ret < 0) {
    perror("ioctl(KVM_SET_MCE)");
    return ret;
  }
  ret = NEXT_FNC(ioctl)(g_vcpu_fd, KVM_SET_MSRS, &g_kvm_msrs);
  if (ret < 0) {
    perror("ioctl(KVM_SET_MSRS)");
    return ret;
  }

  // DPRINTF("Not setting MP State to: %s\n",
  // get_mp_state_string(g_kvm_mp_state));
#if 0
  ret = NEXT_FNC(ioctl)(g_vcpu_fd, KVM_SET_MP_STATE, &g_kvm_mp_state);
  if (ret < 0) {
    perror("ioctl(KVM_SET_MP_STATE)");
    return ret;
  }
  ret = NEXT_FNC(ioctl)(g_vcpu_fd, KVM_SET_LAPIC, &g_kvm_lapic);
  if (ret < 0) {
    perror("ioctl(KVM_SET_LAPIC)");
    return ret;
  }
  ret = NEXT_FNC(ioctl)(g_vcpu_fd, KVM_SET_VCPU_EVENTS, &g_kvm_vcpu_events);
  if (ret < 0) {
    perror("ioctl(KVM_SET_VCPU_EVENTS)");
    return ret;
  }
#endif /* if 0 */
  ret = NEXT_FNC(ioctl)(g_vcpu_fd, KVM_SET_DEBUGREGS, &g_kvm_dbgregs);
  if (ret < 0) {
    perror("ioctl(KVM_SET_DEBUGREGS)");
    return ret;
  }

  /* must be last */

  /*
  ret = kvm_guest_debug_workarounds();
  if (ret < 0) {
    return ret;
  }
  */
  DPRINTF("Put registers done\n");
  return 0;
}

static int
process_memory_region(const struct kvm_userspace_memory_region *mem)
{
  memcpy(&g_kvm_mem_region[mem->slot], mem,
         sizeof(struct kvm_userspace_memory_region));
  g_num_of_memory_regions = (mem->slot > g_num_of_memory_regions) ? mem->slot :
    g_num_of_memory_regions;
  return 0;
}

/*============================================================================*/
/*=========================== END PRIVATE FUNCTIONS ==========================*/
/*============================================================================*/

/*============================================================================*/
/*========================== START WRAPPER FUNCTIONS =========================*/
/*============================================================================*/

/* This is the wrapper for mmap()
 *  Used for saving the params for mmap of vcpu fd
 */
void *
mmap64(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
  static void *(*next_fnc)() = NULL;

  void *result = NEXT_FNC(mmap)(addr, length, prot, flags,
                                fd, offset);

  if (fd == g_vcpu_fd) {
    DPRINTF("Saving the vcpu mmap64 params. addr: %p, length: %zu, prot: %d, "
            "flags: %d, offset: %ld\n",
            addr, length, prot, flags, (long)offset);
    g_vcpu_mmap_addr = result;
    g_vcpu_mmap_prot = prot;
    g_vcpu_mmap_length = length;
    g_vcpu_mmap_flags = flags;

    /* NOTE: The offset is assumed to be zero. */
  }
  return result;
}

/*
 * This is the wrapper for ioctl()
 *  Assumption: QEMU makes three argument ioctl calls to KVM. This will need to
 *              be changed if this assumption is broken.
 *  NOTE: The man page (and POSIX) says that the request type is signed int, but
 *        the Linux kernel, and glibc all treat it as unsigned.
 */
int
ioctl(int fd, unsigned long int request, ...)
{
  va_list argp;
  static int (*next_fnc)() = NULL; /* Same type signature as ioctl */
  static int p_first_get_msr_index_list_call = 1;
  int result;
  void *arg;
  static int init = 1;
  static int init2 = 1;

  va_start(argp, request);
  arg = va_arg(argp, void *);
  va_end(argp);
  result = NEXT_FNC(ioctl)(fd, request, arg);

  /* This is NOT a hack. Kernel declares/defines it as an unsigned int. Glibc
   * for some reason decided to declare/define it as unsigned long. GCC-4.7.3
   * does a sign-extension by default which results in wrong values.
   * See http://sourceware.org/bugzilla/show_bug.cgi?id=14362 */
  switch ((unsigned int)request) {
  case KVM_CREATE_VM:
  {
    DPRINTF("Saving KVM VM. arg: %p\n", arg);
    g_kvm_fd = fd;
    g_vm_fd = result;
    g_kvm_vm_arg = arg;
    DPRINTF("Got VMFD: %u\n", g_vm_fd);
    break;
  }
  case KVM_CREATE_VCPU:
  {
    DPRINTF("Saving KVM VCPU. arg: %p\n", arg);
    g_vcpu_fd = result;
    g_kvm_vcpu_arg = arg;
    DPRINTF("Got VCPUFD %u\n", g_vcpu_fd);
    break;
  }
  case KVM_GET_MSRS:
  {
    /* NOTE: We rely on QEMU to make this call to get to know the supported
     *       msrs, which it must have determined using the
     *       KVM_GET_MSR_INDEX_LIST call at init. This saves us some cycles
     *       but it will fail if QEMU never makes the KVM_GET_MSRS call.
     */
    if (fd == g_vcpu_fd) {
      DPRINTF("Saving supported MSRS from QEMU\n");
      memcpy(&g_kvm_msrs, arg, sizeof(g_kvm_msrs));
    }
    break;
  }
  case KVM_SET_SIGNAL_MASK:
  {
    memcpy(&g_kvm_sigmask, arg, SIGMASK_SIZE);
    break;
  }
  case KVM_REGISTER_COALESCED_MMIO:
  {
    memcpy(&g_kvm_coalesced_mmio_zone, arg, sizeof(g_kvm_coalesced_mmio_zone));
    break;
  }
  case KVM_TPR_ACCESS_REPORTING:
  {
    memcpy(&g_kvm_tpr_access_ctl, arg, sizeof(g_kvm_tpr_access_ctl));
    break;
  }
  case KVM_SET_VCPU_EVENTS:
  {
    if (init) {
      memcpy(&g_kvm_vcpu_events, arg, sizeof(g_kvm_vcpu_events));
      init = 0;
    }
    break;
  }
  case KVM_IRQ_LINE_STATUS:
  {
    // memcpy(&g_kvm_irq_level, arg, sizeof(g_kvm_irq_level));
    // DPRINTF("Setting IRQ Line: %u, Level: %u\n", g_kvm_irq_level.irq,
    // g_kvm_irq_level.level);
    break;
  }
  case KVM_SET_MP_STATE:
  {
    DPRINTF("******************************* Setting MP State to: %s\n",
            get_mp_state_string(*(struct kvm_mp_state *)arg));
    break;
  }
  case KVM_GET_MP_STATE:
  {
    DPRINTF("******************************* Kernel's MP State is: %s\n",
            get_mp_state_string(*(struct kvm_mp_state *)arg));
    break;
  }
  case KVM_SET_LAPIC:
  {
    if (init2) {
      memcpy(&g_kvm_lapic, arg, sizeof(g_kvm_lapic));
      init2 = 0;
    }
    break;
  }
  case KVM_GET_LAPIC:
  {
    DPRINTF("############################### Kernel's LAPIC State\n");
    break;
  }
#if 0

  /* NOTE: We are skipping this... see note for KVM_GET_MSRS above. */
  case KVM_GET_MSR_INDEX_LIST:
  {
    /* The first call is to query the number of MSRs */
    if (p_first_get_msr_index_list_call) {
      DPRINTF("Saving MSR Index List (1/2). arg: %p", arg);
      p_first_get_msr_index_list_call = 0;
    } else {
      /* The subsequent call gives us the actual MSR list */
      DPRINTF("Saving MSR Index List (2/2). arg: %p", arg);
    }
    break;
  }
#endif /* if 0 */
  case KVM_CREATE_PIT2:
  {
    if (fd == g_vm_fd) {
      DPRINTF("Saving PIT2 config. arg: %p\n", arg);
      memcpy(&g_kvm_pit2_config, arg, sizeof(g_kvm_pit2_config));
    }
    break;
  }
  case KVM_SET_IDENTITY_MAP_ADDR:
  {
    DPRINTF("Saving identity map address. arg: %p\n", arg);

    /* Kernel does a copy_from_user() of sizeof(uint64_t) bytes for this addr */
    memcpy(&g_kvm_id_map_addr, arg, sizeof(g_kvm_id_map_addr));
    DPRINTF("IOCTL returned %u\n", result);
    break;
  }
  case KVM_SET_TSS_ADDR:
  {
    DPRINTF("Saving tss address. arg: %p\n", arg);

    /* Kernel uses this value as an (unsigned int) directly */
    g_kvm_tss_addr = arg;
    DPRINTF("IOCTL returned %u\n", result);
    break;
  }
  case KVM_SET_USER_MEMORY_REGION:
  {
    if (fd == g_vm_fd) {
      struct kvm_userspace_memory_region mem;

      /* We don't handle more than MAX_MEM_REGIONS memory regions;
       * will need to make it dynamic to handle all possible cases. */
      DPRINTF("Saving user memory region #%d\n", g_num_of_memory_regions);

      // memcpy(&mem, arg, sizeof(struct kvm_userspace_memory_region));
      process_memory_region(arg);
    }
    break;
  }
  case KVM_CREATE_IRQCHIP:
  {
    DPRINTF("Saving irqchip address. arg: %p\n", arg);

    /* This is not relevant; might as well skip this. See the kernel code
     * (in arch/x86/kvm/x86.c) for more details. */
    g_kvm_irqchip_arg = arg;
    DPRINTF("IOCTL returned %u\n", result);
    break;
  }
  case KVM_SET_VAPIC_ADDR:
  {
    DPRINTF("Saving virtual APIC address. arg: %p\n", arg);

    /* Kernel does a copy_from_user() of sizeof(struct kvm_vapic_addr)
     * bytes. See the code in arch/x86/kvm/x86.c for more details. */
    memcpy(&g_kvm_vapic_addr, arg, sizeof(g_kvm_vapic_addr));
    DPRINTF("IOCTL returned %u\n", result);
    break;
  }
  case KVM_SET_GSI_ROUTING:
  {
    DPRINTF("Saving GSI routing table. Routing table address: %p, "
            "count: %d\n", arg, ((struct kvm_irq_routing *)arg)->nr);

    /* NOTE:
     *  a) QEMU uses address of a global struct to store the GSI routing
     *      table. We use the hack below to save us from copying over the
     *      data again.
     *  b) This struct is re-alloced to twice its previous size within QEMU
     *      each time it reaches the limit.
     *  c) Kernel looks at the number of entries (routing_table.nr), and
     *      does a copy_from_user() of routing_table.nr * sizeof(entry)
     *      bytes.
     *  d) As a result, we get away with a single ioctl(KVM_SET_GSI_ROUTING)
     *      call at the time of restart.
     * IMPORTANT: If (a), or (c) breaks, this code will be invalid.
     */
    g_kvm_gsi_routing_table = arg;
    break;
  }
  }

  return result;
}

/*============================================================================*/
/*=========================== END WRAPPER FUNCTIONS ==========================*/
/*============================================================================*/

static int dummy = 1;
static int (*next_fnc)() = NULL; /* Same type signature as ioctl */

static void
pre_ckpt()
{
  int r;

  DPRINTF("\n*** Before checkpointing. ***\n");
  if (dummy == 1) {
    r = save_registers();
    if (r < 0) {
      DPRINTF("ERROR: Querying CPU state from the kernel returned: %d\n", r);
      DPRINTF("WARNING: Please try checkpointing again\n");
    }
    r = save_pit2();
    if (r < 0) {
      DPRINTF("ERROR: Querying PIT state from the kernel returned: %d\n", r);
      DPRINTF("WARNING: Please try checkpointing again\n");
    }
    r = save_irqchip();
    if (r < 0) {
      DPRINTF("ERROR: Querying IRQCHIP state from the kernel returned: %d\n",
              r);
      DPRINTF("WARNING: Please try checkpointing again\n");
    }

    // dummy = 2;
  } else if (dummy >= 2) {
    r = restore_registers();
    if (r < 0) {
      DPRINTF("ERROR: Restoring the registers returned: %d\n", r);
      DPRINTF("WARNING: Cannot continue\n");
      exit(-1);
    }
  }
}

static void
restart()
{
  int r;

  DPRINTF("Restarting from checkpoint.\n");
  if (g_kvm_fd > 0 && g_vm_fd > 0 && g_vcpu_fd > 0) {
    r = create_vm();
    if (r < 0) {
      DPRINTF("ERROR: Creating VMFD returned: %d\n", r);
      DPRINTF("WARNING: Please try checkpointing again\n");
      exit(-1);
    } else if (r > 0) {
      int i = 0;
      int t = 0;

      r = restore_id_map_addr();
      if (r < 0) {
        DPRINTF("ERROR: Restoring identity map addr returned: %d\n", r);
        DPRINTF("WARNING: Please try checkpointing again\n");
        exit(-1);
      }
      r = restore_tss_addr();
      if (r < 0) {
        DPRINTF("ERROR: Restoring tss addr returned: %d\n", r);
        DPRINTF("WARNING: Please try checkpointing again\n");
        exit(-1);
      }
      r = create_irqchip();
      if (r < 0) {
        DPRINTF("ERROR: Creating IRQCHIP returned: %d\n", r);
        DPRINTF("WARNING: Please try checkpointing again\n");
        exit(-1);
      }

      r = create_vcpu();
      if (r < 0) {
        DPRINTF("ERROR: Creating new VCPU returned: %d\n", r);
        DPRINTF("WARNING: Cannot continue\n");
        exit(-1);
      }
      if (NEXT_FNC(mmap)(g_vcpu_mmap_addr, g_vcpu_mmap_length,
                         g_vcpu_mmap_prot, g_vcpu_mmap_flags | MAP_FIXED,
                         g_vcpu_fd, 0) == MAP_FAILED) {
        DPRINTF("ERROR: Mapping the new VCPU returned MAP_FAILED\n");
        DPRINTF("WARNING: Cannot continue\n");
        exit(-1);
      }

      r = NEXT_FNC(ioctl)(g_vcpu_fd, KVM_SET_SIGNAL_MASK, &g_kvm_sigmask);
      if (r < 0) {
        DPRINTF("ERROR: Setting VCPU Signal Mask returned: %d\n", r);
        exit(-1);
      }

      r = NEXT_FNC(ioctl)(g_vm_fd, KVM_IRQ_LINE_STATUS, &g_kvm_irq_level);
      if (r < 0) {
        DPRINTF("ERROR: Setting IRQ LINE status returned: %d\n", r);
        exit(-1);
      }

      r = NEXT_FNC(ioctl)(g_vm_fd, KVM_REGISTER_COALESCED_MMIO,
                          &g_kvm_coalesced_mmio_zone);
      if (r < 0) {
        DPRINTF("ERROR: Setting Coalesced MMIO Zone returned: %d\n", r);
        exit(-1);
      }

      DPRINTF("Setting #%d memory regions\n", g_num_of_memory_regions);
      struct kvm_userspace_memory_region *mem;
      for (i = 0; i < g_num_of_memory_regions; i++) {
        mem = &g_kvm_mem_region[i];
        DPRINTF("slot:%X, flags:%X, start:%llX, size:%llX, ram:%llX)\n",
                mem->slot, mem->flags, mem->guest_phys_addr,
                mem->memory_size, mem->userspace_addr);
        r = NEXT_FNC(ioctl)(g_vm_fd, KVM_SET_USER_MEMORY_REGION,
                            &g_kvm_mem_region[i]);
        if (r < 0) {
          DPRINTF("ERROR: Creating memory region #%d returned: \n", i, r);
          perror("ioctl(KVM_SET_USER_MEMORY_REGION)");
        }
      }

      /* See note in the ioctl() wrapper. */
      DPRINTF("Setting routing tables. ptr: %p...\n",
              g_kvm_gsi_routing_table);
      r = NEXT_FNC(ioctl)(g_vm_fd, KVM_SET_GSI_ROUTING,
                          g_kvm_gsi_routing_table);
      if (r < 0) {
        DPRINTF("ERROR: Setting routing table (#routes=%d) returned: "
                "%d\n", g_kvm_gsi_routing_table->nr, r);
      }

      r = create_pit2();
      if (r < 0) {
        DPRINTF("Creating PIT2 returned: %d\n", r);
      }
      r = restore_pit2();
      if (r < 0) {
        DPRINTF("ERROR: Restoring PIT2 returned: %d\n", r);
        DPRINTF("WARNING: Cannot continue\n");
        exit(-1);
      }
      int array[] = { 0, 1, 4, 8, 12 };
      g_kvm_irq_level.level = 0;
      for (i = 0; i < 5; i++) {
        g_kvm_irq_level.irq = array[i];
        r = NEXT_FNC(ioctl)(g_vm_fd, KVM_IRQ_LINE_STATUS, &g_kvm_irq_level);
        if (r < 0) {
          DPRINTF("ERROR: Resetting IRQ#%d LINE returned: %d\n",
                  g_kvm_irq_level.irq,
                  r);
          exit(-1);
        }
      }
      r = restore_irqchip();
      if (r < 0) {
        DPRINTF("ERROR: Restoring IRQCHIP returned: %d\n", r);
        DPRINTF("WARNING: Cannot continue\n");
        exit(-1);
      }
      r = NEXT_FNC(ioctl)(g_vcpu_fd, KVM_TPR_ACCESS_REPORTING,
                          &g_kvm_tpr_access_ctl);
      if (r < 0) {
        DPRINTF("ERROR: Restoring the tpr access reporting returned: %d\n", r);
        DPRINTF("WARNING: Cannot continue\n");
        exit(-1);
      }
      r = NEXT_FNC(ioctl)(g_vcpu_fd, KVM_SET_VAPIC_ADDR,
                          &g_kvm_vapic_addr);
      if (r < 0) {
        DPRINTF("ERROR: Restoring the vapic addr returned: %d\n", r);
        DPRINTF("WARNING: Cannot continue\n");
        exit(-1);
      }
      r = restore_registers();
      if (r < 0) {
        DPRINTF("ERROR: Restoring the registers returned: %d\n", r);
        DPRINTF("WARNING: Cannot continue\n");
        exit(-1);
      }
    }
  }
}

static void
kvm_event_hook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  /* NOTE:  See warning in plugin/README about calls to printf here. */
  switch (event) {
  case DMTCP_EVENT_INIT:
  {
    DPRINTF("The plugin containing %s has been initialized.\n", __FILE__);
    break;
  }

  case DMTCP_EVENT_EXIT:
    DPRINTF("The plugin is being called before exiting.\n");
    break;

  case DMTCP_EVENT_PRECHECKPOINT:
    pre_ckpt();
    break;

  case DMTCP_EVENT_RESTART:
    restart();
    break;

  default:
    break;
  }
}

DmtcpPluginDescriptor_t kvm_plugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "kvm",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "KVM plugin",
  kvm_event_hook
};

DMTCP_DECL_PLUGIN(kvm_plugin);
