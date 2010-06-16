#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $file = "yalta2008.xls";
&message ("Check that $file evaluates correctly.");
&test_sheet_calc ("$samples/excel/$file", "D10:D99", sub { /^(\s*TRUE)+\s*$/i });
