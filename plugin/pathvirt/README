# pathvirt (Path Virtualization DMTCP Plugin)

pathvirt is a DMTCP plugin that virtualizes access to file system paths for
programs run under DMTCP. This allows path access to be transparently
redirected elsewhere on the file system on DMTCP restart.

## Usage

pathvirt operates on the basis of path prefixes and is interfaced with using
the DMTCP_ORIGINAL_PATH_PREFIX/DMTCP_NEW_PATH_PREFIX environment variable.

On DMTCP init, set DMTCP_ORIGINAL_PATH_PREFIX to a colon delimited list of path
prefixes to register them with pathvirt. This is required, but does not
actually change anything about the execution of the program until the first
restart. For example:

    $ DMTCP_ORIGINAL_PATH_PREFIX=/home/user1/bin1:/home/user1/lib1:/home/user1/doc1 \
        dmtcp_launch -i4 --with-plugin $DMTCP_ROOT/lib/dmtcp/libdmtcp_pathvirt.so \
        ./my-program

Then, on any DMTCP restart, one can optionally set DMTCP_NEW_PATH_PREFIX to a
second colon-delimited list of path prefixes. For example:

    $ DMTCP_NEW_PATH_PREFIX=/home/user2/bin2:/home/user2/lib2:/home/user2/doc2 \
        ./dmtcp_restart_script.sh

If one *does not* provide DMTCP_NEW_PATH_PREFIX on restart, pathvirt does nothing,
and the program is restarted from the last checkpoint as normal.

If one *does* provide it, pathvirt becomes active and starts performing path
translation.  At this point, prefixes in this second list will dynamically
replace corresponding prefixes in the first list when a program uses a path
that is prefixed with an entry in the first colon list in a call to libc. For
example:

    open("/home/user1/bin1/ls") => open("/home/user2/bin2/ls")
    open("/home/user1/lib1/libc.so.6") => open("/home/user2/lib2/libc.so.6")
    open("/home/user1/doc1/proj/README") => open("/home/user2/doc2/proj/README")
    open("/not/registered/prefix.txt") => open("/not/registered/prefix.txt")

### Usage Rules

1.  Environment variables:
     - DMTCP_ORIGINAL_PATH_PREFIX: recognized at time of launch or restart (to override)
     - DMTCP_NEW_PATH_PREFIX: recognized at time of restart only

2.  Programmatic API:
     - dmtcp_set_original_path_prefix_list()
     - dmtcp_get_original_path_prefix_list()
     - dmtcp_get_new_path_prefix_list()
    NOTE: There is no dmtcp_set_new_path_prefix()

3.  dmtcp_set_original_path_prefix() can be called at any time
    (at checkpoint time, within a user's plugin wrapper function, etc.).

    However, this will have no effect until the next restart.
    The user must then set the environment variable DMTCP_NEW_PATH_PREFIX
    prior to restart.

4.  On restart, if the original path and new path are of different lengths,
    this is a user error, and DMTCP will report the current old and new path,
    and exit.
    (The DMTCP_NEW_PATH_PREFIX can be set prior to restart to guarantee that
    the path lists are of the same length.)

5.  If there is more than one match of a pathname with a path prefix
    in DMTCP_ORIGINAL_PATH_PREFIX, then the first match is used.  This is
    useful for creating exceptions to a path translation.

    As an example, suppose user2 wants to restart by using a checkpoint
    image created by user1.  User2 wants to start in the directory of
    user2, since he or she has no permission to write in the directory
    of user1.  But user2 should continue to use the original configuration
    file, /home/user1/config.txt .  The following solution will work:
    > DMTCP_ORIGINAL_PATH_PREFIX=/home/user1/config.txt:/home/user1
    > DMTCP_NEW_PATH_PREFIX=/home/user1/config.txt:/home/user2

## Constraints

- The maximum size permitted for the DMTCP_*_PATH_PREFIX environment variables
  is 10*1024 bytes at time of writing. This configuration can be increased by
  editing `plugin/pathvirt/pathvirt.cpp:MAX_ENV_VAR_SIZE` and recompiling.
- The DMTCP_*_PATH_PREFIX environment variables are also constrained by a DMTCP
  environment variable limit of 12*1024 bytes at time of writing. This
  configuration can be increased by editing `src/dmtcpplugin.cpp:MAXSIZE` and
  recompiling.
- Virtualization of certain path prefixes such as `/proc` or `/dev` is
  experimental and unsupported.
- Strict POSIX compliance in regards to certain error handling (in particular,
  EFAULT on invalid address) is not fully supported.
- Path virtualization for processes that are fork-ed and exec-ed from
  a restarted process is supported. The child process inherits the path
  translations from its (restarted) parent.

  However, note that there's no clean way to distinguish a process that's
  fork-ed and exec-ed prior to ckpt-ing from a process that's fork-ed
  and exec-ed after a restart, other than creating a side-effect on the
  filesystem. And so, for now, we delegate the responsibility of error
  checking on the user. The implication is that if a user, by accident
  or by intention, were to set the two env. vars prior to the first
  checkpoint, the pathvirt plugin would get activated.
- Any process that's started over ssh does not inherit the environment
  of its parent on a remote node, by default. This requires special ssh
  options -- ssh -o SendEnv, and PermitUserEnvironment in sshd config --
  which are disabled by default in most cases. Consequently, the plugin
  will not be activated for the process started over ssh.

## Testing

To run a basic test, cd into plugin/pathvirt/test/slot5.

    make check
    *** wait for a checkpoint, then ctrl-c ***
    make restart
    *** wait for a checkpoint, then ctrl-c ***
    make test

You should observe that on launch, the slot5/***5/pv-test.txt files were being
written to, but after restart the ../misc/slot7/***7/pv-test.txt files were
being written to.

---

Authors:
Mark Mossberg <mark.mossberg@gmail.com>
Rohan Garg <rohgarg@ccs.neu.edu>
