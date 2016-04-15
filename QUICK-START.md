# Overview of DMTCP

To install DMTCP, see [INSTALL.md](INSTALL.md).

## Concepts:

DMTCP Checkpoint/Restart allows one to transparently checkpoint to disk
a distributed computation.  It works under Linux, with no modifications
to the Linux kernel nor to the application binaries.  It can be used by
unprivileged users (no root privilege needed).  One can later restart
from a checkpoint, or even migrate the processes by moving the checkpoint
files to another host prior to restarting.

There is one DMTCP coordinator for each computation that you wish to
checkpoint.  By specifying `--coord-host` and `--coord-port` (or the environment
variables `DMTCP_COORD_HOST` and `DMTCP_COORD_PORT`), you can add a process
to a coordinator different from the default coordinator.  If you don't
specify, the default coordinator is always at (`localhost:7779`).

A DMTCP coordinator process is started on one host.  Application binaries
are started under the `dmtcp_launch` command, causing them to connect
to the coordinator upon startup.  As threads are spawned, child processes
are forked, remote processes are spawned via ssh, libraries are dynamically
loaded, DMTCP transparently and automatically tracks them.

By default, DMTCP uses gzip to compress the checkpoint images.  This can
be turned off (`dmtcp_launch --no-gzip` ; or setting an
environment variable to 0: `DMTCP_GZIP=0`).  This will be faster, and if
your memory is dominated by incompressible data, this can be helpful.
Gzip can add seconds for large checkpoint images.  Typically, checkpoint
and restart is less than one second without gzip.

A DMTCP checkpoint image includes any libraries (`.so` files) that it may
have been using.  This strategy is used for greater portability of
the checkpoint images --- and in some cases, it even allows migration of
the checkpoint images (and hence, processes) to hosts with different
Linux distributions, different Linux kernels, etc.

To run a program with checkpointing:

1. Run `dmtcp_coordinator` in a separate terminal/window

   ```
   bin/dmtcp_coordinator
   ```

2. In separate terminal(s), replace each command(s) with `dmtcp_launch [command]`

   ```
   bin/dmtcp_launch ./a.out
   ```

3. To checkpoint, type `c<return>` into `dmtcp_coordinator`

   In `dmtcp_coordinator` window:
   ```
   h<return> for help
   c<return> for checkpoint
   l<return> for list of processes to be checkpointed
   k<return> to kill processes to be checkpointed
   q<return> to kill processes to be checkpointed and quit the coordinator
   ```


4. Restart:
    Creating a checkpoint causes the `dmtcp_coordinator` to write
    a script, `dmtcp_restart_script.sh`, along with a
    checkpoint file (file type: `.dmtcp`) for each client process.
    The simplest way to restart a previously checkpointed computation is:
    ```
    ./bin/dmtcp_restart_script.sh
    ```

    * `./dmtcp_restart_script.sh` usually works "as is", but it can be edited.
    * Alternatively, if all processes were on the same processor,
      and there were no .dmtcp files prior to this checkpoint:
      ```
      ./bin/dmtcp_restart ckpt_*.dmtcp
      ```

## Convenience commands and debugging restarted processes:

1. Help exists:

   ```bash
   # bin/dmtcp_coordinator --help ; bin/dmtcp_launch --help ;
   # bin/dmtcp_command --help, etc.
   # Automatically start a coordinator in background
   bin/dmtcp_launch ./a.out &
   # Checkpoint all processes of the default coordinator
   bin/dmtcp_command --checkpoint
   # Kill a.out, and optionally kill coordinator process
   bin/dmtcp_command --kill
   # Kill a.out, and optionally kill coordinator process
   bin/dmtcp_command --quit
   # Restart directly from local checkpoint images (.dmtcp files)
   ./dmtcp_restart_script.sh
   # Or else, directly restart from the ckpt images in the current directory.
   # (Be sure there are no old ckpt_a.out_*.dmtcp files.
   #  Ensure that the restarted process is running, and not suspended.)
   bin/dmtcp_restart ckpt_a.out_*.dmtcp &
   # Have gdb attach to a restarted process, and debug
   # NOTE:  You must specify 'mtcp_restart', not 'dmtcp_restart'
   gdb ./a.out `pgrep -n MTCP`
   # force a.out to exit any low level libraries and return to a known location
   # set a breakpoint on a common function and continue:
   (gdb) break write
   (gdb) continue
   ```

2. To enable debug statements for DMTCP, configure with: `./configure
   --enable-debug` (or `./configure --help`, in general).
   The flag `--enable-debug` both prints to stderr and writes files.
   ```
   $DMTCP_TMPDIR/dmtcp-$USER@$HOST/jassertlog.*
   ```
   where `$DMTCP_TMPDIR` is `/tmp` by default on most distributions.
   In reading this, it's useful to know that
   DMTCP sets up barriers so that all processes proceed to the
   following states together during checkpoint: `RUNNING`, `SUSPENDED`,
   `FD_LEADER_ELECTION`, `DRAINED`, `CHECKPOINTED`, `REFILLED`, `RUNNING`.

