#!/bin/bash

pid=$1
lsof_cache=""
lsof_L_cache=""


prepare_lsof_cache()
{
  echo "-->Cache LSOF output for future use"
  lsof_cache=`mktemp /tmp/lsof.XXXXXXXXXX`
  lsof_L_cache=`mktemp /tmp/lsof_L.XXXXXXXXXX`
  lsof > $lsof_cache
  lsof -L > $lsof_L_cache
}

search_proc()
{
  pattern="$1"
  for l in `find  /proc/[1-9]*/fd/ -type l 2>/dev/null`; do
    tmp=`readlink $l | grep "$pattern"`
    if [ -n "$tmp" ]; then
      echo "$l"
    fi
  done
}

get_uid()
{
  con=$1
  prefix="$2:["
  uid=${con##"$prefix"}
  uid=${uid%"]"}
  echo $uid
}

parse_pipe()
{
  con="$1"
  uid=`get_uid "$con" "pipe"`
  output="pipe[$uid], rd:"

  for l in `cat $lsof_L_cache | grep $uid | awk '{ print ( $1":"$2":"$3":"$4 ); }'`; do
    tmp=`echo $l | awk -F":" '{ print $4; }' | grep r`
    cmdline=`echo $l | awk -F":" '{ print $1; }' | awk '{ print $1 }'`
    pid1=`echo $l | awk -F":" '{ print $2; }'`
    if [ -n "$tmp" ]; then
      output="$output $cmdline[$pid1]"
    fi
  done
  output="$output, wr:"
  for l in `cat $lsof_cache | grep $uid | awk '{ print ( $1":"$2":"$3":"$4 ); }'`; do
    tmp=`echo $l | awk -F":" '{ print $4; }' | grep "w"`
    cmdline=`echo $l | awk -F":" '{ print $1; }' | awk '{ print $1 }'`
    pid1=`echo $l | awk -F":" '{ print $2; }'`
    if [ -n "$tmp" ]; then
      output="$output $cmdline[$pid1]"
    fi
  done
  echo "$output"
}


addr_hex2human()
{
  addr="$1"
  ip=${addr%%:*}
  port=${addr##*:}
  ip_split=`echo $ip | fold -w2`

  ip_hum=""
  flag=0
  for n in $ip_split; do
    tmp=`echo "ibase=16; $n" | bc`
    if [ "$flag" = 1 ]; then 
      ip_hum="$tmp.$ip_hum"
    else
      ip_hum="$ip_hum$tmp"
      flag=1
    fi
  done
  port_hum=`echo "ibase=16; $port" | bc`
  echo "$ip_hum:$port_hum"
}

unixsock_info()
{
  echo -n "[unix]:"
  uid="$2"
  fd="$3"
  uid1=`expr "$uid" + 1`
  uid2=`expr "$uid" - 1`
  for l in `search_proc "$uid1"`; do
    if [ "$l" != "/proc/$pid/fd/$fd" ]; then
      cmdline=`cat ${l%/*}/../cmdline`
      pid1=${l%/*}
      pid1=${pid1%/*}
      pid1=${pid1##*/}
      cmdline=${cmdline%%-*}
      echo -n " (maybe)$cmdline[$pid1, uid=$uid1]"
    fi
  done

  for l in `search_proc "$uid2"`; do
    if [ "$l" != "/proc/$pid/fd/$fd" ]; then
      cmdline=`cat ${l%/*}/../cmdline`
      pid1=${l%/*}
      pid1=${pid1%/*}
      pid1=${pid1##*/}
      cmdline=${cmdline%%-*}
      echo -n " (maybe)$cmdline[$pid1, uid=$uid2]"
    fi
  done

  echo

}

udpsock_info()
{
  echo  "[udp]"
}

tcp_findends()
{
  uid="$1"
  for l in `search_proc "$uid"`; do
    cmdline=`cat ${l%/*}/../cmdline`
    pid1=${l%/*}
    pid1=${pid1%/*}
    pid1=${pid1##*/}
    echo "$cmdline[$pid1]"
  done
}

tcpsock_info()
{
  uid="$2"
  fd="$3"
  addr1_hex=`echo $1 | awk '{ printf $3 }'`
  addr2_hex=`echo $1 | awk '{ printf $4 }'`
  addr1=`addr_hex2human "$addr1_hex"`
  addr2=`addr_hex2human "$addr2_hex"`
  ip1=${addr1%%:*}
  port1=${addr1##*:}
  ip2=${addr2%%:*}
  port2=${addr2##*:}

  local_info=""
  if [ "$ip1" = "$ip2" ] && [ "$port2" != 0 ]; then
    pattern="$addr2_hex $addr1_hex"
    descr=`grep "$pattern" /proc/$pid/net/tcp`
    uid1=`echo "$descr" | awk '{ printf $10; }'`
    cmdline=`tcp_findends "$uid1"`
    local_info=", local partner=$cmdline"
  fi
  echo "[tcp] loc:$addr1 -> rem:$addr2 $local_info"
}


parse_sock()
{
  con="$1"
  fd="$2"

  uid=`get_uid "$con" "socket"`
  echo -n "socket[$uid], "
  descr=`grep -a $uid ../net/* 2>/dev/null`
  case "$descr" in
      *unix* ) unixsock_info "$descr" "$uid" "$fd";;
      *udp* ) udpsock_info "$descr" "$uid" "$fd";;
      *tcp* ) tcpsock_info "$descr" "$uid" "$fd";;
          * ) echo -e "\tUnknown type of socket!";;
  esac
}

parse_file()
{
 con=$1
 echo "[file] $con"
}


parse_con()
{
	con="$1"
  fd="$2"
	
	is_pipe=`echo $con | grep "pipe:\[[0-9]*\]"`
	is_sock=`echo $con | grep "socket:\[[0-9]*\]"`
	
	if [ -n "$is_pipe" ]; then
		parse_pipe "$con" "$fd"
	elif [ -n "$is_sock" ]; then 
		parse_sock "$con" "$fd"
	else
		parse_file "$con" "$fd"
	fi

}

if [ -z "$1" ]; then
	echo "$0 - Display information about process with pid=<PID>"
	echo "Usage: $0 <PID>"
	exit 0
fi

cwdir=`pwd`
prepare_lsof_cache

cat /proc/$pid/cmdline
echo 
echo

cd /proc/$pid/fd/
fds=`ls -1 | sort -n`
for l in $fds; do
	con=`readlink $l`
	echo -n "$l: "
	parse_con "$con" "$l"
done

cd $cwdir
rm $lsof_cache $lsof_L_cache