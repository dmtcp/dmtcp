Put the following files from slurm-2.6.3/src/api/.libs/ directory here:
libpmi.a
libpmi.la
libpmi.lai
libpmi.so
libpmi.so.0
libpmi.so.0.0.0
libslurm.a
libslurmhelper.a
libslurmhelper.la
libslurm.la
libslurm.lai
libslurm.so
libslurm.so.26
libslurm.so.26.0.0

Custom SLURM build configuration must be performed according to cluster slurm installation.
On U. of Buffalo cluster it was enough to use following:
./configure --prefix=/ --sysconfdir=/ifs/user/artempol/slurm/etc/ --libdir=/usr/lib64/