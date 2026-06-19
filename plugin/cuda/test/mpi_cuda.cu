// This is a hybrid MPI and CUDA example: Distributed Vector Addition.
// To compile: nvcc -o mpi_cuda_vector_add mpi_cuda_vector_add.cu -I/path/to/mpi/include -L/path/to/mpi/lib -lmpi
// To run (e.g., with 4 processes): mpirun -np 4 ./mpi_cuda_vector_add

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <mpi.h>

// --- CUDA Kernel Function ---
// Performs vector addition on the GPU.
__global__ void vectorAdd(float *a, float *b, float *c, int n) {
    // Determine the global index for this thread
    int i = blockIdx.x * blockDim.x + threadIdx.x;

    if (i < n) {
        c[i] = a[i] + b[i];
    }
}

// --- Host Code (MPI and CUDA Management) ---
int main(int argc, char **argv) {
    // Total size of the vector
    const int N_TOTAL = 1024 * 1024;
    const int BLOCK_SIZE = 256;

    int rank, size;
    int iteration = 0;

    // 1. Initialize MPI
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    // Repeat main loop for testing
    while (1) {
      iteration++;
      printf("Iteration %d\n", iteration);
      // Ensure N_TOTAL is divisible by the number of MPI processes
      if (N_TOTAL % size != 0) {
        if (rank == 0) {
          fprintf(stderr, "Error: N_TOTAL (%d) must be divisible by the number of MPI processes (%d).\n", N_TOTAL, size);
        }
        MPI_Finalize();
        return 1;
      }

      // Determine the size of the local vector chunk for this process
      int local_n = N_TOTAL / size;
      size_t local_bytes = local_n * sizeof(float);

      // --- Host Memory Allocation for Local Chunks (CPU side) ---
      float *h_a_local, *h_b_local, *h_c_local;
      h_a_local = (float*)malloc(local_bytes);
      h_b_local = (float*)malloc(local_bytes);
      h_c_local = (float*)malloc(local_bytes);

      // Global memory pointers (only used by rank 0 for total vector/result storage)
      float *h_a_total = NULL;
      float *h_b_total = NULL;
      float *h_c_total = NULL;

      if (rank == 0) {
        // Allocate and initialize the full vectors on the root process (rank 0)
        size_t total_bytes = N_TOTAL * sizeof(float);
        h_a_total = (float*)malloc(total_bytes);
        h_b_total = (float*)malloc(total_bytes);
        h_c_total = (float*)malloc(total_bytes);

        // Initialize total vectors A and B
        for (int i = 0; i < N_TOTAL; i++) {
          h_a_total[i] = (float)i;
          h_b_total[i] = (float)(i * 2);
        }

        printf("Total Vector Size N = %d, running with P = %d processes.\n", N_TOTAL, size);
        printf("Each process handles a chunk of size %d.\n", local_n);
      }

      // 2. MPI Scatter: Distribute the data from rank 0 to all processes
      // Each process receives its 'local_n' portion into h_a_local and h_b_local
      MPI_Scatter(h_a_total, local_n, MPI_FLOAT, h_a_local, local_n, MPI_FLOAT, 0, MPI_COMM_WORLD);
      MPI_Scatter(h_b_total, local_n, MPI_FLOAT, h_b_local, local_n, MPI_FLOAT, 0, MPI_COMM_WORLD);

      // Set the GPU device for this MPI process (optional but good practice for multi-GPU setups)
#if 0
      int num_device = 0;
      cudaGetDeviceCount(&num_device);
      cudaSetDevice(rank % num_device);
#endif

      // --- Device Memory Allocation (GPU side) ---
      float *d_a, *d_b, *d_c;
      cudaMalloc((void**)&d_a, local_bytes);
      cudaMalloc((void**)&d_b, local_bytes);
      cudaMalloc((void**)&d_c, local_bytes);

      // 3. CUDA Data Transfer: Copy local data from CPU to GPU
      cudaMemcpy(d_a, h_a_local, local_bytes, cudaMemcpyHostToDevice);
      cudaMemcpy(d_b, h_b_local, local_bytes, cudaMemcpyHostToDevice);

      // 4. CUDA Kernel Launch: Execute the vector addition on the GPU
      int num_blocks = (local_n + BLOCK_SIZE - 1) / BLOCK_SIZE;
      vectorAdd<<<num_blocks, BLOCK_SIZE>>>(d_a, d_b, d_c, local_n);

      // Synchronize to ensure the kernel completes
      cudaDeviceSynchronize();

      // 5. CUDA Data Transfer: Copy results back from GPU to CPU
      cudaMemcpy(h_c_local, d_c, local_bytes, cudaMemcpyDeviceToHost);

      // 6. MPI Gather: Collect the local results from all processes back to rank 0
      MPI_Gather(h_c_local, local_n, MPI_FLOAT, h_c_total, local_n, MPI_FLOAT, 0, MPI_COMM_WORLD);

      // 7. Verification (Only done by Rank 0)
      if (rank == 0) {
        printf("Verification check...\n");
        int errors = 0;
        for (int i = 0; i < N_TOTAL; i++) {
          // Check if C[i] == A[i] + B[i] (i + 2*i = 3*i)
          if (h_c_total[i] != (float)(i * 3)) {
            fprintf(stderr, "Error at index %d: Expected %f, got %f\n", i, (float)(i * 3), h_c_total[i]);
            errors++;
          }
        }

        if (errors == 0) {
          printf("Successfully verified the distributed vector addition!\n");
        } else {
          fprintf(stderr, "Verification FAILED with %d errors.\n", errors);
        }

        // Free total host memory
        free(h_a_total);
        free(h_b_total);
        free(h_c_total);
      }

      // 8. Cleanup and Finalization
      // Free local host memory
      free(h_a_local);
      free(h_b_local);
      free(h_c_local);

      // Free device memory
      cudaFree(d_a);
      cudaFree(d_b);
      cudaFree(d_c);
      sleep(1);
    }
    MPI_Finalize();
    return 0;
}

