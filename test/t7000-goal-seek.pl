#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check that goal seeking works right.");

my @args = map { "--goal-seek=A$_:E$_"; } (4 ... 16);
&test_sheet_calc ("$samples/goal-seek.gnumeric", \@args,
		  "H4:H1000", sub { /^(\s*TRUE)+\s*$/ });
