Name:		dmtcp
Version:	1.2.3+svn1245
Release:	1%{?dist}
Summary:	Checkpoint/Restart functionality for Linux processes
Group:		System/Packages
License:	LGPL
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
TightVNC, it can also checkpoint and restart X-Windows applications, as long as
they do not use extensions (e.g.: no OpenGL, no video).

This package contains DMTCP binaries.

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
Requires:       libdmtcpaware-devel = %{version}

%description -n libdmtcpaware-doc
This package provides some basic examples on how to use dmtcpaware.

%package -n libdmtcpaware-devel-static
Summary:	DMTCP programming interface -- static library for devloper pkg
Group:		Development/Libraries/Other
Requires:       libdmtcpaware-devel = %{version}

%description -n libdmtcpaware-devel-static
This package provides static library that can be used to bundle dmtcpaware code
with user application.

%prep
%setup -q

%build
%configure --disable-option-checking
make %{?_smp_mflags}


%check
# disable the test for now as bash is failing with 32-bit when built on 64-bit machine.
#%ifarch %x86_64
./test/autotest.py --slow
#%endif

%install
%make_install

%clean
rm -rf %{buildroot}

%post -n libdmtcpaware1 
/sbin/ldconfig

%postun -n libdmtcpaware1
/sbin/ldconfig

%files
%defattr(-,root,root,-)
%{_bindir}/dmtcp_*
%{_libdir}/%{name}
%{_libdir}/%{name}/dmtcp_*
%{_libdir}/%{name}/dmtcphijack.so
%{_libdir}/%{name}/libmtcp.so
%{_bindir}/mtcp_restart
%exclude %{_libdir}/%{name}/examples
%exclude %{_includedir}/mtcp.h
%doc QUICK-START COPYING

%files -n libdmtcpaware1
%defattr(-,root,root,-)
%{_libdir}/libdmtcpaware.so.*

%files -n libdmtcpaware-devel
%defattr(-,root,root,-)
%{_includedir}/dmtcpaware.h
%{_libdir}/libdmtcpaware.so

%files -n libdmtcpaware-doc
%defattr(-,root,root,-)
%docdir %{_libdir}/%{name}/examples
%{_libdir}/%{name}/examples

%files -n libdmtcpaware-devel-static
%defattr(-,root,root,-)
%{_libdir}/libdmtcpaware.a

%changelog
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