3. `util/gdb-add-symbol-file` may be a useful debugging tool.  It computes
   the arguments for the `add-symbol-file` command of gdb, to import
   symbol information about a dynamic library.  It is most useful in
   combination with *-dbg Linux packages and prefix to `dmtcp_launch`:
   ```
   env LD_LIBRARY_PATH=/usr/lib/debug dmtcp_launch ...
   ```
   followed by `attach` in gdb.

## Command-line options:

`dmtcp_launch`, `dmtcp_command`, and `dmtcp_restart` print
their options when run with no command-line arguments.  `dmtcp_coordinator`
offers help when run (Type `h<return>` for help.).

Options through environment variables:

1. `dmtcp_coordinator`:

   * `DMTCP_CHECKPOINT_INTERVAL=<time in seconds>` (default: `0`, disabled)
   * `DMTCP_COORD_PORT=<coordinator listener port>` (default: `7779`)
   * `DMTCP_CHECKPOINT_DIR=<where restart script is written>` (default: `./`)
   * `DMTCP_TMPDIR=<where temporary files are written>`
     (default: environment variable `TMPDIR` or `/tmp`)

2. `dmtcp_launch` / `dmtcp_restart`:

   * `DMTCP_COORD_HOST=<hostname where coordinator will run>` (default: `localhost`)
   * `DMTCP_COORD_PORT=<coordinator listener port>` (default: `7779`)
   * `DMTCP_GZIP=<0: disable compression of checkpoint image>`
     (default: `1`, compression enabled)
   * `DMTCP_CHECKPOINT_DIR=<location to store checkpoints>` (default: `./`)
   * `DMTCP_SIGCKPT=<internal signal number>` (default: `12(SIGUSR2)`)
   * `DMTCP_TMPDIR=<where temporary files are written>`
     (default: environment variable `TMPDIR` or `/tmp`)

3. `dmtcp_command:
   * `DMTCP_COORD_HOST=<hostname where coordinator will run>` (default: `localhost`)
   * `DMTCP_COORD_PORT=<coordinator listener port>` (default: `7779`)

## Adapting DMTCP to application requirements and to external environments:

1. Application control over checkpoints:

   * `dmtcp_is_enabled()`   [Returns 0 (false) or 1 (true)]
   * `dmtcp_checkpoint()`   [with return values to distinguish resume and restart]
   * `dmtcp_disable_ckpt()` [Temporarily delay checkpoint during critical section]
   * `dmtcp_enable_ckpt()`  [Resume previously delayed checkpoint]

   [ See `test/plugin/applic-delayed-ckpt/` and
   `test/plugin/applic-initiated-ckpt/` for small, easily executed examples. ]

2. User-defined plugin modules may be added to DMTCP.  For examples,
   see the `test/plugin` directory of the source distribution.

   * Plugins provide user hooks for many common events, including:
      checkpoint, resume, restart, thread-start, thread-exit, exec program, etc.
   * Plugins provide for user-defined wrappers around library calls
      and system calls, to allow users to virtualize the external environment.
   * Plugins provide a plublish/subscribe service by which multiple
      processes may exchange information at the time of restart.
   [ See `doc/plugin-tutorial.pdf` for further informaiton, and see
     `test/plugin` for several small, easily executed examples. ]

## Short notes:

1. A restarted process sees the shared libraries and environment variables
   that existed prior to checkpoint.  These are contained in the .dmtcp
   checkpoint file.

2. At restart time, one can choose either to use the original
   `dmtcp_coordinator` or else to start a new coordinator.  Each process
   restarted by the `dmtcp_restart` command needs to know the host and port
   used by `dmtcp_coordinator`.  These default to `localhost` and port `7779`.
   The coordinator can be specified to use port 0, in which case the
   coordinator chooses arbitrary port, and prints it to stdout.  Setting
   `DMTCP_COORD_PORT` in the environment seen by the four main commands
   (`dmtcp_coordinator`, `dmtcp_launch`, `dmtcp_restart` and `dmtcp_command`)
   will override the default port.  Similarly, setting `DMTCP_COORD_HOST` for
   `dmtcp_launch` and `dmtcp_restart` is needed if they start on
   a different host than that of the coordinator.

3. It often works to migrate processes by moving the checkpoint files to
   another host and editing `dmtcp_restart_script.sh` prior to
   restarting.  Whether it works is affected by how different are the
   corresponding versions for the kernel and glibc.

4. Checkpoint is implemented by sending a signal to each user thread.
   As with all well-written code, your system calls should be prepared
   for an error return of `EINTR` (interrupted, due to a simultaneous
   checkpoint invocation or other kernel activity), in which case you
   can call the system call again.

5. Matlab should be invoked without graphics, and to be extra safe,
   without the JVM.  The -nodisplay and -nojvm flags for Matlab suffice:
   ```
   bin/dmtcp_launch matlab -nodisplay -nojvm
   ```

6. For Ubuntu/Debian Linux, ensure that `build-essential` is installed.
   For OpenSuse Linux, ensure that `linux-kernel-headers` is installed.

7. By default, successive checkpoints of the same process write to the same
   checkpoint image filename.  If you prefer that successive checkpoint be
   written to distinct filenames, then use:
   ```
   ./configure --enable-unique-checkpoint-filenames
   ```

8. Using Ctrl+C to kill a computation could leave some stale processes
   that remain connected to the coordinator. This can affect the
   restart. To ensure that this isn't the case, verify that there
   are no stale processes from your computation using `dmtcp_command`
   or `dmtcp_coordinator`. An alternative to Ctrl+C is to use the kill
   command in `dmtcp_command` or `dmtcp_coordinator`.

## Checkpointing Open MPI

Verify that `mpirun` works.
Verify `dmtcp_launch`, `dmtcp_restart`, etc. commands are in your path:
```
ssh <REMOTE-HOST> which dmtcp_launch
```
If they are not in your path, adjust your shell initialization file
   to extend your path.
Verify `ssh <REMOTE-HOST>` works without password otherwise
do the following:
```
ssh-keygen -t dsa       [accept default values]
ssh-keygen -t rsa       [accept default values]
cat ~/.ssh/id*.pub >> ~/.ssh/authorized_keys
```

```
make clean
make
make check

