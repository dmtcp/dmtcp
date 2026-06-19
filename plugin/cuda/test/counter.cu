#include <stdio.h>
#include <unistd.h>

__device__ int counter = 0;
__global__ void increment() {
    counter++;
}

int main(int argc, char **argv) {
  while (true) {
    int host_counter = 0;

    increment<<<1,1>>>();
    cudaMemcpyFromSymbol(&host_counter, counter, sizeof counter);
    printf("%d...\n", host_counter);
    sleep(1);
  }
  return 0;
}
