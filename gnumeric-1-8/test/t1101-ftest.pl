#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $file = "ftest.xls";
&message ("Check that $file evaluates correctly.");
&test_sheet_calc ("$samples/$file", "A1", sub { /\bOK\b/i });
