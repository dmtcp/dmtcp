#!/bin/sh

if file $1 | grep gzip > /dev/null; then
  echo '***' $1 is a gzipped file.  Please uncompress it first.
  exit 0
fi

dir=`dirname $0`

if which mtcp_restart > /dev/null; then
  mtcp_restart --simulate $1
  exit 0
fi

# This next one assumes that this script resides in DMTCP_ROOT/util/
if test -x $dir/../bin/mtcp_restart; then
  $dir/../bin/mtcp_restart --simulate $1
  exit 0
fi
