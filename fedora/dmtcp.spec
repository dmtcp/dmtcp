Name:		dmtcp
Version:	1.2.7
Release:	1%{?dist}
Summary:	Checkpoint/Restart functionality for Linux processes
Group:		Applications/System
License:	LGPLv3+
URL:		http://dmtcp.sourceforge.net
Source0:	http://downloads.sourceforge.net/%{name}/%{name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root
Requires:	libmtcp = %{version}
BuildRequires:	gcc-c++
BuildRequires:	gcc
BuildRequires:	glibc-devel
BuildRequires:	python

# This package is functional only on i386 and x86_64 architectures.
ExclusiveArch:	%ix86 x86_64

%description
DMTCP (Distributed MultiThreaded Checkpointing) is a tool to transparently
checkpointing the state of an arbitrary group of programs including
multi-threaded and distributed computations.  It operates directly on the user
binary executable, with no Linux kernel modules or other kernel mods.

Among the applications supported by DMTCP are OpenMPI, MATLAB, Python, Perl,
and many programming languages and shell scripting languages.  DMTCP also
supports GNU screen sessions, including vim/cscope and emacs. With the use of
TightVNC, it can also checkpoint and restart X-Windows applications, as long as
they do not use extensions (e.g.: no OpenGL, no video).

This package contains DMTCP binaries.

%package -n libmtcp
Summary:	MTCP -- Single process checkpointer library
Group:		Development/Libraries

%description -n libmtcp
MTCP is the single process checkpoint package that is used by DMTCP to
checkpoint processes.

This package provides the libmtcp library that is required to checkpoint a
single process.

%package -n libmtcp-devel
Summary:	MTCP developer package
Group:		Development/Libraries
Requires:	libmtcp = %{version}

%description -n libmtcp-devel
This package provides files for developing applications that need to
interact with MTCP as opposed to DMTCP.

%package -n libdmtcpaware
Summary:	DMTCP programming interface
Group:		Development/Libraries
Requires:	%{name}%{?_isa}

%description -n libdmtcpaware
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
Group:		Development/Libraries
Requires:	libdmtcpaware%{?_isa} = %{version}

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
Group:		Development/Libraries
Requires:	libdmtcpaware-devel%{?_isa} = %{version}

%description -n libdmtcpaware-doc
This package provides some basic examples on how to use dmtcpaware.

%package -n libdmtcpaware-static
Summary:	DMTCP programming interface -- static library for developer pkg
Group:		Development/Libraries
Requires:	libdmtcpaware-devel%{?_isa} = %{version}

%description -n libdmtcpaware-static
This package provides static library that can be used to bundle dmtcpaware code
with user application.

%prep
%setup -q

%build
%configure --enable-mtcp-proc-maps
make %{?_smp_mflags}

%check
./test/autotest.py --slow || :

%install
make install DESTDIR=%{buildroot}

%clean
rm -rf %{buildroot}

%post -n libmtcp -p /sbin/ldconfig

%postun -n libmtcp -p /sbin/ldconfig

%post -n libdmtcpaware -p /sbin/ldconfig

%postun -n libdmtcpaware -p /sbin/ldconfig

%files
%defattr(-,root,root,-)
%{_bindir}/dmtcp_*
%{_bindir}/mtcp_restart
%{_libdir}/%{name}
%{_docdir}/%{name}-%{version}/QUICK-START
%{_docdir}/%{name}-%{version}/COPYING
%{_mandir}/man1/dmtcp.1.gz
%{_mandir}/man1/dmtcp_*.1.gz
%{_mandir}/man1/mtcp.1.gz
%{_mandir}/man1/mtcp_restart.1.gz

%files -n libmtcp
%defattr(-,root,root,-)
%{_libdir}/libmtcp.so.*

%files -n libmtcp-devel
%defattr(-,root,root,-)
%{_libdir}/libmtcp.so
%{_includedir}/mtcp.h

%files -n libdmtcpaware
%defattr(-,root,root,-)
%{_libdir}/libdmtcpaware.so.*

