#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $file = "excel12/database.xlsx";
&message ("Check that $file evaluates correctly.");
&test_sheet_calc ("$samples/$file", "Overview!C2:C99", sub { /(\s*0)+\s*/i });
