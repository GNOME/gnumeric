#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&test_command ("cd $topsrc && $PERL tools/check-null-false-returns",
	       sub { /^$/ });
