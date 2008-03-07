#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $file = "ttest.xls";
&message ("Check that $file evaluates correctly.");
&test_sheet_calc ("$samples/$file", "Overview!B1:D2", sub { !/\bbug\b/i });
