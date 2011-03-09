#!/bin/sh

VERSION=1.2.0
VCS_SVN=https://dmtcp.svn.sourceforge.net/svnroot/dmtcp

#run a command with error checking
e() {
  echo "$@" >&2
  $@ || (echo "ERROR '$@' failed!">&2; exit 1)
}

#get svn revision number
#getRev() {
#  if [[ -z "$1" ]]
#  then
#    getRev .
#  else
#    (cd $1 && e svn info) | grep '^Revision: [0-9]*' | cut -d ' ' -f 2  
#  fi
#}

#list a dirs named ".svn"
#removeSvnDirs() {
#  find $@ -type d | grep '[.]svn$' | xargs rm -rf
#}


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
e rm -rf $NAME/{makeRelease.sh}

#REV=`getRev dmtcp_staging`
#e svn co https://dmtcp.svn.sourceforge.net/svnroot/dmtcp/trunk dmtcp_staging


#e mv dmtcp_staging $NAME
#e rm -rf $NAME/{makeRelease.sh}
#e removeSvnDirs

# FIXME: Not sure if we need it anymore, removing it for now. Re-insert if feel
#        the need.              -- Kapil
#archName=`dpkg-architecture | grep DEB_HOST_ARCH_CPU | \
#          sed -e's%DEB_HOST_ARCH_CPU=%%'`
#sed -i -e "s%Architecture: any%Architecture: $archName%" $NAME/debian/control

e fakeroot tar cf $NAME.tar $NAME
e gzip -9 $NAME.tar
e rm -rf $NAME
e mv $NAME.tar.gz $OLDDIR
e cd $OLDDIR
e rm -rf $STAGING
e ls -al $NAME.tar.gz
