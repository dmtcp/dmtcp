#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <fstream>
#include "pmi_hijack_log.h"

static void *handle = NULL;
static pmi_hijack_log *log;
static int count = 0;

char fprefix[256] = "pmidb";

void
init_handle()
{
  if (!handle) {
    handle =
      dlopen("/user/artempol/OpenMPI/pmi_support/lib/libpmi.so", RTLD_NOW);
  }
  printf("handle=%p\n", handle);
  if (!log) {
    log = new pmi_hijack_log;
  }
}

int
init_fprefix()
{
  char *jid = getenv("SLURM_JOBID");
  char *nid = getenv("SLURM_NODEID");
  char *lid = getenv("SLURM_LOCALID");

  sprintf(fprefix, "%s.%s.%s.%s", fprefix, jid, nid, lid);
}

__attribute__((constructor)) void
initLib(void)
{
  char *p = getenv("LD_PRELOAD");

  printf("library loaded. LD_PRELOAD=%s\n", p);
  init_fprefix();
}

extern "C" {
int
PMI_Init(int *length)
{
  typedef int (*_PMI_Init_t)(int *t);
  static _PMI_Init_t _real = NULL;
  if (!_real) {
    init_handle();
    _real = (_PMI_Init_t)dlsym(handle, "PMI_Init");
  }
  int ret = _real(length);
  printf("MYPMI_Init: %p. ret = %d\n", _real, ret);
  return ret;
}

int
PMI_Initialized(int *length)
{
  typedef int (*_PMI_Initialized_t)(int *t);
  static _PMI_Initialized_t _real = NULL;
  if (!_real) {
    init_handle();
    _real = (_PMI_Initialized_t)dlsym(handle, "PMI_Initialized");
  }
  int ret = _real(length);
  return ret;
}

int
PMI_Finalize(void)
{
  typedef int (*_PMI_Finalize_t)();
  static _PMI_Finalize_t _real = NULL;
  typedef int (*_PMI_KVS_Get_name_length_max_t)(int *t);
  static _PMI_KVS_Get_name_length_max_t _kvsname_max = NULL;
  typedef int (*_PMI_KVS_Get_key_length_max_t)(int *t);
  static _PMI_KVS_Get_key_length_max_t _key_max = NULL;
  typedef int (*_PMI_KVS_Get_value_length_max_t)(int *t);
  static _PMI_KVS_Get_value_length_max_t _val_max = NULL;

  if (!_real) {
    init_handle();
    _real = (_PMI_Finalize_t)dlsym(handle, "PMI_Finalize");
    _kvsname_max =
      (_PMI_KVS_Get_name_length_max_t) dlsym(handle,
                                             "PMI_KVS_Get_name_length_max");
    _key_max =
      (_PMI_KVS_Get_key_length_max_t)dlsym(handle,
                                           "PMI_KVS_Get_key_length_max");
    _val_max =
      (_PMI_KVS_Get_value_length_max_t)dlsym(handle,
                                             "PMI_KVS_Get_value_length_max");
  }

  /*
  std::fstream out;
  out.open("pmi_base.ckpt", std::ios_base::out);
  int kvsname_max, key_max, val_max;
  _kvsname_max( &kvsname_max );
  _key_max( &key_max );
  _val_max( &val_max );
  log->serialize(out, kvsname_max, key_max, val_max);
  */
  int ret = _real();
  printf("MYPMI_Finalize: %p. ret = %d\n", _real, ret);
  return ret;
}

int
PMI_KVS_Put(const char kvsname[], const char key[], const char value[])
{
  typedef int (*_PMI_KVS_Put_t)(const char kvsname[], const char key[],
                                const char value[]);
  static _PMI_KVS_Put_t _real = NULL;
  if (!_real) {
    init_handle();
    _real = (_PMI_KVS_Put_t)dlsym(handle, "PMI_KVS_Put");
  }
  int ret = _real(kvsname, key, value);
  ret = ret * !(log->kvs_put(kvsname, key, value));
  return ret;
}

int
PMI_KVS_Commit(char kvsname[], int length)
{
  typedef int (*_PMI_KVS_Commit_t)(char kvsname[]);
  static _PMI_KVS_Commit_t _real = NULL;
  if (!_real) {
    init_handle();
    _real = (_PMI_KVS_Commit_t)dlsym(handle, "PMI_KVS_Commit");
  }
  int size, rc;
  log->kvs_commit(kvsname);
  return _real(kvsname);
}

int
PMI_KVS_Create(char kvsname[], int length)
{
  typedef int (*_PMI_KVS_Create_t)(char kvsname[], int length);
  static _PMI_KVS_Create_t _real = NULL;
  if (!_real) {
    init_handle();
    _real = (_PMI_KVS_Create_t)dlsym(handle, "PMI_KVS_Create");
  }
  int ret;
  ret = _real(kvsname, length);
  log->kvs_create(kvsname);
  return ret;
}

int
PMI_KVS_Destroy(char kvsname[])
{
  typedef int (*_PMI_KVS_Destroy_t)(char kvsname[]);
  static _PMI_KVS_Destroy_t _real = NULL;
  if (!_real) {
    init_handle();
    _real = (_PMI_KVS_Destroy_t)dlsym(handle, "PMI_Destroy");
  }
  log->kvs_create(kvsname);
  return _real(kvsname);
}

int
PMI_Barrier(void)
{
  typedef int (*_PMI_Barrier_t)(void);
  static _PMI_Barrier_t _real = NULL;
  typedef int (*_PMI_KVS_Get_name_length_max_t)(int *t);
  static _PMI_KVS_Get_name_length_max_t _kvsname_max = NULL;
  typedef int (*_PMI_KVS_Get_key_length_max_t)(int *t);
  static _PMI_KVS_Get_key_length_max_t _key_max = NULL;
  typedef int (*_PMI_KVS_Get_value_length_max_t)(int *t);
  static _PMI_KVS_Get_value_length_max_t _val_max = NULL;


  if (!_real) {
    init_handle();
    _real = (_PMI_Barrier_t)dlsym(handle, "PMI_Barrier");

    _kvsname_max =
      (_PMI_KVS_Get_name_length_max_t)dlsym(handle,
                                            "PMI_KVS_Get_name_length_max");
    _key_max =
      (_PMI_KVS_Get_key_length_max_t)dlsym(handle,
                                           "PMI_KVS_Get_key_length_max");
    _val_max =
      (_PMI_KVS_Get_value_length_max_t)dlsym(handle,
                                             "PMI_KVS_Get_value_length_max");
  }

  std::fstream out;
  out.open(fprefix, std::ios_base::out);
  int kvsname_max, key_max, val_max;
  _kvsname_max(&kvsname_max);
  _key_max(&key_max);
  _val_max(&val_max);
  log->serialize(out, kvsname_max, key_max, val_max);
  out.close();

  return _real();
}
}
