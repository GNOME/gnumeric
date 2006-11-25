#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $file = "regress.gnumeric";
&message ("Check that $file evaluates correctly.");
&test_sheet_calc ("$samples/$file", "C2", sub { /No/i });
