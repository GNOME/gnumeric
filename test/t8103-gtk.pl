#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check that code includes gtk toplevel only.");
&test_command ("cd $topsrc && $PERL tools/check-gtk-includes", sub { /^$/ });
