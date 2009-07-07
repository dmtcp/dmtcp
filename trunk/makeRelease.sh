#!/bin/sh

VERSION=1.03

#run a command with error checking
function e(){
  echo "$@" >&2
  $@ || (echo "ERROR '$@' failed!">&2; exit 1)
}

#get svn revision number
function getRev(){
  if [[ -z "$1" ]]
  then
    getRev .
  else
    (cd $1 && e svn info) | grep '^Revision: [0-9]*' | cut -d ' ' -f 2  
  fi
}

#list a dirs named ".svn"
function removeSvnDirs(){
  find $@ -type d | grep '[.]svn$' | xargs rm -rf
}


OLDDIR=`pwd`

STAGING=/tmp/dmtcp_release_staging_`whoami`
mkdir -p $STAGING
e cd $STAGING

e svn co https://dmtcp.svn.sourceforge.net/svnroot/dmtcp dmtcp_staging

REV=`getRev dmtcp_staging`
NAME=dmtcp_$VERSION-r$REV

e mv dmtcp_staging $NAME
e rm -rf $NAME/{makeRelease.sh,branches}
e removeSvnDirs
e fakeroot tar cf $NAME.tar $NAME
e gzip -9 $NAME.tar
e rm -rf $NAME
e mv $NAME.tar.gz $OLDDIR
e cd $OLDDIR
e rm -rf $STAGING
e ls -al $NAME.tar.gz

