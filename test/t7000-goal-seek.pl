#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check that goal seeking works right.");

my @args;
for (4 ... 8) { push @args, "--goal-seek=A$_:E$_"; }
&test_sheet_calc ("$samples/goal-seek.gnumeric", \@args,
		  "H4:H1000", sub { /^(\s*TRUE)+\s*$/ });
