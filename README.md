# [DMTCP: Distributed MultiThreaded CheckPointing](http://dmtcp.sourceforge.net/) [![Build Status](https://travis-ci.org/dmtcp/dmtcp.png?branch=master)](https://travis-ci.org/dmtcp/dmtcp)

DMTCP is a tool to transparently checkpoint the state of multiple simultaneous
applications, including multi-threaded and distributed applications. It
operates directly on the user binary executable, without any Linux kernel
modules or other kernel modifications.

Among the applications supported by DMTCP are MPI (various implementations),
OpenMP, MATLAB, Python, Perl, R, and many programming languages and shell
scripting languages. DMTCP also supports GNU screen sessions, including
vim/cscope and emacs. With the use of TightVNC, it can also checkpoint
and restart X Window applications.  For a multilib (mixture of 32-
and 64-bit processes), see "./configure --enable-multilib".

DMTCP supports the commonly used OFED API for InfiniBand, as well as its
integration with various implementations of MPI, and resource managers
(e.g., SLURM).

To install DMTCP, see [INSTALL.md](INSTALL.md).

For an overview DMTCP, see [QUICK-START.md](QUICK-START.md).

For the license, see [COPYING](COPYING).

For more information on DMTCP, see: [http://dmtcp.sourceforge.net](http://dmtcp.sourceforge.net).

For the latest version of DMTCP (both official release and git), see:
[http://dmtcp.sourceforge.net/downloads.html](http://dmtcp.sourceforge.net/downloads.html).
