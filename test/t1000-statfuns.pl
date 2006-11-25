#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $file = "statfuns.xls";
&message ("Check that $file evaluates correctly.");
&test_sheet_calc ("$samples/excel/$file", "A4", sub { /All ok/i });
