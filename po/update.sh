#!/bin/sh

xgettext --default-domain=gnumeric --directory=.. \
  --add-comments --keyword=_ --keyword=N_ \
  --files-from=./POTFILES.in \
&& test ! -f gnumeric.po \
   || ( rm -f ./gnumeric.pot \
    && mv gnumeric.po ./gnumeric.pot )
