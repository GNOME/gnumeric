#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $file = "arrays.xls";
&message ("Check that $file evaluates correctly.");
&test_sheet_calc ("$samples/$file", "Summary!B2", sub { /TRUE/ });
