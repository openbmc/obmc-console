#!/bin/sh

AUTOCONF_FILES="Makefile.in aclocal.m4 compile config.guess config.h.in \
        config.sub configure depcomp install-sh ltmain.sh missing *libtool*"

case $1 in
    clean)
        test -f Makefile && make maintainer-clean
        rm -rf ${AUTOCONF_FILES}

        exit 0
        ;;
esac

autoreconf -i
echo 'Run "./configure ${CONFIGURE_FLAGS} && make"'
