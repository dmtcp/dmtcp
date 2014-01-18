#
# spec file for package dmtcp
#
# Copyright (c) 2013 SUSE LINUX Products GmbH, Nuernberg, Germany.
# Copyright (c) 2012 Kapil Arya
#
# All modifications and additions to the file contributed by third parties
# remain the property of their copyright owners, unless otherwise agreed
# upon. The license for this file, and modifications and additions to the
# file, is the same license as for the pristine package itself (unless the
# license for the pristine package is not an Open Source License, in which
# case the license is the MIT License). An "Open Source License" is a
# license that conforms to the Open Source Definition (Version 1.9)
# published by the Open Source Initiative.

# Please submit bugfixes or comments via http://bugs.opensuse.org/
#


Name:           dmtcp
Version:        1.2.8
Release:        0
Summary:        Checkpoint/Restart functionality for Linux processes
License:        LGPL-3.0+
Group:          System/Packages
Url:            http://dmtcp.sourceforge.net
Source0:        %{name}-%{version}.tar.gz
BuildRoot:      %{_tmppath}/%{name}-%{version}-build
BuildRequires:  gcc
BuildRequires:  gcc-c++
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

Among the applications supported by DMTCP are Open MPI, MATLAB, Python, Perl,
and many programming languages and shell scripting languages.  DMTCP also
supports GNU screen sessions, including vim/cscope and emacs. With the use of
TightVNC, it can also checkpoint and restart X-Window applications, as long as
they do not use extensions (e.g.: no OpenGL, no video).

This package contains DMTCP binaries.

%package -n libmtcp1
Summary:        MTCP -- Single process checkpointer library
Group:          Development/Libraries/Other
Suggests:       %{name}

%description -n libmtcp1
MTCP is the single process checkpoint package that is used by DMTCP to
checkpoint processes.

This package provides the libmtcp libraty that is required to checkpoint a
single process.

%package -n libmtcp-devel
Summary:        MTCP developer package
Group:          Development/Libraries/Other
Requires:       libmtcp1 = %{version}

%description -n libmtcp-devel
This package provides files for developing applications that need to
interact with MTCP as opposed to DMTCP.

%package -n libdmtcpaware1
Summary:        DMTCP programming interface
Group:          Development/Libraries/Other
Requires:       %{name}

%description -n libdmtcpaware1
DMTCP (Distributed MultiThreaded Checkpointing) is a tool to transparently
checkpointing the state of an arbitrary group of programs including
multi-threaded and distributed computations.  It operates directly on the user
binary executable, with no Linux kernel modules or other kernel mods.

Among the applications supported by DMTCP are Open MPI, MATLAB, Python, Perl,
and many programming languages and shell scripting languages.  DMTCP also
supports GNU screen sessions, including vim/cscope and emacs. With the use of
TightVNC, it can also checkpoint and restart X-Windows applications, as long as
they do not use extensions (e.g.: no OpenGL, no video).

This package provides a programming interface to allow checkpointed
applications to interact with dmtcp.

%package -n libdmtcpaware-devel
Summary:        DMTCP programming interface -- developer package
Group:          Development/Libraries/Other
Requires:       libdmtcpaware1 = %{version}

%description -n libdmtcpaware-devel
DMTCP (Distributed MultiThreaded Checkpointing) is a tool to transparently
checkpointing the state of an arbitrary group of programs including
multi-threaded and distributed computations.  It operates directly on the user
binary executable, with no Linux kernel modules or other kernel mods.

Among the applications supported by DMTCP are Open MPI, MATLAB, Python, Perl,
and many programming languages and shell scripting languages.  DMTCP also
supports GNU screen sessions, including vim/cscope and emacs. With the use of
TightVNC, it can also checkpoint and restart X-Windows applications, as long as
they do not use extensions (e.g.: no OpenGL, no video).

This package provides libraries for developing applications that need to
interact with dmtcp.

%package -n libdmtcpaware-doc
Summary:        DMTCP programming interface -- basic examples
Group:          Development/Libraries/Other
Requires:       libdmtcpaware1 = %{version}

%description -n libdmtcpaware-doc
DMTCP (Distributed MultiThreaded Checkpointing) is a tool to transparently
checkpointing the state of an arbitrary group of programs including
multi-threaded and distributed computations.  It operates directly on the user
binary executable, with no Linux kernel modules or other kernel mods.

Among the applications supported by DMTCP are Open MPI, MATLAB, Python, Perl,
and many programming languages and shell scripting languages.  DMTCP also
supports GNU screen sessions, including vim/cscope and emacs. With the use of
TightVNC, it can also checkpoint and restart X-Windows applications, as long as
they do not use extensions (e.g.: no OpenGL, no video).

This package provides some basic examples on how to use dmtcpaware.

%package -n libdmtcpaware-devel-static
Summary:        DMTCP programming interface -- static library for devloper pkg
Group:          Development/Libraries/Other
Requires:       libdmtcpaware-devel = %{version}

%description -n libdmtcpaware-devel-static
DMTCP (Distributed MultiThreaded Checkpointing) is a tool to transparently
checkpointing the state of an arbitrary group of programs including
multi-threaded and distributed computations.  It operates directly on the user
binary executable, with no Linux kernel modules or other kernel mods.

Among the applications supported by DMTCP are Open MPI, MATLAB, Python, Perl,
and many programming languages and shell scripting languages.  DMTCP also
supports GNU screen sessions, including vim/cscope and emacs. With the use of
TightVNC, it can also checkpoint and restart X-Windows applications, as long as
they do not use extensions (e.g.: no OpenGL, no video).

This package provides static library that can be used to bundle dmtcpaware code
with user application.

%prep
%setup -q
sed -i -e 's,enable_option_checking=fatal,enable_option_checking=no,' configure

%build
%configure --docdir=%{_docdir}/%{name} --disable-option-checking
make %{?_smp_mflags}

%check
# disable the test for now as bash is failing with 32-bit when built on 64-bit
# machine.
%ifarch %x86_64
 ./test/autotest.py --slow
%endif

%install
%make_install

%post -n libmtcp1 -p /sbin/ldconfig

%postun -n libmtcp1 -p /sbin/ldconfig

%post -n libdmtcpaware1 -p /sbin/ldconfig

%postun -n libdmtcpaware1 -p /sbin/ldconfig

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
