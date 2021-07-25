#include <stdio.h>

void *FORTRAN_MPI_IN_PLACE = NULL;
void get_fortran_constants_();

void get_fortran_constants_helper_(int *t) {
  FORTRAN_MPI_IN_PLACE = t;
}


void get_fortran_constants() {
  if (FORTRAN_MPI_IN_PLACE == NULL) {
    get_fortran_constants_();
  }
}

#ifdef STANDALONE
int main() {
  get_fortran_constants();
  printf("Fortran MPI_IN_PLACE = %p\n", FORTRAN_MPI_IN_PLACE);
  return 0;
}
#endif

