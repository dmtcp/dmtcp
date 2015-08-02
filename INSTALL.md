## Installing DMTCP

### Prerequisites

### Build instructions

To build, use:
```
  ./configure
  make
  make check [Optional]
```

As usual, you may prefer to use `make` with `'-j'` for faster, parallel builds.

`make check2`, and `make check3` will exercise further examples in the
test subdirectory:
```
  check2: readline.c
  check3: shared-memory.c
```

This software runs in the original directory, and
```
  make install
```
will install to the install directory based on configure.

Distribution packages for Debian/Ubuntu, OpenSUSE and Fedora are available in their respective repositories.

### Multilib support
To support multilib (mixed use of 32- and 64-bit applications),
there are two possibilities:
1.  Install both the 32- and 64-bit versions of DMTCP packages for your distro.
OR:
2.  Build from source:
```bash
  ./configure
  make
  # Do NOT do 'make clean' here.  You will lose bin/dmtcp_launch, etc.
  ./configure --enable-m32
  make clean   # This won't remove the 64-bit binary and library files.
  make
  # Optionally, change 'make' to 'make install'' if desired.
```

When running DMTCP with multilib support, the 64-bit applicationss,
```
  dmtcp_launch, dmtcp_restart, and dmtcp_command
```
all inter-operate with both 32- and 64-bit applications.

If your process will create multiple processes, you should use standard DMTCP.
A brief overview follows.
The general methodology is:
```
  ./dmtcp_coordinator #in one window: type h to the coordinator to see commands
  ./dmtcp_launch a.out <args,...>
```

Note that files `/tmp/${USER}/dmtcp-${USER}@${HOST}/jassertlog.*` are created
with debugging information if you configured with `--enable-debug`.
See `./configure --help` for that and other options.

See the file [QUICK-START.md](QUICK-START.md) for further information on using DMTCP.


## Running without a separate coordinator process:

Usually, the most convenient procedure is to use DMTCP in its
default mode, which includes a separate DMTCP coordinator process.
In some rare cases, you may wish to avoid a separate coordinator process by
providing a `--no-coordinator` flag to `dmtcp_launch`. Multiprocess
computations are not supported with this flag.
