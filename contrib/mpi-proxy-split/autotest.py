#!/usr/bin/env python

from shlex import split
from sys import stdout
from subprocess import call, PIPE
from os import path, getcwd
from time import sleep
from sys import argv

TESTS=["mpi_hello_world", "Abort_test", "Allreduce_test", "Alltoall_test",
       "Alltoallv_test", "Barrier_test", "Bcast_test", "Comm_split_test",
       "Reduce_test", "send_recv", "send_recv_many", "ping_pong",
       "Comm_dup_test", "Sendrecv_test", "Waitall_test", "Allgather_test",
       "Group_size_rank", "Type_commit_contiguous"]

NAME=path.basename(getcwd())
LIBNAME="libdmtcp_%s" % (NAME)
DMTCP_ROOT="../.."
# srun is for cori; change it to mpirun for local usage
MPIRUN="srun"
MPIRUN_FLAGS="-n 4"
DMTCP_COORD="%s/bin/dmtcp_coordinator" % (DMTCP_ROOT)
DMTCP_LAUNCH="%s/bin/dmtcp_launch" % (DMTCP_ROOT)
DMTCP_LAUNCH_FLAGS="-j --with-plugin ./%s.so" % (LIBNAME)

def checkTestSuccess(test=None, retcode=0):
  if "Abort_test" in test and retcode == 134:
    return "Pass"
  elif retcode == 0:
    return "Pass"
  else:
    return "Fail"

def startCoordinator():
  cmd = "%s -q -q --exit-on-last --daemon" % (DMTCP_COORD)
  return call(split(cmd)) == 0

def main():
  for idx, t in enumerate(sorted(TESTS)):
    TEST = "test/%s.exe" % (t)
    if (len(argv) > 1) and (TEST != argv[1]):
      continue
    if not startCoordinator():
      continue
    cmd = "%s %s %s %s %s" % \
          (MPIRUN, MPIRUN_FLAGS, DMTCP_LAUNCH, DMTCP_LAUNCH_FLAGS, TEST)
    output = "(%d/%d) Testing: %s ... " % (idx + 1, len(TESTS), t)
    stdout.write("%s" % (output))
    stdout.flush()
    rc = call(split(cmd), stdout=PIPE, stderr=PIPE)
    stdout.write("%s\n" % (checkTestSuccess(test=t, retcode=rc)))
    stdout.flush()

if __name__ == "__main__":
  # Execute only if run as a script
  main()
