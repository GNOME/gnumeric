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

. $srcdir/macros/autogen.sh

rm -f xlibtool
mv libtool xlibtool
rm -f $srcdir/xltmain.sh
mv $srcdir/ltmain.sh $srcdir/xltmain.sh
sed -e 's/^archive_cmds="\(.*\)"$/archive_cmds="\1 \\$deplibs"/' xlibtool > libtool
sed -e 's/\$show "extracting global C symbols from.*//' <  $srcdir/xltmain.sh > $srcdir/ltmain.sh

chmod +x libtool $srcdir/ltmain.sh
