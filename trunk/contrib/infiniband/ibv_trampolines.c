#include "debug.h"
#include <string.h>
#include <assert.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include "ibvctx.h"
#include <infiniband/verbs.h>
#include "ibv_trampolines.h"
#include "dmtcpplugin.h"

// Defines void _dmtcp_setup_trampolines()
//   (used in constructor in dmtcpworker.cpp)

#ifdef __x86_64__
static char asm_jump[] = {
  // mov    $0x1234567812345678,%rax
  0x48, 0xb8, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12,
  // jmpq   *%rax
  0xff, 0xe0
};
// Beginning of address in asm_jump:
# define ADDR_OFFSET 2
#else
static char asm_jump[] = {
  0xb8, 0x78, 0x56, 0x34, 0x12, // mov    $0x12345678,%eax
  0xff, 0xe0                    // jmp    *%eax
};
// Beginning of address in asm_jump:
# define ADDR_OFFSET 1
#endif

// Make sure that trampolines are only set up once
// otherwise original function address may be lost
#define TRAMP_NUM 5
static int first_time[TRAMP_NUM] = {1,1,1,1,1};

#define ASM_JUMP_LEN sizeof(asm_jump)
#define INSTALL_IBV_TRAMPOLINE(name) \
  memcpy(name##_addr, name##_trampoline_jump, ASM_JUMP_LEN)
#define UNINSTALL_IBV_TRAMPOLINE(name) \
  memcpy(name##_addr, name##_displaced_instructions, ASM_JUMP_LEN)
#define SETUP_IBV_TRAMPOLINE(func, addr)                                \
  do {                                                                  \
    long pagesize = sysconf(_SC_PAGESIZE);                              \
    long page_base;                                                     \
    /************ Find libc func and set up permissions. **********/    \
    /* We assume that no one is wrapping func yet. */                   \
    func##_addr = addr;                                                 \
    /* Base address of page where func resides. */                      \
    page_base = (long)func##_addr - ((long)func##_addr % pagesize);     \
    /* Give that whole page RWX permissions. */                         \
    int retval = mprotect((void *)page_base, pagesize,                  \
        PROT_READ | PROT_WRITE | PROT_EXEC);                            \
    assert ( retval != -1 );                                 \
    /************ Set up trampoline injection code. ***********/        \
    /* Trick to get "free" conversion of a long value to the            \
       character-array representation of that value. Different sizes of \
       long and endian-ness are handled automatically. */               \
    union u {                                                           \
      long val;                                                         \
      char bytes[sizeof(long)];                                         \
    } data;                                                             \
    data.val = (long)&func##_trampoline;                                \
    memcpy(func##_trampoline_jump, asm_jump, ASM_JUMP_LEN);              \
    /* Insert real trampoline address into injection code. */           \
    memcpy(func##_trampoline_jump+ADDR_OFFSET, data.bytes, sizeof(long)); \
    /* Save displaced instructions for later restoration. */            \
    memcpy(func##_displaced_instructions, func##_addr, ASM_JUMP_LEN);   \
    /* Inject trampoline. */                                            \
    INSTALL_IBV_TRAMPOLINE(func);                                           \
  } while (0)

#define DECLARE_IBV_TRAMPOLINE(func)                                     \
static char func##_trampoline_jump[ASM_JUMP_LEN];                     \
static char func##_displaced_instructions[ASM_JUMP_LEN];              \
static void * func##_addr = NULL                                     

DECLARE_IBV_TRAMPOLINE(ibv_post_recv);
DECLARE_IBV_TRAMPOLINE(ibv_post_srq_recv);
DECLARE_IBV_TRAMPOLINE(ibv_post_send);
DECLARE_IBV_TRAMPOLINE(ibv_poll_cq);
DECLARE_IBV_TRAMPOLINE(ibv_req_notify_cq);

/* Since ibv_post_recv is a static inline function that dispatches *
 * to a function pointer, we want to wrap the address of that function. */
static int ibv_post_recv_passthru(struct ibv_qp *qp,
                                  struct ibv_recv_wr *wr, struct ibv_recv_wr **bad_wr)
{
//    JNOTE("I'M IN POST_RECV TRAMPOLINE");

  return _post_recv(qp, wr, bad_wr);
}

/* Calls to ibv_post_recv will land here. */
static int ibv_post_recv_trampoline(struct ibv_qp *qp,
                                    struct ibv_recv_wr *wr, struct ibv_recv_wr **bad_wr)
{
//  PDEBUG("WRAPPER for ibv_post_recv.\n");
  dmtcp_plugin_disable_ckpt();
//  WRAPPER_EXECUTION_DISABLE_CKPT();
  /* Unpatch ibv_func. */
  UNINSTALL_IBV_TRAMPOLINE(ibv_post_recv);
  int retval = ibv_post_recv_passthru(qp, wr, bad_wr);
  /* Repatch ibv_func. */
  INSTALL_IBV_TRAMPOLINE(ibv_post_recv);
//  WRAPPER_EXECUTION_ENABLE_CKPT();
  dmtcp_plugin_enable_ckpt();
  return retval;
}

static int ibv_post_srq_recv_passthru(struct ibv_srq *srq,
                                  struct ibv_recv_wr *wr, struct ibv_recv_wr **bad_wr)
{
  return _post_srq_recv(srq, wr, bad_wr);
}

static int ibv_post_srq_recv_trampoline(struct ibv_srq *srq,
                                    struct ibv_recv_wr *wr, struct ibv_recv_wr **bad_wr)
{
//  PDEBUG("WRAPPER for ibv_srq_post_recv.\n");
  dmtcp_plugin_disable_ckpt();
//  WRAPPER_EXECUTION_DISABLE_CKPT();
  /* Unpatch ibv_func. */
  UNINSTALL_IBV_TRAMPOLINE(ibv_post_srq_recv);
  int retval = ibv_post_srq_recv_passthru(srq, wr, bad_wr);
  /* Repatch ibv_func. */
  INSTALL_IBV_TRAMPOLINE(ibv_post_srq_recv);
//  WRAPPER_EXECUTION_ENABLE_CKPT();
  dmtcp_plugin_enable_ckpt();
  return retval;
}

static int ibv_req_notify_cq_trampoline(struct ibv_cq * cq, int solicited_only)
{
  dmtcp_plugin_disable_ckpt();
  UNINSTALL_IBV_TRAMPOLINE(ibv_req_notify_cq);
  int rslt = _req_notify_cq(cq, solicited_only);
  INSTALL_IBV_TRAMPOLINE(ibv_req_notify_cq);
  dmtcp_plugin_enable_ckpt();

  return rslt;
}

static int ibv_post_send_passthru(struct ibv_qp *qp,
                                  struct ibv_send_wr *wr, struct ibv_send_wr **bad_wr)
{
//    JNOTE("I'M IN POST_SEND TRAMPOLINE");

  return _post_send(qp, wr, bad_wr);
}

/* Calls to ibv_post_recv will land here. */
static int ibv_post_send_trampoline(struct ibv_qp *qp,
                                    struct ibv_send_wr *wr, struct ibv_send_wr **bad_wr)
{
//  PDEBUG("WRAPPER for ibv_post_send.\n");
  dmtcp_plugin_disable_ckpt();
//  WRAPPER_EXECUTION_DISABLE_CKPT();
  /* Unpatch ibv_func. */
  UNINSTALL_IBV_TRAMPOLINE(ibv_post_send);
  int retval = ibv_post_send_passthru(qp, wr, bad_wr);
  /* Repatch ibv_func. */
  INSTALL_IBV_TRAMPOLINE(ibv_post_send);
//  WRAPPER_EXECUTION_ENABLE_CKPT();
  dmtcp_plugin_enable_ckpt();
  return retval;
}

static int ibv_poll_cq_passthru(struct ibv_cq *cq,
                                int num_entries, struct ibv_wc *wc)
{
//    JNOTE("I'M IN POLL_CQ TRAMPOLINE");

  return _poll_cq(cq, num_entries, wc);
}

/* Calls to ibv_post_recv will land here. */
static int ibv_poll_cq_trampoline(struct ibv_cq *cq,
                                  int num_entries, struct ibv_wc *wc)
{
//  PDEBUG("WRAPPER for ibv_poll_cq.\n");
  dmtcp_plugin_disable_ckpt();
//  WRAPPER_EXECUTION_DISABLE_CKPT();
  /* Unpatch ibv_func. */
  UNINSTALL_IBV_TRAMPOLINE(ibv_poll_cq);
  int retval = ibv_poll_cq_passthru(cq, num_entries, wc);
  /* Repatch ibv_func. */
  INSTALL_IBV_TRAMPOLINE(ibv_poll_cq);
//  WRAPPER_EXECUTION_ENABLE_CKPT();
  dmtcp_plugin_enable_ckpt();
  return retval;
}

/* Any trampolines which should be installed are done so via this function. */
void _dmtcp_setup_ibv_trampolines(int (*post_recv_ptr)(struct ibv_qp *,
                                  struct ibv_recv_wr *, struct ibv_recv_wr **),
				  int (*post_srq_recv_ptr)(struct ibv_srq *,
				  struct ibv_recv_wr *, struct ibv_recv_wr **),
                                  int (*post_send_ptr)(struct ibv_qp *,
                                      struct ibv_send_wr *, struct ibv_send_wr **),
                                  int (*poll_cq_ptr)(struct ibv_cq *, int, struct ibv_wc *),
                                  int (*req_notify_ptr)(struct ibv_cq *, int))

{
  if (first_time[0]) {
    first_time[0] = 0;
    SETUP_IBV_TRAMPOLINE(ibv_post_recv, (void *) post_recv_ptr);
  }
  if (first_time[1]) {
    first_time[1] = 0;
    SETUP_IBV_TRAMPOLINE(ibv_post_srq_recv, (void *) post_srq_recv_ptr);
  }
  if (first_time[2]) {
    first_time[2] = 0;
    SETUP_IBV_TRAMPOLINE(ibv_post_send, (void *) post_send_ptr);
  }
  if (first_time[3]) {
    first_time[3] = 0;
    SETUP_IBV_TRAMPOLINE(ibv_poll_cq, (void *) poll_cq_ptr);
  }
  if (first_time[4]) {
    first_time[4] = 0;
    SETUP_IBV_TRAMPOLINE(ibv_req_notify_cq, (void *) req_notify_ptr);
  }
}

void _uninstall_req_notify_cq_trampoline(void)
{
  UNINSTALL_IBV_TRAMPOLINE(ibv_req_notify_cq);
}

void _install_req_notify_cq_trampoline(void)
{
  INSTALL_IBV_TRAMPOLINE(ibv_req_notify_cq);
}

void _uninstall_poll_cq_trampoline(void)
{
  UNINSTALL_IBV_TRAMPOLINE(ibv_poll_cq);
}

void _install_poll_cq_trampoline(void)
{
  INSTALL_IBV_TRAMPOLINE(ibv_poll_cq);
}
void _uninstall_post_recv_trampoline(void)
{
  UNINSTALL_IBV_TRAMPOLINE(ibv_post_recv);
}

void _install_post_recv_trampoline(void)
{
  INSTALL_IBV_TRAMPOLINE(ibv_post_recv);
}

void _uninstall_post_srq_recv_trampoline(void)
{
  UNINSTALL_IBV_TRAMPOLINE(ibv_post_srq_recv);
}

void _install_post_srq_recv_trampoline(void)
{
  INSTALL_IBV_TRAMPOLINE(ibv_post_srq_recv);
}
void _uninstall_post_send_trampoline(void)
{
  UNINSTALL_IBV_TRAMPOLINE(ibv_post_send);
}

void _install_post_send_trampoline(void)
{
  INSTALL_IBV_TRAMPOLINE(ibv_post_send);
}
