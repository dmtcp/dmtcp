#
# spec file for package dmtcp
#
# Copyright (c) 2014 SUSE LINUX Products GmbH, Nuernberg, Germany.
# Copyright (c) 2012-2014 Kapil Arya
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
Version:        2.4
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
BuildRequires:  glibc-devel
%if 0%{?suse_version} >= 1100
BuildRequires:  python-base
%else
BuildRequires:  python
%endif

# This package is functional only on i386 and x86_64 architectures.
ExclusiveArch:  %ix86 x86_64

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
Summary:        DMTCP developer package
Group:          Development/Libraries/Other
Requires:       dmtcp = %{version}

%description -n dmtcp-devel
This package provides dmtcp.h for developing plugins.

%prep
%setup -q
sed -i -e 's,enable_option_checking=fatal,enable_option_checking=no,' configure

%build
%configure --docdir=%{_docdir}/%{name} --disable-option-checking
make %{?_smp_mflags}

%check
make tests
./test/autotest.py --retry-once --slow || :

%install
%make_install

%files
%defattr(-,root,root,-)
%{_bindir}/dmtcp_*
%{_bindir}/mtcp_restart
%{_libdir}/%{name}
%{_mandir}/man1/*
%{_docdir}/%{name}

%files -n dmtcp-devel
%defattr(-,root,root,-)
%{_includedir}/dmtcp.h

%changelog
