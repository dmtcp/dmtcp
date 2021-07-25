/*
  Example code for Bcast (not used here):
   https://mpitutorial.com/tutorials/mpi-broadcast-and-collective-communication/
   https://github.com/mpitutorial/mpitutorial/blob/gh-pages/tutorials/mpi-broadcast-and-collective-communication/code/my_bcast.c
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <mpi.h>
#include <math.h>
#include <assert.h>
/************************************************************
This is a simple Ibarrier program in MPI
OPTIONS:   iterations, MPI_TEST, MPI_WAIT, Move the 'sleep(1)'
************************************************************/

#define MPI_TEST
#ifndef MPI_TEST
# define MPI_WAIT
#endif

int main(argc,argv)
int argc;
char *argv[];
{
    int i,iter,myid, numprocs;
    int iterations;
    MPI_Status status;
    MPI_Request request = MPI_REQUEST_NULL; 

    MPI_Init(&argc,&argv);
    MPI_Comm_size(MPI_COMM_WORLD,&numprocs);
    MPI_Comm_rank(MPI_COMM_WORLD,&myid);
    iterations = 100000;
    for (iter = 0; iter < iterations; iter++) {
        MPI_Ibarrier(MPI_COMM_WORLD, &request);
        // printf("[Rank = %d]\n", myid);
        // fflush(stdout);
        // sleep(1); // The checkpoint is likely to occur here.
#ifdef MPI_TEST
        while (1) {
          int flag = 0;
          MPI_Test(&request, &flag, &status);
          if (flag) { break; }
        }
#endif
#ifdef MPI_WAIT
        MPI_Wait(&request, &status);
#endif
    }
    MPI_Finalize();
}
