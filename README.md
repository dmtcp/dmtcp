# [DMTCP: Distributed MultiThreaded CheckPointing](http://dmtcp.sourceforge.net/) [![Build Status](https://travis-ci.org/dmtcp/dmtcp.png?branch=master)](https://travis-ci.org/dmtcp/dmtcp)

DMTCP is a tool to transparently checkpoint the state of multiple simultaneous
applications, including multi-threaded and distributed applications. It
operates directly on the user binary executable, without any Linux kernel
modules or other kernel modifications.

Among the applications supported by DMTCP are Open MPI, MATLAB, Python, Perl,
and many programming languages and shell scripting languages. DMTCP also
supports GNU screen sessions, including vim/cscope and emacs. With the use of
TightVNC, it can also checkpoint and restart X Window applications, as long as
they do not use extensions (e.g.: no OpenGL, no video). See the QUICK-START
file for further details.

DMTCP supports the OFED API for InfiniBand on an experimental basis. For older
versions of OFED, the DMTCP 2.1 release should be adequate. For newer versions
of OFED, please use DMTCP 2.2.

To install DMTCP, see the file:
  INSTALL

For an overview DMTCP, see the file:
  QUICK-START

For the licene an overview DMTCP, see the file:
  COPYING

Directory layout:
 - subdirectories: dmtcp and mtcp : will eventually be combined into
   	a future top-level 'src' subdirectory
 - bin : DMTCP binaries (`dmtcp_launch`, `dmtcp_restart`, `dmtcp_coordinator`, etc.)
 - lib : DMTCP internal libraries, including internal plugins that are
        not exposed to the end user.
 - test : Used by 'make check'
 - plugin : (top-level directory of optional plugins; they must be
             invoked by a command line flag of 'dmtcp_launch')
 - doc : Random documentation on aspects of the DMTCP design
         Note especially:  doc/plugin-tutorial.pdf
 - include : Contains dmtcp.h ; useful for third-party plugins
 - contrib : Contributed plugins and other addons.  The more popular ones
             will eventually be migrated to the top-level plugin directory
             as they become mature;  Contrib plugins are not built by default
 - util : random utilities useful mostly for experts

For more information on DMTCP, see:
  [http://dmtcp.sourceforge.net](http://dmtcp.sourceforge.net)
That web page also contains the release notes for each DMTCP release.

For the latest version of DMTCP (both official release and svn), see:
  [http://dmtcp.sourceforge.net/downloads.html](http://dmtcp.sourceforge.net/downloads.html)
