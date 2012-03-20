Name:		dmtcp
Version:	1.2.4
Release:	1%{?dist}
Summary:	Checkpoint/Restart functionality for Linux processes
Group:		System/Packages
License:	LGPLv3+
URL:		http://dmtcp.sourceforge.net
Source0:	%{name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root
BuildRequires:  gcc-c++
BuildRequires:  gcc
BuildRequires:  glibc-devel
%if 0%{?suse_version} >= 1100
BuildRequires:  python-base
%else
BuildRequires:  python
%endif

# This package is functional only on i386 and x86_64 architectures.
ExclusiveArch:  %ix86 x86_64

%description
DMTCP (Distributed MultiThreaded Checkpointing) is a tool to transparently
checkpointing the state of an arbitrary group of programs including
multi-threaded and distributed computations.  It operates directly on the user
binary executable, with no Linux kernel modules or other kernel mods.

Among the applications supported by DMTCP are OpenMPI, MATLAB, Python, Perl,
and many programming languages and shell scripting languages.  DMTCP also
supports GNU screen sessions, including vim/cscope and emacs. With the use of
TightVNC, it can also checkpoint and restart X-Window applications, as long as
they do not use extensions (e.g.: no OpenGL, no video).

This package contains DMTCP binaries.

%package -n libmtcp1
Summary:	MTCP -- Single process checkpointer library
Group:		Development/Libraries/Other
Suggests:	%{name}

%description -n libmtcp1
MTCP is the single process checkpoint package that is used by DMTCP to
checkpoint processes.

This package provides the libmtcp libraty that is required to checkpoint a
single process.

%package -n libmtcp-devel
Summary:	MTCP developer package
Group:		Development/Libraries/Other
Requires:       libmtcp1 = %{version}

%description -n libmtcp-devel
This package provides files for developing applications that need to
interact with MTCP as opposed to DMTCP.

%package -n libdmtcpaware1
Summary:	DMTCP programming interface
Group:		Development/Libraries/Other
Requires:       %{name}

%description -n libdmtcpaware1
DMTCP (Distributed MultiThreaded Checkpointing) is a tool to transparently
checkpointing the state of an arbitrary group of programs including
multi-threaded and distributed computations.  It operates directly on the user
binary executable, with no Linux kernel modules or other kernel mods.

Among the applications supported by DMTCP are OpenMPI, MATLAB, Python, Perl,
and many programming languages and shell scripting languages.  DMTCP also
supports GNU screen sessions, including vim/cscope and emacs. With the use of
TightVNC, it can also checkpoint and restart X-Windows applications, as long as
they do not use extensions (e.g.: no OpenGL, no video).

This package provides a programming interface to allow checkpointed
applications to interact with dmtcp.

%package -n libdmtcpaware-devel
Summary:	DMTCP programming interface -- developer package
Group:		Development/Libraries/Other
Requires:       libdmtcpaware1 = %{version}

%description -n libdmtcpaware-devel
DMTCP (Distributed MultiThreaded Checkpointing) is a tool to transparently
checkpointing the state of an arbitrary group of programs including
multi-threaded and distributed computations.  It operates directly on the user
binary executable, with no Linux kernel modules or other kernel mods.

Among the applications supported by DMTCP are OpenMPI, MATLAB, Python, Perl,
and many programming languages and shell scripting languages.  DMTCP also
supports GNU screen sessions, including vim/cscope and emacs. With the use of
TightVNC, it can also checkpoint and restart X-Windows applications, as long as
they do not use extensions (e.g.: no OpenGL, no video).

This package provides libraries for developing applications that need to
interact with dmtcp.

%package -n libdmtcpaware-doc
Summary:	DMTCP programming interface -- basic examples
Group:		Development/Libraries/Other
Requires:       libdmtcpaware1 = %{version}

%description -n libdmtcpaware-doc
DMTCP (Distributed MultiThreaded Checkpointing) is a tool to transparently
checkpointing the state of an arbitrary group of programs including
multi-threaded and distributed computations.  It operates directly on the user
binary executable, with no Linux kernel modules or other kernel mods.

Among the applications supported by DMTCP are OpenMPI, MATLAB, Python, Perl,
and many programming languages and shell scripting languages.  DMTCP also
supports GNU screen sessions, including vim/cscope and emacs. With the use of
TightVNC, it can also checkpoint and restart X-Windows applications, as long as
they do not use extensions (e.g.: no OpenGL, no video).

This package provides some basic examples on how to use dmtcpaware.

%package -n libdmtcpaware-devel-static
Summary:	DMTCP programming interface -- static library for devloper pkg
Group:		Development/Libraries/Other
Requires:       libdmtcpaware-devel = %{version}

%description -n libdmtcpaware-devel-static
DMTCP (Distributed MultiThreaded Checkpointing) is a tool to transparently
checkpointing the state of an arbitrary group of programs including
multi-threaded and distributed computations.  It operates directly on the user
binary executable, with no Linux kernel modules or other kernel mods.

Among the applications supported by DMTCP are OpenMPI, MATLAB, Python, Perl,
and many programming languages and shell scripting languages.  DMTCP also
supports GNU screen sessions, including vim/cscope and emacs. With the use of
TightVNC, it can also checkpoint and restart X-Windows applications, as long as
they do not use extensions (e.g.: no OpenGL, no video).

This package provides static library that can be used to bundle dmtcpaware code
with user application.

%prep
%setup -q

%build
%configure --docdir=%{_docdir}/%{name} --disable-option-checking
make %{?_smp_mflags}


%check
# disable the test for now as bash is failing with 32-bit when built on 64-bit machine.
%ifarch %x86_64
 ./test/autotest.py --slow
%endif

%install
%make_install

%clean
rm -rf %{buildroot}

%post -n libmtcp1
/sbin/ldconfig

%postun -n libmtcp1
/sbin/ldconfig

%post -n libdmtcpaware1 
/sbin/ldconfig

%postun -n libdmtcpaware1
/sbin/ldconfig

%files
%defattr(-,root,root,-)
%{_bindir}/dmtcp_*
%{_libdir}/%{name}
%{_libdir}/%{name}/dmtcphijack.so
%{_bindir}/mtcp_restart
%{_mandir}/man1/*
%doc QUICK-START COPYING
%exclude %{_docdir}/%{name}/examples

%files -n libmtcp1
%defattr(-,root,root,-)
%{_libdir}/libmtcp.so.*

%files -n libmtcp-devel
%defattr(-,root,root,-)
%{_includedir}/mtcp.h
%{_libdir}/libmtcp.so

%files -n libdmtcpaware1
%defattr(-,root,root,-)
%{_libdir}/libdmtcpaware.so.*

%files -n libdmtcpaware-devel
%defattr(-,root,root,-)
%{_includedir}/dmtcpaware.h
%{_libdir}/libdmtcpaware.so

%files -n libdmtcpaware-doc
%defattr(-,root,root,-)
%docdir %{_docdir}/%{name}/examples
%{_docdir}/%{name}/examples

%files -n libdmtcpaware-devel-static
%defattr(-,root,root,-)
%{_libdir}/libdmtcpaware.a

%changelog
* Mon Jan 24 2012 Kapil Arya <kapil@ccs.neu.edu> - 1.2.4-1
- Preparing for upstream release 1.2.4.
  + Release Notes from upstream:
- There is now much more robust treatment of processes that rapidly
    create and destroy threads.  This was the case for the Java JVM
    (both for OpenJDK and Oracle (Sun) Java).  This was also the case
    for Cilk.  Cilk++ was not tested.  We believe this new DMTCP to now be
    highly robust -- and we would appreciate receiving a notification if
    you find a Java or Cilk program that is not compatible with DMTCP.
- Zero-mapped pages are no longer expanded and saved to the DMTCP checkpoint
    image.  For Java programs (and other programs using zero-mapped
    pages for their allocation arena or garbage collecotr), the checkpoint
    image will now be much smaller.  Checkpoint and restart times
    will also be faster.
- DMTCP_ROOT/dmtcp/doc directory added with documentation of some
    DMTCP internals.  architecture-of-dmtcp.pdf is a good place to
    start reading for those who are curious.
- The directory of example plugins was moved to DMTCP_ROOT/test/plugin.
    This continues to support third-part wrappers around system calls,
    can registering functions to be called by DMTCP at interesting times
    (like pre-checkpoint, post-resume, post-restart, new thread created, etc.).
- This version of MTCP (inside this package) should be compatible with
    the checkpoint-restart service of Open MPI.  The usage will be
    documented soon through the Open MPI web site.  As before, an alternative
    is to simply start Open MPI inside DMTCP, and let DMTCP treat all of
    Open MPI as a "black box" that happens to be a ditributed computation
- A new --prefix command line flag has been added to dmtcp_checkpoint.
    It operates similarly to the flag of the same name in Open MPI.
    For distributed computations, remote processes will use the prefix
    as part of the path to find the remote dmtcp_checkpoint command.
    This is useful when a gateway machine has a different directory
    structure from the remote nodes.
- configure --enable-ptrace-support now uses ptrace plugin (more modular code).
    The ptrace plugin should also be more robust.  It now fixes some
    additional cases that were missing earlier
- ./configure --enable-unique-checkpoint-filenames  was not respecting
    bin/dmtcp_checkpoint --checkpoint-open-files .  This is now fixed.
- If the coordinator received a kill request in the middle of a checkpoint,
    the coordinator could freeze or die.  This has now been fixed, with
    the expected behavior:  Kill the old computation that is in the
    middle of a checkpoint, and then allow any new computations to begin.
- dmtcp_inspector utility was broken in last release; now fixed
- configure --enable-forked-checkpoint was broken in the last release.
    It is fixed again.
- Many smaller bug fixes.

* Sun Jan 23 2012 Kapil Arya <kapil@ccs.neu.edu> - 1.2.3+svn1449-1
- Updating to latest upstream revision.

* Mon Jul 25 2011 Kapil Arya <kapil@ccs.neu.edu> - 1.2.3+svn1214-1
- Configure error fixed for failing build on Fedora 15.

* Wed Jul 22 2011 Kapil Arya <kapil@ccs.neu.edu> - 1.2.3-1
- Updated to upstream release 1.2.2

* Wed Jun 22 2011 Kapil Arya <kapil@ccs.neu.edu> - 1.2.2-1
- Updated to upstream release 1.2.2

* Sat Mar 12 2011 Kapil Arya <kapil@ccs.neu.edu> - 1.2.1-1
- Updated to release 1.2.1

* Wed Mar 9 2011 Kapil Arya <kapil@ccs.neu.edu> - 1.2.0+svn928-1
- Preparing for release 1.2.1.

* Sun Feb 6 2011 Kapil Arya <kapil@ccs.neu.edu> - 1.2.0+svn886-1
- Intial RPM release.
