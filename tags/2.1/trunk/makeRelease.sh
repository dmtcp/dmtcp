#!/bin/sh

VERSION=`cat VERSION`
VCS_SVN=svn://svn.code.sf.net/p/dmtcp/code

#run a command with error checking
e() {
  echo "$@" >&2
  $@ || (echo "ERROR '$@' failed!">&2; exit 1)
}

OLDDIR=`pwd`

STAGING=/tmp/dmtcp_release_staging_`whoami`
mkdir -p $STAGING
e cd $STAGING

REV=`(e svn info $VCS_SVN) | grep '^Revision: [0-9]*' | cut -d ' ' -f 2`
echo dmtcp HEAD revision: $REV
if test -z "$DMTCP_USE_SVN_REV"; then
  NAME=dmtcp-$VERSION
else
  NAME=dmtcp-$VERSION+svn$REV
fi

e svn export -r $REV $VCS_SVN/trunk $NAME
#e rm -rf $NAME/{makeRelease.sh}

e fakeroot tar cf $NAME.tar $NAME
e gzip -9 $NAME.tar
e rm -rf $NAME
e mv $NAME.tar.gz $OLDDIR
e cd $OLDDIR
e rm -rf $STAGING
e ls -al $NAME.tar.gz
