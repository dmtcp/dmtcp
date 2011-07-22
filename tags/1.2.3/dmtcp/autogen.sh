#!/bin/sh
# Does autoconf/automake as needed for DMTCP:
# See for overview:  http://www.openismus.com/documents/linux/automake/automake
# INPUT:  Makefile.am, configure.am
# RUNS:  autoconf, autoheader, automake, etc., in the right order
# OUTPUT:  config.h.in, Makefile.in, configure, etc.

autoreconf --force --install --verbose
