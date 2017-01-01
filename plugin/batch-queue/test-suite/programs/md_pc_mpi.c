#include <mpi.h>
#include <stdio.h>
#include <math.h>

#define NCX 15
#define NCY 15
#define NCZ 15
#define N  (4 * NCX * NCY * NCZ)

#define NDIM 3
#define PI 3.14159265358979

#define A1 3.949846138
#define A3 0.252408784
#define A5 0.076542912
#define A7 0.008355968
#define A9 0.029899776

#define L 1029
#define C 221591
#define M 1048576

#define GEAR0  (19.0 / 120.0)
#define GEAR1  (3.0 / 4.0)
#define GEAR3  (1.0 / 2.0)
#define GEAR4  (1.0 / 12.0)

#define NBUFF (6 * N)

void gisum(int *aaa, int nnn, int *bbb)
{
 int i;
 MPI_Allreduce(aaa, bbb, nnn, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
 for(i = 0; i < nnn; i++) *(aaa + i) = *(bbb + i);
}

void gdsum(double *aaa, int nnn, double *bbb)
{
 int i;
 MPI_Allreduce(aaa, bbb, nnn, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
 for(i = 0; i < nnn; i++) *(aaa + i) = *(bbb + i);
}

#define Merge_tag 6001
void merge(int myid, int numprocs, int natms, double *xxx, double *yyy, double *zzz, double *buffer)
{
 MPI_Status status;
 MPI_Request request;
 int nsize;
 int i, j, k;
 int iatm1, iatm2;
 int jid, kid;
 int katm1, katm2;

 nsize = (N + numprocs - 1) / numprocs;
// if(nbuff < 6 * nsize) printf("error: nbuff < 6*nsize\n");

 j = 0;

 iatm1 = (myid * N) / numprocs;
 iatm2 = ( (myid + 1) * N) / numprocs - 1;

 for(i = iatm1; i <= iatm2; i++)
 {
  *(buffer + j + 0) = *(xxx + i);
  *(buffer + j + 1) = *(yyy + i);
  *(buffer + j + 2) = *(zzz + i);
  j = j + 3;
 }

 MPI_Barrier(MPI_COMM_WORLD);

 jid = (myid + 1) % numprocs;

 for(k = 1; k <= numprocs - 1; k++)
 {
  kid = (myid + numprocs - k) % numprocs;

  katm1 = (kid * N) / numprocs;
  katm2 = ((kid + 1) * N) / numprocs - 1;

  MPI_Irecv(buffer + 3 * nsize, 3 * nsize, MPI_DOUBLE,\
            MPI_ANY_SOURCE, Merge_tag + k, MPI_COMM_WORLD, &request);
  MPI_Send(buffer, 3 * nsize, MPI_DOUBLE, jid,\
           Merge_tag + k, MPI_COMM_WORLD);
  MPI_Wait(&request, &status);

  j = 3 * nsize;

  for(i = katm1; i <= katm2; i++)
  {
   *(xxx + i) = *(buffer + j + 0);
   *(yyy + i) = *(buffer + j + 1);
   *(zzz + i) = *(buffer + j + 2);
   j = j + 3;
  }
  
  for(i = 0; i <= 3 * nsize - 1; i++)
  {
   *(buffer + i) = *(buffer + 3 * nsize + i);
  }
 } 

}
#undef Merge_tag

void restart(int step, double cellx, double celly, double cellz,\
double rx[N], double ry[N], double rz[N],\
double rx1[N], double ry1[N], double rz1[N],\
double rx2[N], double ry2[N], double rz2[N],\
double rx3[N], double ry3[N], double rz3[N],\
double rx4[N], double ry4[N], double rz4[N])
{
 int i;
 FILE *fp1;

 fp1 = fopen("restart", "w+");

 fprintf(fp1, "%10d %12.6lf %12.6lf %12.6lf %10d\n", N, cellx, celly, cellz, step);

 for(i = 0; i < N; i++)
 {
  fprintf(fp1, "%12.6lf %12.6lf %12.6lf\n", rx[i], ry[i], rz[i]);
  fprintf(fp1, "%12.6lf %12.6lf %12.6lf\n", rx1[i], ry1[i], rz1[i]);
  fprintf(fp1, "%12.6lf %12.6lf %12.6lf\n", rx2[i], ry2[i], rz2[i]);
  fprintf(fp1, "%12.6lf %12.6lf %12.6lf\n", rx3[i], ry3[i], rz3[i]);
  fprintf(fp1, "%12.6lf %12.6lf %12.6lf\n", rx4[i], ry4[i], rz4[i]);
 }

 fclose(fp1);

}

void correct(double dt,\
double rx[N], double ry[N], double rz[N],\
double rx1[N], double ry1[N], double rz1[N],\
double rx2[N], double ry2[N], double rz2[N],\
double rx3[N], double ry3[N], double rz3[N],\
double rx4[N], double ry4[N], double rz4[N],\
double fx[N], double fy[N], double fz[N],\
int myid, int numprocs)
{
 int i;
 double axi, ayi, azi;
 double corrx, corry, corrz;
 double c1, c2, c3, c4;
 double cr, cr1, cr3, cr4;
 int iatm1, iatm2;

 iatm1 = (myid * N) / numprocs;
 iatm2 = ( (myid + 1) * N) / numprocs - 1;

 c1 = dt;
 c2 = c1 * dt / 2.0;
 c3 = c2 * dt / 3.0;
 c4 = c3 * dt / 4.0;

 cr = GEAR0 * c2;
 cr1 = GEAR1 * c2 / c1;
 cr3 = GEAR3 * c2 / c3;
 cr4 = GEAR4 * c2 / c4;

 
 for(i = iatm1; i <= iatm2; i++)
 {
  axi = fx[i];
  ayi = fy[i];
  azi = fz[i];
  corrx = axi - rx2[i];
  corry = ayi - ry2[i];
  corrz = azi - rz2[i];

  rx[i] = rx[i] + cr * corrx;
  ry[i] = ry[i] + cr * corry;
  rz[i] = rz[i] + cr * corrz;
  rx1[i] = rx1[i] + cr1* corrx;
  ry1[i] = ry1[i] + cr1* corry;
  rz1[i] = rz1[i] + cr1* corrz;
  rx2[i] = axi;
  ry2[i] = ayi;
  rz2[i] = azi;
  rx3[i] = rx3[i] + cr3 * corrx;
  ry3[i] = ry3[i] + cr3 * corry;
  rz3[i] = rz3[i] + cr3 * corrz;
  rx4[i] = rx4[i] + cr4 * corrx;
  ry4[i] = ry4[i] + cr4 * corry;
  rz4[i] = rz4[i] + cr4 * corrz;  
 } 
}

void pbc(double *r, double box)
{
 if( *r > 0.0)
  while(*r / box >= 0.5) *r = *r - box;
 else
  while(*r / box <= -0.5) *r = *r + box;
}

void force(double cellx, double celly, double cellz,\
double rcut, double *v, double *vc, double *w,\
double rx[N], double ry[N], double rz[N],\
double fx[N], double fy[N], double fz[N],\
int myid, int numprocs)
{
 int i, j, ncut;
 double rcutsq;
 double rxij, ryij, rzij, fxij, fyij, fzij;
 double rijsq, sr2, sr6, sr12, vij, wij, fij;

 int last, mpm2, npm2;
 int m;
 double buffer[6];

 last = N;
 mpm2 = N / 2;
 npm2 = (N - 1) / 2;


 rcutsq = rcut * rcut;
 
 ncut = 0;
 *v = 0.0; 
 *w = 0.0;

 for(m = 1; m <= mpm2; m++)
 {
  if(m > npm2) last = mpm2;

  for(i = myid; i < last; i = i + numprocs) 
  {
   j = i + m;
   if(j >= N) j = j - N;

   rxij= rx[i] - rx[j]; ryij = ry[i] - ry[j]; rzij = rz[i] - rz[j];   

   pbc(&rxij, cellx); pbc(&ryij, celly); pbc(&rzij, cellz);

   rijsq = rxij * rxij + ryij * ryij + rzij * rzij;

   if(rijsq < rcutsq)
   {
    sr2 = 1.0 / rijsq;
    sr6 = sr2 * sr2 *sr2;
    sr12 = sr6 * sr6;

    vij = sr12 - sr6;
    *v = *v + vij;

    wij = vij + sr12;
    *w = *w + wij;

    fij = wij * sr2;

    fxij = fij * rxij;
    fyij = fij * ryij;
    fzij = fij * rzij;

    fx[i] = fx[i] + fxij;   
    fy[i] = fy[i] + fyij;   
    fz[i] = fz[i] + fzij;   

    fx[j] = fx[j] - fxij;
    fy[j] = fy[j] - fyij;
    fz[j] = fz[j] - fzij;

    ncut = ncut + 1;
   }
  }
 }

 sr2 = 1.0 / rcutsq;
 sr6 = sr2 * sr2 * sr2;
 sr12 = sr6 * sr6;
 vij = sr12 - sr6;
 *vc = *v - (double)ncut * vij;
 
 for(i = 0; i < N; i++)
 {
  fx[i] = fx[i] * 24.0;
  fy[i] = fy[i] * 24.0;
  fz[i] = fz[i] * 24.0;
 }

 *v = *v * 4.0;
 *vc = *vc * 4.0;
 *w = *w * 24.0 / 3.0;
 buffer[0] = *v;
 buffer[1] = *vc;
 buffer[2] = *w;
 gdsum(buffer, 3, buffer + 3);
 *v = buffer[0];
 *vc = buffer[1];
 *w = buffer[2];
}

void predict(double dt,\
double rx[N], double ry[N], double rz[N],\
double rx1[N], double ry1[N], double rz1[N],\
double rx2[N], double ry2[N], double rz2[N],\
double rx3[N], double ry3[N], double rz3[N],\
double rx4[N], double ry4[N], double rz4[N],
int myid, int numprocs)
{
 int i;
 double c1, c2, c3, c4;
 int iatm1, iatm2;
 double buffer[NBUFF];

 iatm1 = (myid * N) / numprocs;
 iatm2 = ( (myid + 1) * N) / numprocs - 1;

 c1 = dt;
 c2 = c1 * dt / 2.0;
 c3 = c2 * dt / 3.0;
 c4 = c3 * dt / 4.0;

 for(i = iatm1; i <= iatm2; i++)
 {
  rx[i] = rx[i] + c1 * rx1[i] + c2 * rx2[i] + c3 * rx3[i] + c4 * rx4[i];
  ry[i] = ry[i] + c1 * ry1[i] + c2 * ry2[i] + c3 * ry3[i] + c4 * ry4[i];
  rz[i] = rz[i] + c1 * rz1[i] + c2 * rz2[i] + c3 * rz3[i] + c4 * rz4[i];
  rx1[i] = rx1[i] + c1 * rx2[i] + c2 * rx3[i] + c3 * rx4[i];
  ry1[i] = ry1[i] + c1 * ry2[i] + c2 * ry3[i] + c3 * ry4[i];
  rz1[i] = rz1[i] + c1 * rz2[i] + c2 * rz3[i] + c3 * rz4[i];
  rx2[i] = rx2[i] + c1 * rx3[i] + c2 * rx4[i];
  ry2[i] = ry2[i] + c1 * ry3[i] + c2 * ry4[i];
  rz2[i] = rz2[i] + c1 * rz3[i] + c2 * rz4[i];
  rx3[i] = rx3[i] + c1 * rx4[i];
  ry3[i] = ry3[i] + c1 * ry4[i];
  rz3[i] = rz3[i] + c1 * rz4[i];
 }
 merge(myid, numprocs, N, rx, ry, rz, buffer);  
}


void lrc(double rcut, double dens, double *vlrc, double *wlrc)
{
 double sr3, sr9;

 sr3 = (1.0 / rcut) * (1.0 / rcut) * (1.0 / rcut);
 sr9 = sr3 * sr3 * sr3;
 *vlrc = (8.0 / 9.0) * PI * dens * (double)N * (sr9 - 3.0 * sr3);
 *wlrc = (16.0 / 9.0) * PI * dens * (double)N * (2.0 * sr9 - 3.0 * sr3);
}

void traj(int step, FILE *fp1,\
double rx[N], double ry[N], double rz[N])
{
 int i;

 fprintf(fp1, "%10d\n", N);
 fprintf(fp1, "%10d\n", step);
 for(i = 0; i < N; i++)
  fprintf(fp1, "O  %12.6lf %12.6lf %12.6lf\n", rx[i], ry[i], rz[i]);
}

void print_config(FILE *fp1, double cellx, double celly, double cellz,\
 double rx[N], double ry[N], double rz[N], double rx1[N], double ry1[N], double rz1[N])
{
 int i;
 fprintf(fp1, "box length cellx celly cellz = %12.6lf %12.6lf %12.6lf\n", cellx, celly, cellz);
 fprintf(fp1, "index rx ry rz\n");
 for(i = 0; i < N; i = i + 10) fprintf(fp1, "%10d %12.6lf %12.6lf %12.6lf\n", i + 1, rx[i], ry[i], rz[i]);
 fprintf(fp1, "index vx vy vz\n");
 for(i = 0; i < N; i = i + 10) fprintf(fp1, "%10d %12.6lf %12.6lf %12.6lf\n", i + 1, rx1[i], ry1[i], rz1[i]); 
}

double kinetic(double vx[N], double vy[N], double vz[N],\
               int myid, int numprocs)
{
 int i;
 double k;
 int iatm1, iatm2;
 double buffer;

 iatm1 = (myid * N) / numprocs;
 iatm2 = ( (myid + 1) * N) / numprocs - 1;

 k = 0.0;

 for(i = iatm1; i <= iatm2; i++)
 {
  k = k + vx[i] * vx[i] + vy[i] * vy[i] + vz[i] * vz[i];
 }
 gdsum(&k, 1, &buffer);
 
 k = 0.5 * k;
 
 return k;
}

void v_scale(double temper, double *temp,\
             double vx[N], double vy[N], double vz[N],\
             int myid, int numprocs)
{
 int i;
 double factor;
 double k, kn;
 int iatm1, iatm2;

 iatm1 = (myid * N) / numprocs;
 iatm2 = ( (myid + 1) * N) / numprocs - 1;

 factor = sqrt(temper / (*temp));

 for(i = iatm1; i <= iatm2; i++)
 {
  vx[i] = vx[i] * factor;
  vy[i] = vy[i] * factor;
  vz[i] = vz[i] * factor;
 }

 k = kinetic(vx, vy, vz, myid, numprocs);
 kn = k / (double)N;
 *temp = kn * 2.0 / NDIM;
}

void zero_momentum(double vx[N], double vy[N], double vz[N],\
                   int myid, int numprocs)
{
 int i;
 double sumx, sumy, sumz;
 int iatm1, iatm2;
 double bufferd[6];

 iatm1 = (myid * N) / numprocs;
 iatm2 = ( (myid + 1) * N) / numprocs - 1;
 
 sumx = sumy = sumz = 0.0;

 for(i = iatm1; i <= iatm2; i++)
 {
  sumx = sumx + vx[i];
  sumy = sumy + vy[i];
  sumz = sumz + vz[i];
 }
 bufferd[0] = sumx; bufferd[1] = sumy; bufferd[2] = sumz;
 gdsum(bufferd, 3, bufferd + 3);
 sumx = bufferd[0]; sumy = bufferd[1]; sumz = bufferd[2];

 for(i = iatm1; i <= iatm2; i++)
 {
  vx[i] = vx[i] - sumx / (double)N;
  vy[i] = vy[i] - sumy / (double)N;
  vz[i] = vz[i] - sumz / (double)N;
 }
}

double ranf()
{

 static int seed = 0;
 
 seed = (seed * L + C) % M;
 return (double)seed / M;
}

double gauss()
{
 double sum, r, r2;
 int i;
 
 sum = 0.0;
 for(i = 0; i < 12; i++) sum = sum + ranf();
 r = (sum - 6.0) / 4.0;
 r2 = r * r;
 return ( ( ( (A9 * r2 + A7) * r2 + A5) * r2 + A3) * r2 + A1) * r; 
}

void ini_vel(double temp, double vx[N], double vy[N], double vz[N],\
             int myid, int numprocs)
{
 double rtemp;
 int i;
 double k, kn, temp1;

 rtemp = sqrt(temp);

 for(i = 0; i < N; i++)
 {
  vx[i] = rtemp * gauss();
  vy[i] = rtemp * gauss();
  vz[i] = rtemp * gauss();
 }

 zero_momentum(vx, vy, vz, myid, numprocs);

 k = kinetic(vx, vy, vz, myid, numprocs);
 kn = k / (double)N;
 temp1 = 2.0 * kn / NDIM;

 v_scale(temp, &temp1, vx, vy, vz, myid, numprocs); 

}

void r_unscale(double cellx, double celly, double cellz, double rx[N], double ry[N], double rz[N],\
               int myid, int numprocs)
{
 int i;
 int iatm1, iatm2;

 iatm1 = (myid * N) / numprocs;
 iatm2 = ( (myid + 1) * N) / numprocs - 1;

 for(i = iatm1; i <=iatm2; i++)
 {
  rx[i] = rx[i] * cellx;
  ry[i] = ry[i] * celly;
  rz[i] = rz[i] * cellz;
 }
}

void move_to_com(double rx[N], double ry[N], double rz[N],\
                 int myid, int numprocs)
{
 int i;
 double sumx = 0.0, sumy = 0.0, sumz = 0.0;
 double bufferd[6];
 int iatm1, iatm2;

 iatm1 = (myid * N) / numprocs;
 iatm2 = ( (myid + 1) * N) / numprocs - 1;

 for(i = iatm1; i <= iatm2; i++)
 {
  sumx = sumx + rx[i];
  sumy = sumy + ry[i];
  sumz = sumz + rz[i];
 }
 bufferd[0] = sumx; bufferd[1] = sumy; bufferd[2] = sumz;
 gdsum(bufferd, 3, bufferd + 3);
 sumx = bufferd[0]; sumy = bufferd[1]; sumz = bufferd[2];
 
 for(i = iatm1; i <= iatm2; i++)
 {
  rx[i] = rx[i] - sumx / (double)N;
  ry[i] = ry[i] - sumy / (double)N;
  rz[i] = rz[i] - sumz / (double)N;
 }
} 

void fcc(double rx[N], double ry[N], double rz[N],
int myid, int numprocs)
{

 int ix, iy, iz, iref, m;

 rx[0] = 0.0, ry[0] = 0.0, rz[0] = 0.0;
 rx[1] = 0.5, ry[1] = 0.5, rz[1] = 0.0;
 rx[2] = 0.0, ry[2] = 0.5, rz[2] = 0.5;
 rx[3] = 0.5, ry[3] = 0.0, rz[3] = 0.5;

 m = 0;

 for(iz = 0; iz < NCZ; iz++)
 {
  for(iy = 0; iy < NCY; iy++)
  {
   for(ix = 0; ix < NCX; ix++)
   {
    for(iref = 0; iref < 4; iref++)
    {
     rx[iref + m] = rx[iref] + (double)ix;
     ry[iref + m] = ry[iref] + (double)iy;
     rz[iref + m] = rz[iref] + (double)iz;
    }
    m = m + 4;
   }
  }
 }
 move_to_com(rx, ry, rz, myid, numprocs);
}

void ini_r_v_cell(double temp, double dens,\
double *cellx, double *celly, double *cellz,\
double rx[N], double ry[N], double rz[N],\
double vx[N], double vy[N], double vz[N],\
int myid, int numprocs)
{
 double vol, unit_cell_len;
 double buffer[NBUFF];
 
 vol = (double)N / dens; 
 unit_cell_len = pow(vol / (double)(NCX * NCY * NCZ), 1.0 / 3.0);
 *cellx = unit_cell_len * (double)NCX;
 *celly = unit_cell_len * (double)NCY;
 *cellz = unit_cell_len * (double)NCZ;
 
 fcc(rx, ry, rz, myid, numprocs);

 r_unscale(unit_cell_len, unit_cell_len, unit_cell_len, rx, ry, rz, myid, numprocs);

 ini_vel(temp, vx, vy, vz, myid, numprocs);

 merge(myid, numprocs, N, rx, ry, rz, buffer);
 merge(myid, numprocs, N, vx, vy, vz, buffer);

}

void print_control(FILE *fp1, int nstep, int estep, int iprint, int iscale, int itraj,\
 int irestart, double rcut, double dt, double temp0, double dens)
{
 fprintf(fp1, "NVE MD for LJ using 5-value Gear predict-correct integrator\n");
 fprintf(fp1, "CONTROL PARAMETERS:\n");
 fprintf(fp1, "total number of steps = %10d\n", nstep);
 fprintf(fp1, "total number of equilibration steps = %10d\n", estep);
 fprintf(fp1, "interval for printing various energy information = %10d\n", iprint);
 fprintf(fp1, "interval for velocity rescaling during equilibration = %10d\n", iscale);
 fprintf(fp1, "interval for storing trajectory file = %10d\n", itraj);
 fprintf(fp1, "interval for writing restart file = %10d\n", irestart);
 fprintf(fp1, "potential cutoff = %12.6lf\n", rcut);
 fprintf(fp1, "timestep = %12.6lf\n", dt);
 fprintf(fp1, "initial temperature = %12.6lf\n", temp0);
 fprintf(fp1, "number density = %12.6lf\n", dens);
}

void read_control(int *nstep, int *estep, int *iprint, int *iscale, int *itraj, int *irestart,\
double *rcut, double *dt, double *temp0, double *dens)
{
 FILE *fp1;

 fp1 = fopen("control","r+");

 fscanf(fp1, "%d", nstep); while(fgetc(fp1) != '\n');
 fscanf(fp1, "%d", estep); while(fgetc(fp1) != '\n');
 fscanf(fp1, "%d", iprint); while(fgetc(fp1) != '\n');
 fscanf(fp1, "%d", iscale); while(fgetc(fp1) != '\n');
 fscanf(fp1, "%d", itraj); while(fgetc(fp1) != '\n');
 fscanf(fp1, "%d", irestart); while(fgetc(fp1) != '\n');

 fscanf(fp1, "%lf", rcut); while(fgetc(fp1) != '\n');
 fscanf(fp1, "%lf", dt); while(fgetc(fp1) != '\n');
 fscanf(fp1, "%lf", temp0); while(fgetc(fp1) != '\n');
 fscanf(fp1, "%lf", dens); while(fgetc(fp1) != '\n');

 fclose(fp1);
}


int main(int argc, char *argv[])
{
 int nstep, estep, iprint, iscale, itraj, irestart;
 double rcut, dt, temp0, dens;

 double rx[N], ry[N], rz[N];
 double rx1[N], ry1[N], rz1[N];
 double rx2[N], ry2[N], rz2[N];
 double rx3[N], ry3[N], rz3[N];
 double rx4[N], ry4[N], rz4[N];
 double fx[N], fy[N], fz[N];

 int i, step;
 double cellx, celly, cellz;
 double vol, pres, norm, temp, freedom;
 double k, v, vc, w, e, ec;
 double kn, vn, en, ecn;
 double vlrc, wlrc;

 double acv, ack, ace, acec, acp, act;
 double avv, avk, ave, avec, avp, avt;
 double acvsq, acksq, acesq, acecsq, acpsq, actsq;
 double flv, flk, fle, flec, flp, flt;

 FILE *fp1, *fp2;

 int  myid, numprocs;
 double startwtime = 0.0, endwtime;
 int buffer1[12];
 double buffer2[8];
 int j;
 double buffer[NBUFF]; 

 MPI_Init( &argc, &argv );
 MPI_Barrier( MPI_COMM_WORLD );

 MPI_Comm_rank( MPI_COMM_WORLD, &myid );
 MPI_Comm_size( MPI_COMM_WORLD, &numprocs );
 
 nstep = estep = iprint = iscale = itraj = irestart = 0;
 rcut = dt = temp0 = dens = 0.0;
 if( myid == 0 )
 {
  startwtime = MPI_Wtime();
  fp1 = fopen("output", "w+");
  fp2 = fopen("history.xyz", "w+");
  fprintf(fp1, "running on %5d nodes\n", numprocs);
  read_control(&nstep, &estep, &iprint, &iscale, &itraj, &irestart, &rcut, &dt, &temp0, &dens);
  print_control(fp1, nstep, estep, iprint, iscale, itraj, irestart, rcut, dt, temp0, dens);
 }
 buffer1[0] = nstep; buffer1[1] = estep; buffer1[2] = iprint;
 buffer1[3] = iscale; buffer1[4] = itraj; buffer1[5] = irestart;
 gisum(buffer1, 6, buffer1 + 6);
 nstep = buffer1[0]; estep = buffer1[1]; iprint = buffer1[2];
 iscale = buffer1[3]; itraj = buffer1[4]; irestart = buffer1[5];

 buffer2[0] = rcut; buffer2[1] = dt; buffer2[2] = temp0; buffer2[3] = dens; 
 gdsum(buffer2, 4, buffer2 + 4);
 rcut = buffer2[0]; dt = buffer2[1]; temp0 = buffer2[2]; dens = buffer2[3]; 

 ini_r_v_cell(temp0, dens, &cellx, &celly, &cellz, rx, ry, rz, rx1, ry1, rz1, myid, numprocs);

 if(myid == 0) print_config(fp1, cellx, celly, cellz, rx, ry, rz, rx1, ry1, rz1);

 for(i = 0; i < N; i++)
 {
  rx2[i] = ry2[i] = rz2[i] = 0.0;
  rx3[i] = ry3[i] = rz3[i] = 0.0;
  rx4[i] = ry4[i] = rz4[i] = 0.0;
 }

 freedom = NDIM * (double)N;
 if(myid == 0) fprintf(fp1,"total number of degrees of freedom = %12.6lf\n", freedom);

 k = kinetic(rx1, ry1, rz1, myid, numprocs);
 temp0 = k * 2.0 / freedom;
 if(myid == 0) fprintf(fp1, "initial temperature = %12.6lf\n", temp0);

 vol = cellx * celly * cellz;
 if(myid == 0) fprintf(fp1, "system volume = %12.6lf\n", vol);

 dens = (double)N / vol;
 if(myid == 0) fprintf(fp1, "number density = %12.6lf\n", dens);

 step = 0; 
 if(myid == 0) traj(step, fp2, rx, ry, rz);

 lrc(rcut, dens, &vlrc, &wlrc); 

 acv = ack = ace = acec = acp = act = 0.0;
 acvsq = acksq = acesq = acecsq = acpsq = actsq = 0.0;
 flv = flk = fle = flec = flp = flt = 0.0;

 if(myid == 0) fprintf(fp1,"**** start of dynamics ****\n");
 if(myid == 0) fprintf(fp1,"step energy cutenergy kinetic potential temperature pressure\n");

 for(step = 1; step <= nstep; step++)
 {
  predict(dt, rx, ry, rz, rx1, ry1, rz1, rx2, ry2, rz2, rx3, ry3, rz3, rx4, ry4, rz4, myid, numprocs);

  for(i = 0; i < N; i++)
  {        
   fx[i] = 0.0; fy[i] = 0.0; fz[i] = 0.0;
  }     

  force(cellx, celly, cellz, rcut, &v, &vc, &w, rx, ry, rz, fx, fy, fz, myid, numprocs);
   
  j = 0;
  for(i = 0; i < N; i++)
  {
   buffer[j + 0] = fx[i];
   buffer[j + 1] = fy[i];
   buffer[j + 2] = fz[i];
   j = j + 3;
  }
  gdsum(buffer, 3 * N, buffer + 3 * N);
  j = 0;
  for(i = 0; i < N; i++)
  {      
   fx[i] = buffer[j + 0];
   fy[i] = buffer[j + 1];
   fz[i] = buffer[j + 2];
   j = j + 3;
  }

  correct(dt, rx, ry, rz, rx1, ry1, rz1, rx2, ry2, rz2, rx3, ry3, rz3, rx4, ry4, rz4, fx, fy, fz, myid, numprocs);

  v = v + vlrc;
  w = w + wlrc;

  k = kinetic(rx1, ry1, rz1, myid, numprocs);

  e = k + v;
  ec = k + vc;

  kn = k / (double)N; en = e / (double)N; vn = v / (double)N; ecn = ec / (double)N;

  temp = k * 2.0 / freedom;
  if((step <= estep) && (step % iscale == 0))
   v_scale(temp0, &temp, rx1, ry1, rz1, myid, numprocs);

  pres = dens * temp + w / vol;

  if(step > estep)
  {
   ace = ace + en; acec = acec + ecn; ack = ack + kn; acv= acv + vn; acp = acp + pres;
   acesq = acesq + en * en; acecsq = acecsq + ecn * ecn; acksq = acksq + kn * kn; acvsq = acvsq + vn * vn; acpsq = acpsq + pres * pres;
  }  

  if(step % iprint == 0 && myid == 0) 
   fprintf(fp1, "%10d %12.6lf %12.6lf %12.6lf %12.6lf %12.6lf %12.6lf\n", step, en, ecn, kn, vn, temp, pres);
  
  if(step % itraj == 0)
  {
   merge(myid, numprocs, N, rx, ry, rz, buffer); 
   if(myid == 0) traj(step, fp2, rx, ry, rz);
  }

  if(step % irestart == 0)
  {
   merge(myid, numprocs, N, rx, ry, rz, buffer);
   merge(myid, numprocs, N, rx1, ry1, rz1, buffer);
   merge(myid, numprocs, N, rx2, ry2, rz2, buffer);
   merge(myid, numprocs, N, rx3, ry3, rz3, buffer);
   merge(myid, numprocs, N, rx4, ry4, rz4, buffer);
   if(myid == 0)
    restart(step, cellx, celly, cellz, rx, ry, rz, rx1, ry1, rz1, rx2, ry2, rz2, rx3, ry3, rz3, rx4, ry4, rz4);
  }
 }

 if(myid == 0) fprintf(fp1,"**** end of dynamics ****\n");

 norm = (double)(nstep - estep);

 ave = ace / norm; avec = acec / norm; avk = ack / norm; avv = acv / norm; avp = acp / norm;
 acesq = (acesq / norm) - ave * ave; acecsq = (acecsq / norm) - avec * avec;
 acksq = (acksq / norm) - avk * avk; acvsq = (acvsq / norm) - avv * avv;
 acpsq = (acpsq / norm) - avp * avp;

 if(acesq > 0.0) fle = sqrt(acesq); if(acecsq > 0.0) flec = sqrt(acecsq);
 if(acksq > 0.0) flk = sqrt(acksq); if(acvsq > 0.0) flv = sqrt(acvsq);
 if(acpsq > 0.0) flp = sqrt(acpsq);

 avt = avk * 2.0 / NDIM;
 flt = flk * 2.0 / NDIM;

 if(myid == 0)
 {
  fprintf(fp1, "averages energy cutenergy kinetic potential temperature pressure\n");
  fprintf(fp1, "%12.6lf %12.6lf %12.6lf %12.6lf %12.6lf %12.6lf\n", ave, avec, avk, avv, avt, avp);
  fprintf(fp1, "fluctuation energy cutenergy kinetic potential temperature pressure\n");
  fprintf(fp1, "%12.6lf %12.6lf %12.6lf %12.6lf %12.6lf %12.6lf\n", fle, flec, flk, flv, flt, flp);
 }

 if ( myid == 0 )
 {
  endwtime = MPI_Wtime();
  fprintf(fp1, "wall clock time = %f seconds\n", endwtime-startwtime);
  fclose(fp1);
  fclose(fp2);
 }

 MPI_Finalize();

 return 0;
}
