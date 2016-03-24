#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $file = "complex-tests.gnumeric";
&message ("Check that $file evaluates correctly.");
&test_sheet_calc ("$samples/$file", "A3", sub { $_ > 15 });