dmtcp_launch mpirun ./hello_mpi
dmtcp_command --checkpoint

./dmtcp_restart_script.sh
```

DMTCP uses SIGUSR2 as default and so do older versions of Open MPI.
If you have an older version (e.g < 1.3), try choosing a different
value of SIGNUM for DMTCP as follows:
```
dmtcp_launch --ckpt-signal <SIGNUM> mpirun ./hello_mpi
```

## Using DMTCP with X-Window:

Note that this method does not work with X extensions like OpenGL.
If someone wishes to extend this method to OpenGL, we have some
ideas for an approach that we can share.  Also, this method does
not currently successfully checkpoint an xterm, for reasons that
we do not fully understand.  We will look further into this later
when time and resources permit.

### TightVNC
Install TightVNC (either as a package from your Linux distro, or at:
    http://www.tightvnc.com
with installation instructions at:
  http://www.tightvnc.com/doc/unix/README.txt

If the server fails to start, you may need to specify the location of
fonts on your system. Do this by editing the `vncserver` Perl script
(which you put in your path above).  Modify the `$fontPath` variable
to point to your font directories. For example, you can list all of the
subdirectories of `/usr/share/fonts/` in the `fontPath`.

The processes started up automatically by the VNC server are listed in
the `~/.vnc/xstartup` file. Use the following as your `~/.vnc/xstartup`,
where we use the blackbox window manager and an `x_app` application
as an example:
```
#!/bin/csh
blackbox &
x_app
```

You should test that you can use the `vncserver` and `vncviewer` now.
This example uses desktop number 1:
```
vncserver :1
vncviewer localhost:1
# Kill vncviewer window manually, and then:
vncserver -kill :1
```

### Launch TightVNC with DMTCP

Make sure the executables `dmtcp_launch`, `dmtcp_coordinator`,
`dmtcp_restart`, and `dmtcp_command` are in your path.

Note that if the VNC server is killed without using the
`vncserver -kill`, there will be some temporary files left over that
prevent the server from restarting.  If this occurs, remove them:
```
rm -rf /tmp/.X1-lock /tmp/.X11-unix/X1
```
where X1 corresponds to starting the server on port 1.

Now, start the VNC server under checkpointing control:
```
dmtcp_launch vncserver :1
```

Use the VNC viewer to view your `x_app` application in the blackbox
window manager:
```
vncviewer localhost:1
```

Before checkpointing, close any xterm windows.  Also, close
the vncviewer itself.  They can be reopened again after
the checkpoint has completed.

[Optional] To verify that vncserver is running under checkpoint control:
```
dmtcp_command h
dmtcp_command s
```

To checkpoint the VNC server, `x_app`, and any other processes running
under the VNC server, remove any old checkpoint files, and type:
```
rm -f ckpt_*.dmtcp
dmtcp_command --checkpoint
```

This creates a file of the form `ckpt_*.dmtcp` for each process being
checkpointed.  To kill the vncviewer and restart, use the restart script:
```
vncserver -kill :1
# This script assumes dmtcp_restart is in your path.  If not,
#  modify the script to replace dmtcp_restart by a full path to it.
./dmtcp_restart_script.sh
```

Alternatively, you may prefer to directly use the `dmtcp_restart` command:
```
vncserver -kill :1
dmtcp_restart ckpt_*.dmtcp
```

Note: if checkpointing doesn't fully complete, make sure you're not out
of disk space, and that there are no other file system problems.

### Other applications with X Window support

Certain applications, such as some shells, vim, etc., try to
recognize mouse events from the X11 windows system.  While DMTCP
successfully checkpoints and restarts these applications, it does so
by disabling the connection to X11.  Mouse events are not recognized.

## Support for incremental and differential checkpoint using HBICT

[HBICT (Hash Based Incremental Checkpointing
Tool)](http://hbict.sourceforge.net/projects/hbict) 
provides DMTCP support for delta-compression (relative to the previous
checkpoint) which is then additionally compressed using gzip.

To enable it:

1. Download and install HBICT somewhere in your `PATH`.  For example,
   to test it in a single user's account:
   ```
   $ tar zxvf hbict-1.0.tar.gz; cd hbict-1.0; ./configure; make
   $ export PATH=$PWD/src:$PATH
   ```
   To see the options of 'hbict', execute:
   ```
   $ hbict --help
   ```
   Note, in particular, the `-r` option to create a new full checkpoint
   from the existing delta-compressed checkpoint files.

2. In the DMTCP directory, configure and re-make DMTCP for HBICT:
   ```
   $ ./configure --enable-delta-compression; make clean; make
   ```

3. The `--no-hbict` and `--no-gzip` options of `dmtcp_launch`
   will control what type of compression is used.
   a. `dmtcp_launch <my_prog>` causes successive checkpoints to be
      delta-compressed relative to previous checkpoints (HBICT
      compression) and then also compressed with gzip.
   b. `dmtcp_launch --no-gzip <my_prog>` causes successive
      checkpoints to be delta-compressed relative to previous checkpoints
     (using HBICT compression).
   c. `dmtcp_launch --no-hbict <my_prog>` causes gzipped checkpoints
      to be created with no delta-compression.
   d. `dmtcp_launch --no-hbict --no-gzip  <my_prog>` disables all
      compression.

   If delta-compression is used you'll see the several checkpoint files in
   your `DMTCP_CHECKPOINT_DIR` as in the following example using
   HBICT+gzip compression:
   ```
   $ dmtcp_launch test/dmtcp1
   $ ls -1 -sh
   4,0K ckpt_dmtcp1_6cbb52b0-12935-4ddcf7a0.dmtcp
   1,8M ckpt_dmtcp1_6cbb52b0-12935-4ddcf7a0.hbict.0
    56K ckpt_dmtcp1_6cbb52b0-12935-4ddcf7a0.hbict.1
    52K ckpt_dmtcp1_6cbb52b0-12935-4ddcf7a0.hbict.2
    ...
   ```

   In this example, the size of the delta-compressed checkpoints are
   32 times smaller, since test/dmtcp1 is mostly unchanging text (code),
   with little data to change.  The actualy delta-compression ratio
   depends strongly on the particular application.

4. To restart the application you'll need the file
   `ckpt_dmtcp1_6cbb52b0-12935-4ddcf7a0.dmtcp`, along with all the
   `*.hbict.N` files to be present in the same directory.  Then execute:
   ```
   $ dmtcp_restart ckpt_dmtcp1_6cbb52b0-12935-4ddcf7a0.dmtcp
   ```
   and the `*.hbict.N` files will automaticaly be discovered and used
   by the HBICT tool.

## Directory layout:

- `src`: DMTCP source code.
- `src/plugin`: source for DMTCP internal plugins.
- `jalib`: small pkg used by DMTCP for assertions, warnings, tracing code, etc.
- `bin`: DMTCP binaries (`dmtcp_launch`, `dmtcp_restart`, `dmtcp_coordinator`, etc.)
- `lib`: DMTCP internal libraries, including internal plugins that are
       not exposed to the end user.
- `test`: Used by 'make check'
- `test/plugin`: Simple examples for learning DMTCP plugins
- `plugin`: (top-level directory of optional plugins; they must be
            invoked by a command line flag of `dmtcp_launch`)
- `doc`: Random documentation on aspects of the DMTCP design
        Note especially:  doc/plugin-tutorial.pdf
- `include`: Contains dmtcp.h ; useful for third-party plugins
- `contrib`: Contributed plugins and other add-ons.  The more popular ones
            will eventually be migrated to the top-level plugin directory
            as they become mature;  Contrib plugins are not built by default.
- `util`: random utilities, useful mostly for experts
