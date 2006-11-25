#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $file = "operator.xls";
&message ("Check that $file evaluates correctly.");
&test_sheet_calc ("$samples/excel/$file", "C1:C15", sub { /^(\s*Success)+\s*$/i });
