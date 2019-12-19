#!/usr/bin/env python

import os, sys, time

cwd = os.getcwd()
last_digit = cwd[-1]
fil  = 'pv-test.txt'
count = 0

fullpath = '{}/{}'.format(cwd, fil)
binpath = '{}/bin{}/{}'.format(cwd, last_digit, fil)
docpath = '{}/doc{}/{}'.format(cwd, last_digit, fil)
libpath = '{}/lib{}/{}'.format(cwd, last_digit, fil)

while True:
    print('[{}] Appending...'.format(count))
    try:
        with open(binpath, 'a+') as f:
            f.write('{} appending\n'.format(count))
        with open(docpath, 'a+') as f:
            f.write('{} appending\n'.format(count))
        with open(libpath, 'a+') as f:
            f.write('{} appending\n'.format(count))
        count += 1
        time.sleep(1)
    except IOError:
        print('could not open file')
        sys.exit(1)
