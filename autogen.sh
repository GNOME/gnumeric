#!/bin/sh
# Run this to generate all the initial makefiles, etc.

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

PKG_NAME="Gnumeric"

(test -f $srcdir/configure.in \
  && test -d $srcdir/src \
  && test -f $srcdir/src/gnumeric.h) || {
    echo -n "**Error**: Directory "\`$srcdir\'" does not look like the"
    echo " top-level gnumeric directory"
    exit 1
}

if libtool --version >/dev/null 2>&1; then
    vers=`libtool --version | sed -e "s/^[^0-9]*//" -e "s/ .*$//" | awk 'BEGIN { FS = "."; } { printf "%d", ($1 * 1000 + $2) * 1000 + $3;}'`
    if test "$vers" -ge 1003004; then
        true
    else
        echo "Please upgrade your libtool to version 1.3.4 or better." 1>&2
        exit 1
    fi
fi

. $srcdir/macros/autogen.sh
