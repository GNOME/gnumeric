#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $file = "bitwise.xls";
&message ("Check that $file evaluates correctly.");
&test_sheet_calc ("$samples/excel/$file", "Summary!C5", sub { /^0$/ });
