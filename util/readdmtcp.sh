#!/bin/sh

if test "$1" = ""; then
  echo 'Usage:  readdmtcp.sh <CKPT IMAGE>'
  echo 'Example:  util/readdmtcp.sh ckpt_dmtcp1_*.dmtcp'
  exit 0
fi

trap 'rm -f ckpt_tmp.dmtcp' INT QUIT EXIT

if file $1 | grep gzip > /dev/null; then
  echo '***' $1 is a gzipped file.  Will uncompress it into ckpt_tmp.dmcp first.
  gzip -dc $1 > ckpt_tmp.dmtcp
  set ckpt_tmp.dmtcp
fi

dir=`dirname $0`

if which mtcp_restart > /dev/null 2> /dev/null; then
  mtcp_restart --simulate $1 2>&1
  exit 0
fi

# This next one assumes that this script resides in DMTCP_ROOT/util/
if test -x $dir/../bin/mtcp_restart; then
  $dir/../bin/mtcp_restart --simulate $1 2>&1
  exit 0
fi
