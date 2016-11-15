#include <stdio.h>
#include <stdlib.h>
#include "mpi.h"
#include "pmi.h"

int
get_kvs_name(char **kvs_name, int *kvs_name_l)
{
  int ret = PMI_KVS_Get_name_length_max(kvs_name_l);

  if (ret) {
    return ret;
  }
  *kvs_name = malloc(*kvs_name_l);
  return PMI_KVS_Get_my_name(*kvs_name, *kvs_name_l);
}

int
main(int argc, char *argv[])
{
  int rank;
  int size;
  int i = 1;
  char *kvs_name;
  int kvs_name_l;

  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  PMI_BOOL initialized;
  int ret = PMI_Initialized(&initialized);
  printf("%d: ret=%d, init=%d\n", rank, ret, initialized);
  if (initialized) {
    get_kvs_name(&kvs_name, &kvs_name_l);
    printf("%d: kvs_name = %s\n", kvs_name);
  }

  MPI_Finalize();
  return 0;
}
