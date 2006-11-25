#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check that code does not fix NULL and FALSE.");
&test_command ("cd $topsrc && $PERL tools/check-null-false-returns",
	       sub { /^$/ });
