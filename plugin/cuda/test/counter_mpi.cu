#include <stdio.h>
#include <unistd.h>
#include <mpi.h>

__device__ int counter = 0;
__global__ void increment() {
    counter++;
}

int main(int argc, char **argv) {
  MPI_Init(&argc, &argv);
  while (true) {
    int host_counter = 0;

    increment<<<1,1>>>();
    cudaMemcpyFromSymbol(&host_counter, counter, sizeof counter);
    printf("%d...\n", host_counter);
    sleep(1);
  }
  MPI_Finalize();
  return 0;
}
