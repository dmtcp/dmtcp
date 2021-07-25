! Tested on Cori with:  ftn -c THIS_FILE.f90
! C binding: https://gcc.gnu.org/onlinedocs/gfortran/Interoperable-Subroutines-and-Functions.html
! MPICH: integer(c_int),bind(C, name=" MPIR_F08_MPI_IN_PLACE ") & ::  MPI_IN_PLACE
! FIXME: Make this cleaner, either use bind() command, or use an out parameter.


      subroutine get_fortran_constants()
        implicit none
        include 'mpif.h'

        ! explicit interfaces
        interface
          subroutine get_fortran_constants_helper(t)
            implicit none
            integer,intent(in) :: t
          end subroutine get_fortran_constants_helper
        end interface
        call get_fortran_constants_helper(MPI_IN_PLACE)
      end subroutine get_fortran_constants
