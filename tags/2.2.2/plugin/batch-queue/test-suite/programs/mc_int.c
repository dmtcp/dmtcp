#include <mpi.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <unistd.h> 

double f1(double x);
double f2(double x);
double f3(double x);
double f4(double x);
double f5(double x);
double f6(double x);
double f7(double x);
double f8(double x);
double f9(double x);
double f10(double x);
double f11(double x);
double f12(double x);
double f13(double x);
double f14(double x);
double f15(double x);
double f16(double x);
double f17(double x);
double f18(double x);
double f19(double x);
double f20(double x);
double f21(double x);
double f22(double x);
double f23(double x);
double f24(double x);
double f25(double x);
double f26(double x);
double f27(double x);
double f28(double x);
double f29(double x);
double f30(double x);
double f31(double x);
double f32(double x);
double f33(double x);
double f34(double x);
double f35(double x);
double f36(double x);
double f37(double x);
double f38(double x);
double f39(double x);
double f40(double x);
double f41(double x);
double f42(double x);
double f43(double x);
double f44(double x);
double f45(double x);
double f46(double x);
double f47(double x);
double f48(double x);
double f49(double x);
double f50(double x);

double urand(void);

/* prefix all output with hostname to aid in veriyfing checkpoint/restart operation */
char * getMyHostName(char * hname, int maxLen);

#define nFuncs (50)
#define MAX_THREADS (32)

