#!/usr/bin/env python

# Copyright (C) 2016 Kyle Harrigan (kwharrigan@gmail.com)
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
# 
#     http://www.apache.org/licenses/LICENSE-2.0
# 
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
# implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# portions based off hookexample.py

import sys
sys.path.append("../../contrib/python")

import dmtcp
import os
from ctypes import *


# Note that we must use the libc version of getenv.  As it turns out,
# the python version makes the actual getenv call upon module import,
# and then caches the result for calls by os.environ, os.getenv, etc.
#
# This makes it tricky for modify-env to get restarted env vars into
# Python.  
#
# So, as undesirable as it may seem, the trick for now is to use CDLL
# and get it straight from the horse's mouth.

libc = CDLL('libc.so.6')
getenv = libc.getenv
getenv.restype = c_char_p # getenv returns a char*

def do_ckpt():
    '''
    Checkpoint, and then indicate if we are in the initial checkpoint
    or in a restarted version
    '''
    print("About to checkpoint.")
    dmtcp.checkpoint()
    print("Checkpoint done.")
    if dmtcp.isResume():
        print("The process is resuming from a checkpoint.")
    else:
        print("The process is restarting from a previous checkpoint.")
    return

print("Calling do_ckpt()")
do_ckpt()
if dmtcp.isRestart():
    print('Restarted...HOME should be set to value in dmtcp_env.txt')
else:
    print('First time...HOME should be set to your home')
print('HOME=[%s] (from libc.so getenv)' % getenv('HOME'))
print('HOME=[%s] (from os.getenv)' % os.getenv('HOME'))
