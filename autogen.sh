#!/bin/sh
# Run this to generate all the initial makefiles, etc.

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

PKG_NAME="Gnumeric"

if test -d ${srcdir}/libglade; then
    > ${srcdir}/libglade/NO-AUTO-GEN
    > ${srcdir}/libglade/libglade.spec
    > ${srcdir}/libglade/libgladeConf.sh
else
    echo you need to checkout gnumeric again 
    exit 1
fi

(test -f $srcdir/configure.in \
  && test -d $srcdir/src \
  && test -f $srcdir/src/gnumeric.h) || {
    echo -n "**Error**: Directory "\`$srcdir\'" does not look like the"
    echo " top-level gnumeric directory"
    exit 1
}

. $srcdir/macros/autogen.sh

