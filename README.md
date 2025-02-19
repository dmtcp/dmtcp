# [DMTCP: Distributed MultiThreaded CheckPointing](http://dmtcp.sourceforge.net/) 
[![Build](https://github.com/dmtcp/dmtcp/actions/workflows/make-check.yml/badge.svg)](https://github.com/dmtcp/dmtcp/actions/workflows/make-check.yml)

DMTCP is a tool to transparently checkpoint the state of multiple simultaneous
applications, including multi-threaded and distributed applications. It
operates directly on the user binary executable, without any Linux kernel
modules or other kernel modifications.

DMTCP supports:
 * most Linux distros
 * x86_64, i386, arm64, risc-v

If you are looking for transparent checkpointing of MPI, please see:
 * MANA package: https://github.com/mpickpt/mana
 * MANA documentation: https://mana-doc.readthedocs.io/en/latest/

(MANA is implemented as a plugin on top of DMTCP.  MANA stands for
 MPI-Agnostic Network-Agnostic transparent checkpointing.)

Since DMTCP supports most binaries that use the POSIX C library,
DMTCP should also support the following applications:
OpenMP, MATLAB, Python, Perl, R, and many programming languages and
shell scripting languages. DMTCP also supports GNU screen sessions,
including vim/cscope and emacs. With the use of TightVNC, it can also
checkpoint and restart X Window applications.  For a multilib (mixture
of 32- and 64-bit processes), see "./configure --enable-multilib".

To install DMTCP, see [INSTALL.md](INSTALL.md).

For an overview DMTCP, see [QUICK-START.md](QUICK-START.md).

For the license, see [COPYING](COPYING).

For more information on DMTCP, see: [http://dmtcp.sourceforge.net](http://dmtcp.sourceforge.net).

For the latest version of DMTCP (both official release and git), see
the "Releases" on the home page at:
[https://github.com/dmtcp/dmtcp] (https://github.com/dmtcp/dmtcp)

----

Finally, there is also an only loosely related package supported by our
research group, which shares the philosophy of transparency.

If you are interested in _easily_ debugging applications that use
POSIX threads and a transparent model checker, then please also
consider
 * https://github.com/mcminickpt/mcmini
 * https://mcmini-doc.readthedocs.io/
This finds
bugs in deadlock, assertion failures, crashes, etc., by simply running
(for example):
    ./mcmini -f -q -m15 ./a.out [args] ...

This runs directly on the original multithreaded binary executable, without
modification.
This package has also been classroom-test at the undergraduate level.
Feel free to write to @gc00 on github, if you are interested in using
this package as a teaching tool for POSIX threads.
