#!/bin/sh
set -v

# This will test the multi-arch functionality in a semi-automated way.
# For a less verbose treatment, try:
#    sh -x ./multi-arch.sh > /dev/null

dir=/tmp/$USER

# Clean up any old files
rm -rf $dir/dmtcp-multi-arch
rm -rf $dir/dmtcp-multi-arch-build

mkdir -p $dir
cd $dir
git clone https://github.com/dmtcp/dmtcp.git dmtcp-multi-arch
mkdir -p $dir/dmtcp-multi-arch-build
cd dmtcp-multi-arch
./configure --prefix=$dir/dmtcp-multi-arch-build --enable-m32 && make clean \
    && make -j && make install || \
  (echo "*** multi-arch not supported ***" && false) || \
  exit 1
./configure --prefix=$dir/dmtcp-multi-arch-build && make clean && make -j \
    && make install
gcc -m32 -o dmtcp-tmp test/dmtcp1.c
file dmtcp-tmp
make tidy
echo "**** Launching .... ****"
(sleep 12 && pkill -9 dmtcp-tmp) & \
    $dir/dmtcp-multi-arch-build/bin/dmtcp_launch -p0 -i6 ./dmtcp-tmp
echo "**** Restarting .... ****"
(sleep 12 && pkill -9 'dmtcp-tmp|mtcp_restart') & \
    $dir/dmtcp-multi-arch-build/bin/dmtcp_restart -p0 ckpt_dmtcp-tmp_*.dmtcp
echo "**** The above application should have restarted. ****"
