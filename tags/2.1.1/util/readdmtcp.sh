#!/bin/sh

if file $1 | grep gzip > /dev/null; then
  echo '***' $1 is a gzipped file.  Please uncompress it first.
  exit 0
fi

if which mtcp_restart > /dev/null; then
  mtcp_restart --simulate $1
  exit 0
fi

if test -x bin/mtcp_restart; then
  bin/mtcp_restart --simulate $1
  exit 0
fi

if test -x ../../bin/mtcp_restart; then
  ../../bin/mtcp_restart --simulate $1
  exit 0
fi
