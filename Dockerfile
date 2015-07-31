# Dockerfile to build DMTCP container images.
FROM phusion/baseimage
MAINTAINER Kapil Arya <kapil@ccs.neu.edu>

RUN apt-get update -q && apt-get -qy install    \
      build-essential                           \
      gdb                                       \
      git-core                                  \
      make                                      \
      python                                    \
      python2.7                                 \
      wget

RUN mkdir -p /dmtcp
RUN mkdir -p /tmp

WORKDIR /dmtcp
RUN git clone https://github.com/dmtcp/dmtcp.git /dmtcp && \
      git checkout master &&                    \
      git log -n 1

RUN ./configure --prefix=/usr && make -j 2 && make install