int main(int argc, char ** argv)
{
  int pid, np;
  MPI_Init(&argc, &argv);

  MPI_Comm_rank(MPI_COMM_WORLD, &pid);
  MPI_Comm_size(MPI_COMM_WORLD, &np);  
  
  setbuf(stdout, NULL);
  char hname[1000];


  if(pid == 0)
    system("echo \"Start time is: `date`\""); 

  if( argc < 3 ){
    printf("%s Usage:\n%s <nsamples> <seed>\n",argv[0],argv[0]);
    exit(0);
  }

  int nSamples = atoi(argv[1]);

  double a[] = {0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, -2.00, -1.00,
                      1.00, 1.00, 1.00, 1.00, 1.00, 1.00, 1.00, 1.00, -1.00,  0.00,
                      2.00, 2.00, 2.00, 2.00, 2.00, 2.00, 2.00, 2.00,  0.00,  1.00,
                      3.00, 3.00, 3.00, 3.00, 3.00, 3.00, 3.00, 3.00,  1.00,  2.00,
                      4.00, 4.00, 4.00, 4.00, 4.00, 4.00, 4.00, 4.00,  2.00,  3.00};

  double b[] = {3.14, 4.00, 1.00, 1.00, 1.00, 3.00,  4.00, 1.00, 1.00, 1.00,
                      4.14, 5.00, 2.00, 6.00, 2.00, 6.00,  5.00, 3.00, 2.00, 3.00,
                      5.14, 6.00, 3.00, 7.00, 3.00, 9.00,  6.00, 4.00, 3.00, 5.00,
                      6.14, 7.00, 4.00, 8.00, 4.00, 10.00, 7.00, 5.00, 5.00, 7.00,
                      7.14, 8.00, 5.00, 9.00, 5.00, 12.00, 8.00, 7.00, 7.00, 9.00}; 

  int seed = atoi(argv[2])+pid;

  double sum, mci[nFuncs];
  int i,j, tid;

  double (*f[nFuncs])(double)={f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, 
			       f11, f12, f13, f14, f15, f16, f17, f18, 
			       f19, f20, f21, f22, f23, f24, f25, f26,
			       f27, f28, f29, f30, f31, f32, f33, f34,
			       f35, f36, f37, f38, f39, f40, f41, f42,
			       f43, f44, f45, f46, f47, f48, f49, f50};

  srand(seed);

  if(pid == 0)
  {
    printf("%s : Monte Carlo Integration\n", getMyHostName(hname, 1000) );
    printf("%s : Random number seed = %d\n", getMyHostName(hname, 1000), seed);
    printf("%s : nSamples = %d\n", getMyHostName(hname, 1000), nSamples);
    printf("%s : nProcs = %d\n", getMyHostName(hname, 1000), np);
  }

  for(j = 0; j < nFuncs; j++)
  {
    sum = 0.00;
 
    for(i = pid; i < nSamples; i+=np)
    {
      sum += (*f[j])(a[j] + urand()*(b[j] - a[j]));
    }   
    MPI_Reduce(&sum, &(mci[j]), 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
    if(pid == 0)
    {
      mci[j] = ((b[j]-a[j])*(mci[j]))/(double)nSamples;
      printf("%s : Integral of f%d over %E to %E is %E\n", getMyHostName(hname, 1000), j+1, a[j], b[j], mci[j]);
    }
  }  

/*
  if(pid == 0)
  {
    int i = 1;
    while(i){
	sleep(1);
    }
  }
*/    

  MPI_Finalize();
  return 0;
}

double urand(void)
{
  return ((double)rand()/(double)RAND_MAX);
}

char * getMyHostName(char * hname, int maxLen)
{
  gethostname(hname, maxLen);
  return hname;
}

/* these examples are from http://math.fullerton.edu/mathews/n2003/montecarlomod.html */
double f1(double x){ return (sin(x)+(1.00/3.00)*sin(3.00*x)); }
double f2(double x){ return sqrt(x); }
double f3(double x){ return (4.00/(1.00 + (x*x)));}
double f4(double x){ return (sqrt(x + sqrt(x))); }

/* more examples from calculus textbook */
double f5(double x){ return (x-4.00); }
double f6(double x){ return (3.00*x-1.00); }
double f7(double x){ return (2.00*x*x); }
double f8(double x){ return (x*x+1); }
double f9(double x){ return (3.00*x*x-x); }
double f10(double x){ return (2.00*x*x+x); }
double f11(double x){ return (2.00); }
double f12(double x){ return (pow(x,7.00)); }
double f13(double x){ return (1.00/(pow(x, 7.00))); }
double f14(double x){ return (pow(x, (1.00/3.00))); }
double f15(double x){ return (pow(x*x+1, 2.00)); }
double f16(double x){ return ((x*x-12.00)/(x*x*x*x)); }
double f17(double x){ return (pow(x*x,1.00/5.00)+1.00); }
double f18(double x){ return (x+fabs(x)); }
double f19(double x){ return (sqrt(1.00+pow(x,4.00))); }
double f20(double x){ return ((pow(x*x-1.00,5.00))*x); }
double f21(double x){ return (sin(x)*cos(x)*cos(x)); }
double f22(double x){ return ((x-5.00)*sqrt(x+1.00)); }
double f23(double x){ return (sin(sqrt(x)/sqrt(x))); }
double f24(double x){ return (1.00/(sqrt(x)*pow(1.00+sqrt(x),4.00))); }
double f25(double x){ return ((x*x+5.00)*(x*x*x+15*x-3.00)/sqrt(fabs(194-pow(x*x*x+15*x-3,2.00)))); }

/* repeat f1 through f25 (limits will be different */
double f26(double x){ return f1(x); }
double f27(double x){ return f2(x); }
double f28(double x){ return f3(x); }
double f29(double x){ return f4(x); }
double f30(double x){ return f5(x); }
double f31(double x){ return f6(x); }
double f32(double x){ return f7(x); }
double f33(double x){ return f8(x); }
double f34(double x){ return f9(x); }
double f35(double x){ return f10(x); }
double f36(double x){ return f11(x); }
double f37(double x){ return f12(x); }
double f38(double x){ return f13(x); }
double f39(double x){ return f14(x); }
double f40(double x){ return f15(x); }
double f41(double x){ return f16(x); }
double f42(double x){ return f17(x); }
double f43(double x){ return f18(x); }
double f44(double x){ return f19(x); }
double f45(double x){ return f20(x); }
double f46(double x){ return f21(x); }
double f47(double x){ return f22(x); }
double f48(double x){ return f23(x); }
double f49(double x){ return f24(x); }
double f50(double x){ return f25(x); }
