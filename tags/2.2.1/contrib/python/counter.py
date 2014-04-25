#!/usr/bin/python

import time
import sys

n = 0
while True:
    n += 1
    print "%d " % (n),
    sys.stdout.flush()
    time.sleep(1)
