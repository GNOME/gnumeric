#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $file = "intersection-tests.gnumeric";
&message ("Check that $file evaluates correctly.");
&test_sheet_calc ("$samples/$file", "B1:B5", sub { /^(\bTRUE\b\s*)*$/ });
