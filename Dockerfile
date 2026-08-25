# Dockerfile to build DMTCP container images.
FROM debian:10-slim as builder
LABEL maintain="Kapil Arya <kapil@ccs.neu.edu>"

RUN apt-get update -q && apt-get -qy install    \
      build-essential                           \
      git-core                                  \
      make

WORKDIR /dmtcp
COPY . .
ARG MAKE_JOBS="2"
RUN ./configure --prefix=/usr && make -j "$MAKE_JOBS" && make install

FROM debian:10-slim
COPY --from=builder /usr/local/ /usr/local/
