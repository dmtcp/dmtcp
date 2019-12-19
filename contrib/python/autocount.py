#!/usr/bin/env python

import sys, time

def increment():
    global num
    num += 1

def print_i():
    global num
    print(num)
    print("Hello World! %d" % (num))
    sys.stdout.flush()

def rest():
    time.sleep(1)

num = 0

while True:
    increment()
    print_i()
    #rest()
    if num == 10:
        print("put breakpoint here.")
