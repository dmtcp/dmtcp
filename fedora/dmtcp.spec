%{!?_pkgdocdir: %global _pkgdocdir %{_docdir}/%{name}-%{version}}

Name:		dmtcp
Version:	3.0.0
Release:	0.0%{?dist}
Summary:	Checkpoint/Restart functionality for Linux processes
Group:		Applications/System
# dmtcp.h is ASL-2.0
License: LGPLv3+ and ASL-2.0
URL:		http://dmtcp.sourceforge.net
Source0:	http://downloads.sourceforge.net/%{name}/3.0.0/%{name}-%{version}.tar.gz
BuildRequires:	gcc-c++
BuildRequires:	python3

# This package is functional only on i386, x86_64 and aarch64 architectures.
# It should also work on %%ix86, but Koji seems to have problems with it.
ExclusiveArch:	x86_64 aarch64

%description
DMTCP (Distributed MultiThreaded Checkpointing) is a tool to
transparently checkpointing the state of an arbitrary group of
applications including multi-threaded and distributed computations.
It operates directly on the user binary executable, with no Linux kernel
modules or other kernel mods.

Among the applications supported by DMTCP are Open MPI, MVAPICH2, MATLAB,
R, Python, Perl, and many programming languages and shell scripting
languages.  It supports both TCP sockets and InfiniBand connections.
With the use of TightVNC, it can also checkpoint and restart X-Window
applications.  The OpenGL library for 3D graphics is supported through
a special plugin.

This package contains DMTCP binaries.

%package -n dmtcp-devel
Summary:	DMTCP developer package
Group:		Development/Libraries
Requires:	dmtcp%{?_isa} = %{version}-%{release}

%description -n dmtcp-devel
This package provides files for developing DMTCP plugins.

%prep
%setup -q

%build
%configure --docdir=%{_pkgdocdir}
%make_build

# A few tests may take a long time.  If a test times out, the check fails.
# Hopefully, the test machine is fast enough, and timeouts are long
#   enough to avoid that problem.
%check
AUTOTEST="--retry-once --slow --slow" make check

%install
%make_install

%files
%{_bindir}/dmtcp_*
%{_bindir}/mtcp_restart
%{_libdir}/%{name}
%{_pkgdocdir}/
%{_mandir}/man1/*.1*

%files -n dmtcp-devel
%{_includedir}/dmtcp.h

%changelog
* Tue Feb 16 2021 Gene Cooperman <gene@ccs.neu.edu> - 3.0.0
- Preparing for upstream release 3.0.

* Tue Dec 17 2019 Gene Cooperman <gene@ccs.neu.edu> - 2.6.1~rc1-0.1
- Preparing for upstream release 2.3.

* Thu Jul 3 2014 Kapil Arya <kapil@ccs.neu.edu> - 2.3-1
- Preparing for upstream release 2.3.

* Mon Mar 10 2014 Kapil Arya <kapil@ccs.neu.edu> - 2.2-1
- Preparing for upstream release 2.2.
- Remove libmtcp* packages.
- Install all docs in _pkgdocdir
- Added --retry-once flag to autotest.

* Fri Jan 10 2014 Kapil Arya <kapil@ccs.neu.edu> - 2.1-1
- Preparing for upstream release 2.1.

* Thu Dec 12 2013 Ville Skytta <ville.skytta@iki.fi> - 1.2.8-2
- Install docs to %%{_pkgdocdir} where available (#993726).
- Own package level doc dir.

* Tue Jul 30 2013 Kapil Arya <kapil@ccs.neu.edu> - 1.2.8-1
- Preparing for upstream release 1.2.8.

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