%files -n libdmtcpaware-devel
%defattr(-,root,root,-)
%{_includedir}/dmtcpaware.h
%{_libdir}/libdmtcpaware.so

%files -n libdmtcpaware-doc
%defattr(-,root,root,-)
%{_docdir}/%{name}-%{version}/examples

%files -n libdmtcpaware-static
%defattr(-,root,root,-)
%{_libdir}/libdmtcpaware.a

%changelog
* Mon Mar 11 2013 Kapil Arya <kapil@ccs.neu.edu> - 1.2.7-1
- Preparing for upstream release 1.2.7.
- Use %%{_docdir} instead of %%doc for QUICK-START and COPYING.
* Tue Oct 09 2012 Orion Poplawski <orion@cora.nwra.com> - 1.2.6-1
- Update to 1.2.6
- Use URL for Source0
- Add patch to drop -fstack-protector on mtcp_maybebpt.c
- Drop configure hack
- Run tests
* Sun Jul 08 2012 kapil@ccs.neu.edu
- Preparing for upstream release 1.2.5.
* Tue Jan 24 2012 kapil@ccs.neu.edu
- Preparing for upstream release 1.2.4.
* Mon Jan 23 2012 kapil@ccs.neu.edu
- Updating to svn 1449.
* Tue Oct 25 2011 kapil@ccs.neu.edu
- Updating to svn 1321.
- libdmtcpaware-devel-static renamed to libdmtcpaware-static
- %%{_isa} added to Requires
- disable_option_checking changed from "fatal" to "no"
- QUICK_START and COPYING installed using %%{doc}
* Tue Aug  9 2011 gene@ccs.neu.edu
- Updating to upstream release 1.2.3-1.svn1247M.
- svn revision 1246 adds objcopy to set section attribute in libmtcp.so
  (if debuginfo repo was present during build, limbtcp.so was missing a section)
- dmtcp.spec and 'make install' changed for improved file layout
* Tue Jul 26 2011 kapil@ccs.neu.edu
- Top level configure files updated to fix configure error.
* Fri Jul 22 2011 kapil@ccs.neu.edu
- Updating to upstream release 1.2.3.
* Sat Jul  2 2011 kapil@ccs.neu.edu
- Updating to upstream release 1.2.2.
* Wed Jun 22 2011 kapil@ccs.neu.edu
- Exclude mtcp.c from installation.
* Wed Jun 22 2011 kapil@ccs.neu.edu
- Updating to upstream release 1.2.2.
* Fri Jun 17 2011 kapil@ccs.neu.edu
- libdmtcpaware.a moved to libdmtcpaware-devel-static package.
- dmtcpaware examples moved to libdmtcpaware-doc package.
* Fri Jun 10 2011 kapil@ccs.neu.edu
- Build requirements updated.
- Minor cleanup.
* Tue Jun  7 2011 kapil@ccs.neu.edu
- Added "ExclusiveArch %%ix86 x86_64" and removed ExcludeArch lines.
- buildroot not cleaned in %%install section.
* Sat May 14 2011 kapil@ccs.neu.edu
- dependency on libc.a removed for mtcp_restart.
- Several other bug fixes and improvements.
* Sat Mar 12 2011 kapil@ccs.neu.edu
- Updated to release 1.2.1
* Fri Mar 11 2011 kapil@ccs.neu.edu
- Remove debug flags.
* Fri Mar 11 2011 kapil@ccs.neu.edu
- Updated to revision 935.
* Thu Mar 10 2011 kapil@ccs.neu.edu
- Reverting tarball to prev version.
* Thu Mar 10 2011 kapil@ccs.neu.edu
- Testing fix for restart under 32-bit OSes.
* Thu Mar 10 2011 kapil@ccs.neu.edu
- Updated tarball with compiler warnings fixed.
* Thu Mar 10 2011 kapil@ccs.neu.edu
- Added python to dependency list for running make check.
* Thu Mar 10 2011 kapil@ccs.neu.edu
- Preparing for release 1.2.1. Pulled updates from the latest dmtcp svn.
