#!/bin/sh -xe
#
# Simple script to run the appropriate autotools from a repository.
#
autoreconf
rm -rf autom4te.cache config.log config.status
rm -f Makefile rdoff/Makefile doc/Makefile
rm -f config.h.in config.h config/config.h
