#!/usr/bin/env python

import dmtcp

def my_ckpt():
    print("About to checkpoint.")

    dmtcp.checkpoint()
    print("Checkpoint done.")

    if dmtcp.isResume():
        print("The process is resuming from a checkpoint.")
    else:
        print("The process is restarting from a previous checkpoint.")
    return

x = 1
print(x)

print("Calling my_ckpt()")
my_ckpt()
x = 12
print(x)
